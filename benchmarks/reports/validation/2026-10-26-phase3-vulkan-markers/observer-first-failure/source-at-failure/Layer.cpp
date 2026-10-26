// Own process-local observer: forward real Vulkan commands, with optional lookup denial.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include "Observer.hpp"
#include <cstring>
#include <map>
#include <mutex>
#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C" __attribute__((visibility("default")))
#endif
namespace {
void* key(const void* h) { return *static_cast<void* const*>(h); }
struct Instance { VkInstance handle; PFN_vkGetInstanceProcAddr get; PFN_GetPhysicalDeviceProcAddr physical; };
struct Device { VkDevice handle; PFN_vkGetDeviceProcAddr get; };
std::map<void*,Instance> instances;
std::map<void*,Device> devices;
std::mutex mutex;
markerprobe::Snapshot totals;
bool deny{};
Instance instance(VkInstance i) { std::lock_guard lock(mutex); return instances.at(key(i)); }
VKAPI_ATTR void VKAPI_CALL destroyInstance(VkInstance i,const VkAllocationCallbacks* a) {
    auto p=instance(i);auto k=key(i);
    reinterpret_cast<PFN_vkDestroyInstance>(p.get(i,"vkDestroyInstance"))(i,a);
    std::lock_guard lock(mutex);instances.erase(k);
}
VKAPI_ATTR void VKAPI_CALL destroyDevice(VkDevice d,const VkAllocationCallbacks* a) {
    Device p;auto k=key(d);{std::lock_guard lock(mutex);p=devices.at(k);}
    reinterpret_cast<PFN_vkDestroyDevice>(p.get(d,"vkDestroyDevice"))(d,a);
    std::lock_guard lock(mutex);devices.erase(k);
}
VKAPI_ATTR VkResult VKAPI_CALL createInstance(const VkInstanceCreateInfo* info,const VkAllocationCallbacks* a,VkInstance* out) {
    auto link=reinterpret_cast<const VkLayerInstanceCreateInfo*>(info->pNext);
    while(link&&(link->sType!=VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO||link->function!=VK_LAYER_LINK_INFO))link=reinterpret_cast<const VkLayerInstanceCreateInfo*>(link->pNext);
    if(!link||!link->u.pLayerInfo)return VK_ERROR_INITIALIZATION_FAILED;
    auto next=link->u.pLayerInfo->pfnNextGetInstanceProcAddr;auto physical=link->u.pLayerInfo->pfnNextGetPhysicalDeviceProcAddr;
    const_cast<VkLayerInstanceCreateInfo*>(link)->u.pLayerInfo=link->u.pLayerInfo->pNext;
    auto result=reinterpret_cast<PFN_vkCreateInstance>(next(nullptr,"vkCreateInstance"))(info,a,out);
    if(result==VK_SUCCESS){
        unsigned enabled{};for(unsigned i=0;i<info->enabledExtensionCount;++i)enabled+=!std::strcmp(info->ppEnabledExtensionNames[i],VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        std::lock_guard lock(mutex);instances.emplace(key(*out),Instance{*out,next,physical});
        totals.debugInstances+=enabled!=0;totals.duplicateExtensions+=enabled>1;
    }return result;
}
VKAPI_ATTR VkResult VKAPI_CALL createDevice(VkPhysicalDevice physical,const VkDeviceCreateInfo* info,const VkAllocationCallbacks* a,VkDevice* out) {
    auto link=reinterpret_cast<const VkLayerDeviceCreateInfo*>(info->pNext);
    while(link&&(link->sType!=VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO||link->function!=VK_LAYER_LINK_INFO))link=reinterpret_cast<const VkLayerDeviceCreateInfo*>(link->pNext);
    if(!link||!link->u.pLayerInfo)return VK_ERROR_INITIALIZATION_FAILED;
    auto next=link->u.pLayerInfo->pfnNextGetInstanceProcAddr;auto get=link->u.pLayerInfo->pfnNextGetDeviceProcAddr;Instance p;
    {std::lock_guard lock(mutex);p=instances.at(key(physical));}
    const_cast<VkLayerDeviceCreateInfo*>(link)->u.pLayerInfo=link->u.pLayerInfo->pNext;
    auto result=reinterpret_cast<PFN_vkCreateDevice>(next(p.handle,"vkCreateDevice"))(physical,info,a,out);
    if(result==VK_SUCCESS){std::lock_guard lock(mutex);devices.emplace(key(*out),Device{*out,get});}return result;
}
VKAPI_ATTR void VKAPI_CALL insert(VkCommandBuffer command,const VkDebugUtilsLabelEXT* label) {
    Device p;
    {std::lock_guard lock(mutex);p=devices.at(key(command));const auto index=totals.calls++;
        const bool valid=label&&label->sType==VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT&&!label->pNext&&label->pLabelName;
        if(!valid||index>=32){++totals.errors;}
        else {auto& row=totals.labels[index];row.command=reinterpret_cast<std::uintptr_t>(command);
            const auto length=std::strlen(label->pLabelName);if(length>=sizeof(row.text))++totals.errors;
            else std::memcpy(row.text,label->pLabelName,length+1);
            for(float color:label->color)if(color!=0)++totals.errors;
        }
    }
    auto next=reinterpret_cast<PFN_vkCmdInsertDebugUtilsLabelEXT>(p.get(p.handle,"vkCmdInsertDebugUtilsLabelEXT"));
    if(next){next(command,label);std::lock_guard lock(mutex);++totals.forwarded;}
    else {std::lock_guard lock(mutex);++totals.errors;}
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getInstance(VkInstance,const char*);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getDevice(VkDevice,const char*);
PFN_vkVoidFunction hook(const char* name) {
#define H(n,f) if(!std::strcmp(name,"vk" #n))return reinterpret_cast<PFN_vkVoidFunction>(f);
    H(GetInstanceProcAddr,getInstance) H(GetDeviceProcAddr,getDevice) H(CreateInstance,createInstance) H(DestroyInstance,destroyInstance) H(CreateDevice,createDevice) H(DestroyDevice,destroyDevice)
#undef H
    return nullptr;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getInstance(VkInstance i,const char* name) {
    if(auto p=hook(name))return p;
    if(!i)return nullptr;return instance(i).get(i,name);
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getDevice(VkDevice d,const char* name) {
    if(!std::strcmp(name,"vkGetDeviceProcAddr"))return reinterpret_cast<PFN_vkVoidFunction>(getDevice);
    if(!std::strcmp(name,"vkDestroyDevice"))return reinterpret_cast<PFN_vkVoidFunction>(destroyDevice);
    if(!d)return nullptr;Device p;{std::lock_guard lock(mutex);p=devices.at(key(d));}
    if(!std::strcmp(name,"vkCmdInsertDebugUtilsLabelEXT")){
        std::lock_guard lock(mutex);++totals.lookups;
        return deny?nullptr:reinterpret_cast<PFN_vkVoidFunction>(insert);
    }return p.get(d,name);
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getPhysical(VkInstance i,const char* name) {
    if(!i)return nullptr;auto p=instance(i);return p.physical?p.physical(i,name):p.get(i,name);
}
}
EXPORT VKAPI_ATTR VkResult VKAPI_CALL gfxMarkerNegotiate(VkNegotiateLayerInterface* value) {
    if(!value||value->sType!=LAYER_NEGOTIATE_INTERFACE_STRUCT||value->loaderLayerInterfaceVersion<2)return VK_ERROR_INITIALIZATION_FAILED;
    value->loaderLayerInterfaceVersion=2;value->pfnGetInstanceProcAddr=getInstance;value->pfnGetDeviceProcAddr=getDevice;value->pfnGetPhysicalDeviceProcAddr=getPhysical;return VK_SUCCESS;
}
EXPORT int gfxMarkerSnapshot(markerprobe::Snapshot* out,unsigned size) {
    if(!out||size!=sizeof(*out))return 0;std::lock_guard lock(mutex);*out=totals;
    out->liveInstances=static_cast<unsigned>(instances.size());out->liveDevices=static_cast<unsigned>(devices.size());return 1;
}
EXPORT void gfxMarkerConfigure(unsigned disabled) { std::lock_guard lock(mutex);deny=disabled!=0; }
