#pragma once
#ifndef __linux__
#error Linux native comparison helper requires Linux
#endif
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>
#include <dlfcn.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace linuxbench {
inline void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
inline std::string quote(std::string_view value){
    std::string out="\"";constexpr char digits[]="0123456789abcdef";
    for(unsigned char c:value){if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c<32){out+="\\u00";out+=digits[c>>4];out+=digits[c&15];}else out+=char(c);}
    return out+'"';
}
template<class T>std::string hex(const T& bytes){std::string out;for(auto c:bytes){out+="0123456789abcdef"[c>>4];out+="0123456789abcdef"[c&15];}return out;}
struct Identity {
    std::string name,uuid,driverUuid;std::uint64_t id{};
    std::uint32_t vendor{},device{},driver{},api{},type{};
    std::string json()const{
        std::ostringstream o;o<<"{\"name\":"<<quote(name)<<",\"device_uuid\":"<<quote(uuid)<<",\"driver_uuid\":"<<quote(driverUuid)
            <<",\"adapter_id\":"<<quote(std::to_string(id))<<",\"vendor_id\":"<<vendor<<",\"device_id\":"<<device
            <<",\"driver_version_raw\":"<<driver<<",\"api_version_raw\":"<<api<<",\"device_type\":"<<type<<",\"device_luid\":null}";return o.str();
    }
};
struct NativeQuery {
    void* library{};PFN_vkGetInstanceProcAddr get{};VkInstance instance{};PFN_vkDestroyInstance destroy{};std::string loaderPath;
    NativeQuery()=default;NativeQuery(const NativeQuery&)=delete;NativeQuery& operator=(const NativeQuery&)=delete;
    void close(){if(instance&&destroy)destroy(instance,nullptr);instance={};if(library)dlclose(library);library=nullptr;}
    ~NativeQuery(){close();}
    template<class T>T load(const char* name){auto p=reinterpret_cast<T>(get(instance,name));require(p!=nullptr,name);return p;}
    std::vector<Identity> enumerate(bool validation=false){
        require(!library,"Query reused");library=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL);require(library!=nullptr,"Linux Vulkan loader unavailable");
        get=reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library,"vkGetInstanceProcAddr"));require(get!=nullptr,"Vulkan entry unavailable");
        Dl_info info{};require(dladdr(reinterpret_cast<void*>(get),&info)&&info.dli_fname,"Loader path unavailable");loaderPath=std::filesystem::canonical(info.dli_fname).string();
        const char* layer="VK_LAYER_KHRONOS_validation";
        if(validation){
            auto layers=load<PFN_vkEnumerateInstanceLayerProperties>("vkEnumerateInstanceLayerProperties");std::uint32_t count{};
            require(layers(&count,nullptr)==VK_SUCCESS,"Layer enumeration failed");std::vector<VkLayerProperties> rows(count);
            require(layers(&count,rows.data())==VK_SUCCESS,"Layer enumeration failed");rows.resize(count);
            require(std::any_of(rows.begin(),rows.end(),[&](auto& r){return std::string_view(r.layerName)==layer;}),"Required comparison validation layer absent");
        }
        auto create=load<PFN_vkCreateInstance>("vkCreateInstance");VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_1;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;if(validation){ci.enabledLayerCount=1;ci.ppEnabledLayerNames=&layer;}
        require(create(&ci,nullptr,&instance)==VK_SUCCESS,"Native instance creation failed");destroy=load<PFN_vkDestroyInstance>("vkDestroyInstance");
        auto enumerateDevices=load<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices");auto properties=load<PFN_vkGetPhysicalDeviceProperties2>("vkGetPhysicalDeviceProperties2");
        auto queues=load<PFN_vkGetPhysicalDeviceQueueFamilyProperties>("vkGetPhysicalDeviceQueueFamilyProperties");std::uint32_t count{};
        require(enumerateDevices(instance,&count,nullptr)==VK_SUCCESS,"Native device count failed");std::vector<VkPhysicalDevice> devices(count);
        require(enumerateDevices(instance,&count,devices.data())==VK_SUCCESS,"Native device enumeration failed");devices.resize(count);std::vector<Identity> result;
        for(auto device:devices){
            VkPhysicalDeviceIDProperties ids{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};VkPhysicalDeviceProperties2 p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};p.pNext=&ids;properties(device,&p);
            std::uint32_t n{};queues(device,&n,nullptr);std::vector<VkQueueFamilyProperties> q(n);queues(device,&n,q.data());q.resize(n);
            if(p.properties.apiVersion<VK_API_VERSION_1_1||std::none_of(q.begin(),q.end(),[](auto& f){return f.queueCount&&(f.queueFlags&VK_QUEUE_GRAPHICS_BIT);}))continue;
            std::uint64_t id=14695981039346656037ull;for(auto byte:ids.deviceUUID)id=(id^byte)*1099511628211ull;
            require(id&&std::any_of(std::begin(ids.deviceUUID),std::end(ids.deviceUUID),[](auto b){return b!=0;}),"Nonzero UUID required");
            require(std::any_of(std::begin(ids.driverUUID),std::end(ids.driverUUID),[](auto b){return b!=0;}),"Nonzero driver UUID required");
            result.push_back({p.properties.deviceName,hex(ids.deviceUUID),hex(ids.driverUUID),id,p.properties.vendorID,p.properties.deviceID,p.properties.driverVersion,p.properties.apiVersion,p.properties.deviceType});
        }
        require(!result.empty(),"No supported native device");return result;
    }
};
inline Identity select(const std::vector<Identity>& devices,std::string_view uuid,std::string_view mode){
    require(uuid.size()==32&&std::all_of(uuid.begin(),uuid.end(),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');}),"Exact lowercase UUID required");
    require(std::count_if(devices.begin(),devices.end(),[&](auto& d){return d.uuid==uuid;})==1,"Exact UUID absent or ambiguous");
    const auto found=*std::find_if(devices.begin(),devices.end(),[&](auto& d){return d.uuid==uuid;});
    require(std::count_if(devices.begin(),devices.end(),[&](auto& d){return d.name==found.name;})==1,"Ambiguous public GPU name rejected");
    require((mode=="physical"&&(found.type==1||found.type==2))||(mode=="software-diagnostic"&&found.type==4),"Requested probe kind differs from native device type");return found;
}
inline std::set<std::string> modules(){
    std::ifstream maps("/proc/self/maps");require(bool(maps),"Process maps unavailable");std::set<std::string> result;std::string line;
    while(std::getline(maps,line)){auto start=line.find('/');if(start==std::string::npos)continue;auto path=line.substr(start);if(path.find(".so")==std::string::npos)continue;std::error_code error;auto p=std::filesystem::canonical(path,error);if(!error)result.insert(p.string());}return result;
}
inline std::string environment(){
    struct utsname os{};struct sysinfo memory{};require(uname(&os)==0&&sysinfo(&memory)==0,"Linux environment query failed");
    std::ifstream cpu("/proc/cpuinfo");std::string line,model;
    while(std::getline(cpu,line)){if(line.starts_with("model name")||line.starts_with("Hardware")){auto colon=line.find(':');if(colon!=std::string::npos){model=line.substr(colon+1);break;}}}
    require(!model.empty(),"CPU model unavailable");std::ostringstream out;
    out<<"{\"platform\":\"Linux\",\"kernel\":"<<quote(os.release)<<",\"machine\":"<<quote(os.machine)<<",\"cpu\":"<<quote(model)
        <<",\"ram_bytes\":"<<std::uint64_t(memory.totalram)*memory.mem_unit<<",\"compiler\":"<<quote("GCC " __VERSION__)<<",\"pointer_bits\":"<<sizeof(void*)*8<<'}';return out.str();
}
inline std::string inventory(const std::vector<Identity>& devices){std::string out="[";for(auto& d:devices){if(out.size()>1)out+=',';out+=d.json();}return out+']';}
}
