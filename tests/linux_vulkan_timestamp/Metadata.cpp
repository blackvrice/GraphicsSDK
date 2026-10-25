#define VK_NO_PROTOTYPES
#include <graphics/Graphics.hpp>
#include <vulkan/vulkan_core.h>
#include <dlfcn.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
unsigned assertions{};
void check(bool value,const char* message) {++assertions;if(!value) throw std::runtime_error(message);}
std::string quote(std::string_view s) {
    std::string out="\"";constexpr char hex[]="0123456789abcdef";
    for(unsigned char c:s) {
        if(c=='"'||c=='\\') {out+='\\';out+=static_cast<char>(c);}
        else if(c<32) {out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
        else out+=static_cast<char>(c);
    }
    return out+'"';
}
struct Native {
    void* library{};PFN_vkGetInstanceProcAddr get{};VkInstance instance{};PFN_vkDestroyInstance destroy{};
    ~Native() {if(instance&&destroy) destroy(instance,nullptr);if(library) dlclose(library);}
    template<class T> T load(const char* name) {auto p=reinterpret_cast<T>(get(instance,name));check(p!=nullptr,name);return p;}
    Native() {
        library=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL);check(library!=nullptr,"Native Vulkan loader");
        get=reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library,"vkGetInstanceProcAddr"));check(get!=nullptr,"Native loader entry");
        auto create=load<PFN_vkCreateInstance>("vkCreateInstance");
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_1;
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};info.pApplicationInfo=&app;
        check(create(&info,nullptr,&instance)==VK_SUCCESS,"Native instance");destroy=load<PFN_vkDestroyInstance>("vkDestroyInstance");
    }
};
}
int main(int argc,char**) try {
    check(argc==1,"Usage: gfx_linux_timestamp_metadata");
    Native native;
    auto enumerate=native.load<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices");
    auto properties=native.load<PFN_vkGetPhysicalDeviceProperties2>("vkGetPhysicalDeviceProperties2");
    auto queues=native.load<PFN_vkGetPhysicalDeviceQueueFamilyProperties>("vkGetPhysicalDeviceQueueFamilyProperties");
    std::uint32_t count{};check(enumerate(native.instance,&count,nullptr)==VK_SUCCESS && count==1,"One explicitly selected software device");
    VkPhysicalDevice physical{};check(enumerate(native.instance,&count,&physical)==VK_SUCCESS && count==1,"Native enumeration");
    VkPhysicalDeviceIDProperties ids{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};p.pNext=&ids;properties(physical,&p);
    std::uint64_t id=14695981039346656037ull;for(auto byte:ids.deviceUUID) id=(id^byte)*1099511628211ull;
    check(id!=0 && p.properties.deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU,"Observed device is native CPU/software kind");
    std::uint32_t familyCount{};queues(physical,&familyCount,nullptr);std::vector<VkQueueFamilyProperties> families(familyCount);queues(physical,&familyCount,families.data());families.resize(familyCount);
    auto first=std::find_if(families.begin(),families.end(),[](auto& q){return q.queueCount && (q.queueFlags&VK_QUEUE_GRAPHICS_BIT);});
    check(first!=families.end(),"First nonempty graphics queue matches SDK selection policy");
    const auto bits=first->timestampValidBits;const double period=p.properties.limits.timestampPeriod;
    check(bits>=36 && bits<=64 && std::isfinite(period) && period>0,"Native queue timestamp metadata supported");
    auto adapters=gfx::enumerateAdapters(gfx::Backend::Vulkan);check(adapters && adapters->size()==1,"One SDK software adapter");
    const auto& a=adapters->front();check(a.id.value==id && a.softwareAdapter && a.name==p.properties.deviceName,"SDK and native exact device agree");
    gfx::DeviceDesc desc;desc.backend=gfx::Backend::Vulkan;desc.adapter=a.id;desc.softwareAdapter=true;
    auto device=gfx::createDevice(desc);check(bool(device),"Installed SDK device created");check((*device)->caps().timestampQueries,"SDK timestamp capability agrees with metadata");
    std::cout<<"{\"schema_version\":1,\"SDK_source_fingerprint\":"<<quote(gfx::buildFingerprint())<<",\"assertions\":"<<assertions
        <<",\"adapter_id\":"<<quote(std::to_string(id))<<",\"adapter_name\":"<<quote(a.name)<<",\"device_type\":"<<p.properties.deviceType
        <<",\"queue_family_index\":"<<std::distance(families.begin(),first)<<",\"queue_flags\":"<<first->queueFlags<<",\"queue_count\":"<<first->queueCount
        <<",\"timestamp_valid_bits\":"<<bits<<",\"timestamp_period_nanoseconds\":"<<std::setprecision(17)<<period
        <<",\"SDK_timestamp_queries\":true,\"raw_ticks_exposed\":false,\"query_execution\":false,\"hardware_GPU_accepted\":false}\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
