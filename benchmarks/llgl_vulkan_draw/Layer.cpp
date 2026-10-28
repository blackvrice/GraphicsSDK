// Own observer forwards the real Vulkan calls; it performs no rendering itself.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include "Observer.hpp"
#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>
#define EXPORT extern "C" __declspec(dllexport)
namespace {
void* key(const void* h){return *static_cast<void*const*>(h);}
struct Instance{VkInstance h;PFN_vkGetInstanceProcAddr get;PFN_GetPhysicalDeviceProcAddr physical;};
struct Device{VkDevice h;PFN_vkGetDeviceProcAddr get;};
std::mutex mutex;std::map<void*,Instance> instances;std::map<void*,Device> devices;drawobserver::Snapshot totals;
Instance instance(const void* h){std::lock_guard lock(mutex);return instances.at(key(h));}
Device device(const void* h){std::lock_guard lock(mutex);return devices.at(key(h));}
VKAPI_ATTR void VKAPI_CALL destroyInstance(VkInstance h,const VkAllocationCallbacks* a){auto p=instance(h);auto k=key(h);reinterpret_cast<PFN_vkDestroyInstance>(p.get(h,"vkDestroyInstance"))(h,a);std::lock_guard lock(mutex);instances.erase(k);}
VKAPI_ATTR void VKAPI_CALL destroyDevice(VkDevice h,const VkAllocationCallbacks* a){auto p=device(h);auto k=key(h);reinterpret_cast<PFN_vkDestroyDevice>(p.get(h,"vkDestroyDevice"))(h,a);std::lock_guard lock(mutex);devices.erase(k);}
VKAPI_ATTR VkResult VKAPI_CALL createInstance(const VkInstanceCreateInfo* info,const VkAllocationCallbacks* a,VkInstance* out){
    auto link=reinterpret_cast<const VkLayerInstanceCreateInfo*>(info->pNext);while(link&&(link->sType!=VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO||link->function!=VK_LAYER_LINK_INFO))link=reinterpret_cast<const VkLayerInstanceCreateInfo*>(link->pNext);
    if(!link||!link->u.pLayerInfo)return VK_ERROR_INITIALIZATION_FAILED;auto get=link->u.pLayerInfo->pfnNextGetInstanceProcAddr;auto physical=link->u.pLayerInfo->pfnNextGetPhysicalDeviceProcAddr;
    const_cast<VkLayerInstanceCreateInfo*>(link)->u.pLayerInfo=link->u.pLayerInfo->pNext;auto result=reinterpret_cast<PFN_vkCreateInstance>(get(nullptr,"vkCreateInstance"))(info,a,out);
    if(result==VK_SUCCESS){std::lock_guard lock(mutex);instances.emplace(key(*out),Instance{*out,get,physical});++totals.createdInstances;totals.instanceApi=info->pApplicationInfo?info->pApplicationInfo->apiVersion:0;}return result;
}
VKAPI_ATTR VkResult VKAPI_CALL createDevice(VkPhysicalDevice h,const VkDeviceCreateInfo* info,const VkAllocationCallbacks* a,VkDevice* out){
    auto link=reinterpret_cast<const VkLayerDeviceCreateInfo*>(info->pNext);while(link&&(link->sType!=VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO||link->function!=VK_LAYER_LINK_INFO))link=reinterpret_cast<const VkLayerDeviceCreateInfo*>(link->pNext);
    if(!link||!link->u.pLayerInfo)return VK_ERROR_INITIALIZATION_FAILED;auto p=instance(h);auto next=link->u.pLayerInfo->pfnNextGetInstanceProcAddr;auto get=link->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    const_cast<VkLayerDeviceCreateInfo*>(link)->u.pLayerInfo=link->u.pLayerInfo->pNext;auto result=reinterpret_cast<PFN_vkCreateDevice>(next(p.h,"vkCreateDevice"))(h,info,a,out);
    if(result==VK_SUCCESS){std::lock_guard lock(mutex);devices.emplace(key(*out),Device{*out,get});++totals.createdDevices;}return result;
}
VKAPI_ATTR void VKAPI_CALL draw(VkCommandBuffer command,uint32_t vertices,uint32_t count,uint32_t first,uint32_t instanceIndex){
    auto p=device(command);auto next=reinterpret_cast<PFN_vkCmdDraw>(p.get(p.h,"vkCmdDraw"));
    {std::lock_guard lock(mutex);++totals.calls;totals.instances+=count;totals.vertices+=std::uint64_t(vertices)*count;totals.badArguments+=(vertices!=3||first!=0||instanceIndex!=0);if(totals.calls==1)totals.minInstances=count;else totals.minInstances=std::min(totals.minInstances,count);totals.maxInstances=std::max(totals.maxInstances,count);}
    next(command,vertices,count,first,instanceIndex);{std::lock_guard lock(mutex);++totals.forwarded;}
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getInstance(VkInstance,const char*);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getDevice(VkDevice,const char*);
PFN_vkVoidFunction hook(const char* name){
#define H(n,f) if(!std::strcmp(name,"vk" #n))return reinterpret_cast<PFN_vkVoidFunction>(f);
    H(GetInstanceProcAddr,getInstance) H(GetDeviceProcAddr,getDevice) H(CreateInstance,createInstance) H(DestroyInstance,destroyInstance) H(CreateDevice,createDevice) H(DestroyDevice,destroyDevice) H(CmdDraw,draw)
#undef H
    return nullptr;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getInstance(VkInstance h,const char* name){if(auto p=hook(name))return p;return h?instance(h).get(h,name):nullptr;}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getDevice(VkDevice h,const char* name){if(auto p=hook(name))return p;if(!h)return nullptr;auto p=device(h);return p.get(h,name);}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getPhysical(VkInstance h,const char* name){if(!h)return nullptr;auto p=instance(h);return p.physical?p.physical(h,name):p.get(h,name);}
}
EXPORT VKAPI_ATTR VkResult VKAPI_CALL gfxDrawNegotiate(VkNegotiateLayerInterface* value){if(!value||value->sType!=LAYER_NEGOTIATE_INTERFACE_STRUCT||value->loaderLayerInterfaceVersion<2)return VK_ERROR_INITIALIZATION_FAILED;value->loaderLayerInterfaceVersion=2;value->pfnGetInstanceProcAddr=getInstance;value->pfnGetDeviceProcAddr=getDevice;value->pfnGetPhysicalDeviceProcAddr=getPhysical;return VK_SUCCESS;}
EXPORT int gfxDrawSnapshot(drawobserver::Snapshot* out,unsigned size){if(!out||size!=sizeof(*out))return 0;std::lock_guard lock(mutex);*out=totals;out->liveInstances=unsigned(instances.size());out->liveDevices=unsigned(devices.size());return 1;}
EXPORT void gfxDrawReset(){std::lock_guard lock(mutex);totals.calls=totals.instances=totals.vertices=totals.forwarded=totals.badArguments=0;totals.minInstances=totals.maxInstances=0;}
