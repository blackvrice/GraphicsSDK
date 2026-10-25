#define VK_NO_PROTOTYPES
#include <graphics/Graphics.hpp>
#include <vulkan/vulkan_core.h>
#include <dlfcn.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
unsigned assertions{}, creations{};
void check(bool value, const char* text) { ++assertions; if (!value) throw std::runtime_error(text); }
std::string quote(std::string_view value) {
    std::string out="\"";
    constexpr char hex[]="0123456789abcdef";
    for (unsigned char c:value) {
        if(c=='"'||c=='\\') {out+='\\';out+=static_cast<char>(c);}
        else if(c<32) {out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
        else out+=static_cast<char>(c);
    }
    return out+'"';
}
std::string hexBytes(const std::uint8_t* bytes, std::size_t size) {
    constexpr char digits[]="0123456789abcdef";std::string out;
    for(std::size_t i=0;i<size;++i) {out+=digits[bytes[i]>>4];out+=digits[bytes[i]&15];}
    return out;
}
const char* code(gfx::ErrorCode c) {
    switch(c) {
        case gfx::ErrorCode::Unsupported:return "Unsupported";
        case gfx::ErrorCode::InvalidArgument:return "InvalidArgument";
        case gfx::ErrorCode::BackendFailure:return "BackendFailure";
        default:return "other";
    }
}
struct Rejection {std::string name;gfx::Error error;};
std::vector<Rejection> rejections;
template<class T> void reject(const gfx::Result<T>& r,gfx::ErrorCode expected,const char* name) {
    check(!r && r.error().code==expected,name);rejections.push_back({name,r.error()});
}
template<class T> T take(gfx::Result<T> r) {
    if(!r) throw std::runtime_error(r.error().message+" native="+std::to_string(r.error().nativeCode));
    return std::move(*r);
}
struct NativeInfo {
    VkPhysicalDeviceProperties properties{};
    std::array<std::uint8_t,VK_UUID_SIZE> uuid{},driverUuid{};
    std::uint64_t id{};
};
struct NativeQuery {
    void* library{};PFN_vkGetInstanceProcAddr get{};VkInstance instance{};PFN_vkDestroyInstance destroy{};
    std::string loaderPath;
    ~NativeQuery() {if(instance && destroy) destroy(instance,nullptr);if(library) dlclose(library);}
    template<class T> T load(const char* name) {auto p=reinterpret_cast<T>(get(instance,name));check(p!=nullptr,name);return p;}
    std::vector<NativeInfo> enumerate() {
        library=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL);check(library!=nullptr,"Native loader is available");
        get=reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library,"vkGetInstanceProcAddr"));check(get!=nullptr,"Native loader entry point");
        Dl_info module{};check(dladdr(reinterpret_cast<void*>(get),&module)!=0 && module.dli_fname,"Actual loader path");
        loaderPath=std::filesystem::canonical(module.dli_fname).string();
        auto create=load<PFN_vkCreateInstance>("vkCreateInstance");
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_1;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;
        check(create(&ci,nullptr,&instance)==VK_SUCCESS,"Native instance creation");
        destroy=load<PFN_vkDestroyInstance>("vkDestroyInstance");
        auto enumerateDevices=load<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices");
        auto properties=load<PFN_vkGetPhysicalDeviceProperties2>("vkGetPhysicalDeviceProperties2");
        auto queueProperties=load<PFN_vkGetPhysicalDeviceQueueFamilyProperties>("vkGetPhysicalDeviceQueueFamilyProperties");
        std::uint32_t count{};check(enumerateDevices(instance,&count,nullptr)==VK_SUCCESS,"Native device count");
        std::vector<VkPhysicalDevice> devices(count);check(enumerateDevices(instance,&count,devices.data())==VK_SUCCESS,"Native device enumeration");
        devices.resize(count);std::vector<NativeInfo> result;
        for(auto device:devices) {
            VkPhysicalDeviceIDProperties ids{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
            VkPhysicalDeviceProperties2 p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};p.pNext=&ids;properties(device,&p);
            std::uint32_t queues{};queueProperties(device,&queues,nullptr);std::vector<VkQueueFamilyProperties> q(queues);queueProperties(device,&queues,q.data());q.resize(queues);
            if(p.properties.apiVersion<VK_API_VERSION_1_1 || std::none_of(q.begin(),q.end(),[](auto& f){return f.queueCount && (f.queueFlags&VK_QUEUE_GRAPHICS_BIT);})) continue;
            NativeInfo info;info.properties=p.properties;std::copy_n(ids.deviceUUID,VK_UUID_SIZE,info.uuid.begin());std::copy_n(ids.driverUUID,VK_UUID_SIZE,info.driverUuid.begin());
            info.id=14695981039346656037ull;for(auto byte:info.uuid) info.id=(info.id^byte)*1099511628211ull;
            check(info.id && std::any_of(info.uuid.begin(),info.uuid.end(),[](auto b){return b!=0;}),"Nonzero native identity");
            result.push_back(info);
        }
        return result;
    }
};
std::set<std::string> runtimeModules() {
    std::set<std::string> result;std::ifstream maps("/proc/self/maps");std::string line;
    while(std::getline(maps,line)) {
        auto slash=line.find('/');if(slash==std::string::npos) continue;
        auto path=line.substr(slash);if(path.find("libvulkan.so")==std::string::npos && path.find("libvulkan_lvp.so")==std::string::npos) continue;
        std::error_code error;auto canonical=std::filesystem::canonical(path,error);if(!error) result.insert(canonical.string());
    }
    return result;
}
gfx::DeviceDesc description(const gfx::AdapterInfo& a) {gfx::DeviceDesc d;d.backend=gfx::Backend::Vulkan;d.adapter=a.id;d.softwareAdapter=a.softwareAdapter;return d;}
void errorsJson() {
    std::cout<<",\"rejections\":[";bool first=true;
    for(auto& r:rejections) {if(!first)std::cout<<',';first=false;std::cout<<"{\"case\":"<<quote(r.name)<<",\"code\":"<<quote(code(r.error.code))<<",\"native\":"<<r.error.nativeCode<<'}';}
    std::cout<<']';
}
void selection() {
    NativeQuery native;const auto raw=native.enumerate();const auto adapters=take(gfx::enumerateAdapters(gfx::Backend::Vulkan));
    const auto again=take(gfx::enumerateAdapters(gfx::Backend::Vulkan));check(!adapters.empty() && adapters.size()==raw.size() && again.size()==adapters.size(),"Real candidate sets are nonempty and agree");
    unsigned software{},hardware{};
    for(auto& a:adapters) {
        auto n=std::find_if(raw.begin(),raw.end(),[&](auto& p){return p.id==a.id.value;});check(n!=raw.end(),"SDK ID matches all native UUID bytes");
        check(a.id.backend==gfx::Backend::Vulkan && a.name==n->properties.deviceName && a.vendorId==n->properties.vendorID && a.deviceId==n->properties.deviceID,"SDK/native metadata agree");
        check(a.softwareAdapter==(n->properties.deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU),"Software kind is native CPU device type");
        check(std::any_of(again.begin(),again.end(),[&](auto& b){return a.id==b.id && a.name==b.name && a.softwareAdapter==b.softwareAdapter;}),"Fresh instances preserve exact identity");
        a.softwareAdapter?++software:++hardware;
        auto d=take(gfx::createDevice(description(a)));++creations;
        auto other=take(gfx::createDevice(description(a)));++creations;
        for(auto* p:{d.get(),other.get()}) {
            check(p->caps().adapterId==a.id && p->caps().adapterName==a.name && p->caps().softwareAdapter==a.softwareAdapter,"Two native devices use the exact selected adapter");
            check(bool(p->checkStatus()),"Created device is live");auto s=take(p->resourceStatistics());
            check(!s.uniqueResources && !s.resourceAllocationBytes && !s.pendingSubmissions,"Empty device has no resource ownership");
        }
        auto wrong=description(a);wrong.softwareAdapter=!wrong.softwareAdapter;reject(gfx::createDevice(wrong),gfx::ErrorCode::InvalidArgument,"software-kind-mismatch");
        wrong=description(a);wrong.adapter->backend=gfx::Backend::D3D12;reject(gfx::createDevice(wrong),gfx::ErrorCode::InvalidArgument,"backend-id-mismatch");
    }
    auto missing=description(adapters.front());
    do {++missing.adapter->value;} while(std::any_of(adapters.begin(),adapters.end(),[&](auto& a){return a.id==*missing.adapter;}));
    reject(gfx::createDevice(missing),gfx::ErrorCode::Unsupported,"missing-exact-id-no-fallback");
    missing.adapter->value=0;reject(gfx::createDevice(missing),gfx::ErrorCode::Unsupported,"zero-id-no-fallback");
    if(software) {gfx::DeviceDesc d;d.backend=gfx::Backend::Vulkan;d.softwareAdapter=true;auto p=take(gfx::createDevice(d));++creations;check(p->caps().softwareAdapter,"Explicit software default selection");}
    if(!hardware) {gfx::DeviceDesc d;d.backend=gfx::Backend::Vulkan;reject(gfx::createDevice(d),gfx::ErrorCode::Unsupported,"hardware-default-never-falls-back-to-software");}
    reject(gfx::enumerateAdapters(gfx::Backend::Metal),gfx::ErrorCode::Unsupported,"uncompiled-backend-enumeration");
    auto loss=description(adapters.front());loss.enableDeviceLossTesting=true;reject(gfx::createDevice(loss),gfx::ErrorCode::Unsupported,"unsupported-loss-opt-in");
    std::cout<<"{\"schema_version\":1,\"case\":\"selection\",\"SDK_source_fingerprint\":"<<quote(gfx::buildFingerprint())<<",\"assertions\":"<<assertions<<",\"device_creations\":"<<creations
        <<",\"software_adapters\":"<<software<<",\"hardware_adapters\":"<<hardware<<",\"loader_path\":"<<quote(native.loaderPath)<<",\"adapters\":[";
    bool first=true;for(auto& n:raw) {if(!first)std::cout<<',';first=false;std::cout<<"{\"name\":"<<quote(n.properties.deviceName)<<",\"id\":"<<quote(std::to_string(n.id))<<",\"vendor_id\":"<<n.properties.vendorID<<",\"device_id\":"<<n.properties.deviceID
        <<",\"device_type\":"<<n.properties.deviceType<<",\"api_version_raw\":"<<n.properties.apiVersion<<",\"driver_version_raw\":"<<n.properties.driverVersion<<",\"device_uuid\":"<<quote(hexBytes(n.uuid.data(),n.uuid.size()))<<",\"driver_uuid\":"<<quote(hexBytes(n.driverUuid.data(),n.driverUuid.size()))<<'}';}
    std::cout<<"],\"runtime_modules\":[";first=true;for(auto& path:runtimeModules()){if(!first)std::cout<<',';first=false;std::cout<<quote(path);}std::cout<<']';errorsJson();std::cout<<"}\n";
}
void unavailable(std::string_view mode) {
    gfx::DeviceDesc d;d.backend=gfx::Backend::Vulkan;d.softwareAdapter=true;
    if(mode=="missing-layer") {
        d.validation=true;auto r=gfx::createDevice(d);reject(r,gfx::ErrorCode::Unsupported,"required-layer-missing");
        check(r.error().message.find("VK_LAYER_KHRONOS_validation is not installed")!=std::string::npos,"Missing layer fails explicitly");
    } else {
        const auto expected=mode=="no-driver"?gfx::ErrorCode::BackendFailure:gfx::ErrorCode::Unsupported;
        auto a=gfx::enumerateAdapters(gfx::Backend::Vulkan);auto device=gfx::createDevice(d);
        reject(a,expected,"enumeration-unavailable");reject(device,expected,"creation-unavailable");
        if(mode=="no-driver") check(a.error().nativeCode==VK_ERROR_INCOMPATIBLE_DRIVER && device.error().nativeCode==VK_ERROR_INCOMPATIBLE_DRIVER,"No ICD preserves native incompatible-driver failure");
        else check(a.error().message.find("loader unavailable")!=std::string::npos && device.error().message.find("loader unavailable")!=std::string::npos,"Unusable loader fails without fallback");
    }
    std::cout<<"{\"schema_version\":1,\"case\":"<<quote(mode)<<",\"SDK_source_fingerprint\":"<<quote(gfx::buildFingerprint())<<",\"assertions\":"<<assertions<<",\"device_creations\":0";errorsJson();std::cout<<"}\n";
}
}
int main(int argc,char** argv) try {
    if(argc!=2 || (std::string_view(argv[1])!="selection" && std::string_view(argv[1])!="no-driver" && std::string_view(argv[1])!="no-loader" && std::string_view(argv[1])!="missing-layer")) throw std::runtime_error("Usage: gfx_linux_vulkan_device selection|no-driver|no-loader|missing-layer");
    check(gfx::backendCompiled(gfx::Backend::Vulkan) && !gfx::backendCompiled(gfx::Backend::D3D12),"Real Linux Vulkan-only build");
    if(std::string_view(argv[1])=="selection") selection();else unavailable(argv[1]);return 0;
} catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
