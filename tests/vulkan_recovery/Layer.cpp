// Own explicit test layer: synthetic VkResult controls; never lose a real device.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include "Control.hpp"
#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>
#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C" __attribute__((visibility("default")))
#endif
namespace {
using namespace recoveryprobe;
void* key(const void* h){return *static_cast<void*const*>(h);}
struct I {VkInstance handle{};PFN_vkGetInstanceProcAddr get{};PFN_GetPhysicalDeviceProcAddr physical{};};
struct D {VkDevice handle{};PFN_vkGetDeviceProcAddr get{};std::uint64_t serial{};};
std::map<void*,I> instances;std::map<void*,D> devices;std::mutex mutex;Snapshot totals;
bool reverseEnumeration{};
I instance(VkInstance i){std::lock_guard lock(mutex);return instances.at(key(i));}
D device(const void* d){std::lock_guard lock(mutex);return devices.at(key(d));}
template<class T>T next(const D& d,const char* name){return reinterpret_cast<T>(d.get(d.handle,name));}
void work(){std::lock_guard lock(mutex);++totals.workCalls;}
bool consume(unsigned mode){std::lock_guard lock(mutex);if(totals.armed!=mode)return false;totals.armed=0;++totals.injected[mode];return true;}
VkResult loss(const D& d){
    // The real device stays healthy. Finish its real pending work before pretending
    // that VK_ERROR_DEVICE_LOST made the device's resources safe to destroy.
    const auto r=next<PFN_vkDeviceWaitIdle>(d,"vkDeviceWaitIdle")(d.handle);
    std::lock_guard lock(mutex);++totals.drains;if(r!=VK_SUCCESS)++totals.errors;
    totals.lastLossSerial=d.serial;return VK_ERROR_DEVICE_LOST;
}
VKAPI_ATTR VkResult VKAPI_CALL queueSubmit(VkQueue q,uint32_t n,const VkSubmitInfo* info,VkFence fence){
    auto d=device(q);work();if(consume(SubmitLoss))return loss(d);
    return next<PFN_vkQueueSubmit>(d,"vkQueueSubmit")(q,n,info,fence);
}
VKAPI_ATTR VkResult VKAPI_CALL fenceStatus(VkDevice handle,VkFence fence){
    auto d=device(handle);work();if(consume(PollLoss))return loss(d);
    return next<PFN_vkGetFenceStatus>(d,"vkGetFenceStatus")(handle,fence);
}
VKAPI_ATTR VkResult VKAPI_CALL createFence(VkDevice handle,const VkFenceCreateInfo* info,const VkAllocationCallbacks* a,VkFence* out){
    auto d=device(handle);work();if(consume(FenceLoss)){*out=VK_NULL_HANDLE;return loss(d);}
    return next<PFN_vkCreateFence>(d,"vkCreateFence")(handle,info,a,out);
}
VKAPI_ATTR VkResult VKAPI_CALL createBuffer(VkDevice h,const VkBufferCreateInfo* i,const VkAllocationCallbacks* a,VkBuffer* out){auto d=device(h);work();return next<PFN_vkCreateBuffer>(d,"vkCreateBuffer")(h,i,a,out);}
VKAPI_ATTR VkResult VKAPI_CALL mapMemory(VkDevice h,VkDeviceMemory m,VkDeviceSize offset,VkDeviceSize size,VkMemoryMapFlags flags,void** out){auto d=device(h);work();return next<PFN_vkMapMemory>(d,"vkMapMemory")(h,m,offset,size,flags,out);}
VKAPI_ATTR VkResult VKAPI_CALL begin(VkCommandBuffer c,const VkCommandBufferBeginInfo* i){auto d=device(c);work();return next<PFN_vkBeginCommandBuffer>(d,"vkBeginCommandBuffer")(c,i);}
VKAPI_ATTR VkResult VKAPI_CALL queueIdle(VkQueue q){auto d=device(q);work();return next<PFN_vkQueueWaitIdle>(d,"vkQueueWaitIdle")(q);}
VKAPI_ATTR VkResult VKAPI_CALL deviceIdle(VkDevice h){auto d=device(h);{std::lock_guard lock(mutex);++totals.idleCalls;}return next<PFN_vkDeviceWaitIdle>(d,"vkDeviceWaitIdle")(h);}
VKAPI_ATTR void VKAPI_CALL destroyInstance(VkInstance i,const VkAllocationCallbacks* a){auto p=instance(i);auto k=key(i);reinterpret_cast<PFN_vkDestroyInstance>(p.get(i,"vkDestroyInstance"))(i,a);std::lock_guard lock(mutex);instances.erase(k);}
VKAPI_ATTR void VKAPI_CALL destroyDevice(VkDevice h,const VkAllocationCallbacks* a){auto d=device(h);auto k=key(h);next<PFN_vkDestroyDevice>(d,"vkDestroyDevice")(h,a);std::lock_guard lock(mutex);devices.erase(k);++totals.destroyed;}
VKAPI_ATTR VkResult VKAPI_CALL enumerate(VkInstance i,uint32_t* count,VkPhysicalDevice* out){
    auto p=instance(i);auto r=reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(p.get(i,"vkEnumeratePhysicalDevices"))(i,count,out);
    std::lock_guard lock(mutex);if(reverseEnumeration&&r==VK_SUCCESS&&out&&*count>1){std::reverse(out,out+*count);++totals.reversedEnumerations;}return r;
}
VKAPI_ATTR VkResult VKAPI_CALL createInstance(const VkInstanceCreateInfo* info,const VkAllocationCallbacks* a,VkInstance* out){
    auto link=reinterpret_cast<const VkLayerInstanceCreateInfo*>(info->pNext);
    while(link&&(link->sType!=VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO||link->function!=VK_LAYER_LINK_INFO))link=reinterpret_cast<const VkLayerInstanceCreateInfo*>(link->pNext);
    if(!link||!link->u.pLayerInfo)return VK_ERROR_INITIALIZATION_FAILED;
    auto get=link->u.pLayerInfo->pfnNextGetInstanceProcAddr;auto physical=link->u.pLayerInfo->pfnNextGetPhysicalDeviceProcAddr;
    const_cast<VkLayerInstanceCreateInfo*>(link)->u.pLayerInfo=link->u.pLayerInfo->pNext;
    auto r=reinterpret_cast<PFN_vkCreateInstance>(get(nullptr,"vkCreateInstance"))(info,a,out);
    if(r==VK_SUCCESS){std::lock_guard lock(mutex);instances.emplace(key(*out),I{*out,get,physical});}return r;
}
VKAPI_ATTR VkResult VKAPI_CALL createDevice(VkPhysicalDevice physical,const VkDeviceCreateInfo* info,const VkAllocationCallbacks* a,VkDevice* out){
    auto link=reinterpret_cast<const VkLayerDeviceCreateInfo*>(info->pNext);
    while(link&&(link->sType!=VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO||link->function!=VK_LAYER_LINK_INFO))link=reinterpret_cast<const VkLayerDeviceCreateInfo*>(link->pNext);
    if(!link||!link->u.pLayerInfo)return VK_ERROR_INITIALIZATION_FAILED;
    auto get=link->u.pLayerInfo->pfnNextGetInstanceProcAddr;auto gdpa=link->u.pLayerInfo->pfnNextGetDeviceProcAddr;I parent;
    {std::lock_guard lock(mutex);parent=instances.at(key(physical));++totals.createAttempts;
        for(auto& [k,d]:devices){(void)k;if(d.serial==totals.lastLossSerial)++totals.prematureCreations;}}
    const_cast<VkLayerDeviceCreateInfo*>(link)->u.pLayerInfo=link->u.pLayerInfo->pNext;
    const bool oom=consume(CreateOOM),lost=consume(CreateLoss);
    if(oom||lost){*out=VK_NULL_HANDLE;std::lock_guard lock(mutex);++totals.failedCreates;return oom?VK_ERROR_OUT_OF_DEVICE_MEMORY:VK_ERROR_DEVICE_LOST;}
    auto r=reinterpret_cast<PFN_vkCreateDevice>(get(parent.handle,"vkCreateDevice"))(physical,info,a,out);
    if(r==VK_SUCCESS){std::lock_guard lock(mutex);const auto serial=++totals.created;devices.emplace(key(*out),D{*out,gdpa,serial});}return r;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getInstance(VkInstance,const char*);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getDevice(VkDevice,const char*);
PFN_vkVoidFunction deviceHook(const char* name){
#define H(n,f) if(!std::strcmp(name,"vk" #n))return reinterpret_cast<PFN_vkVoidFunction>(f);
    H(GetDeviceProcAddr,getDevice) H(DestroyDevice,destroyDevice) H(QueueSubmit,queueSubmit) H(GetFenceStatus,fenceStatus) H(CreateFence,createFence)
    H(CreateBuffer,createBuffer) H(MapMemory,mapMemory) H(BeginCommandBuffer,begin) H(QueueWaitIdle,queueIdle) H(DeviceWaitIdle,deviceIdle)
#undef H
    return nullptr;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getInstance(VkInstance i,const char* name){
    if(auto p=deviceHook(name))return p;
#define H(n,f) if(!std::strcmp(name,"vk" #n))return reinterpret_cast<PFN_vkVoidFunction>(f);
    H(GetInstanceProcAddr,getInstance) H(CreateInstance,createInstance) H(DestroyInstance,destroyInstance) H(CreateDevice,createDevice) H(EnumeratePhysicalDevices,enumerate)
#undef H
    if(!i)return nullptr;return instance(i).get(i,name);
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getDevice(VkDevice d,const char* name){if(auto p=deviceHook(name))return p;if(!d)return nullptr;auto p=device(d);return p.get(d,name);}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getPhysical(VkInstance i,const char* name){if(!i)return nullptr;auto p=instance(i);return p.physical?p.physical(i,name):p.get(i,name);}
}
EXPORT VKAPI_ATTR VkResult VKAPI_CALL gfxRecoveryNegotiate(VkNegotiateLayerInterface* value){
    if(!value||value->sType!=LAYER_NEGOTIATE_INTERFACE_STRUCT||value->loaderLayerInterfaceVersion<2)return VK_ERROR_INITIALIZATION_FAILED;
    value->loaderLayerInterfaceVersion=2;value->pfnGetInstanceProcAddr=getInstance;value->pfnGetDeviceProcAddr=getDevice;value->pfnGetPhysicalDeviceProcAddr=getPhysical;return VK_SUCCESS;
}
EXPORT int gfxRecoveryArm(unsigned mode){std::lock_guard lock(mutex);if(mode<1||mode>5||totals.armed)return 0;totals.armed=mode;return 1;}
EXPORT void gfxRecoveryReverse(unsigned enabled){std::lock_guard lock(mutex);reverseEnumeration=enabled!=0;}
EXPORT int gfxRecoverySnapshot(recoveryprobe::Snapshot* out,unsigned size){if(!out||size!=sizeof(*out))return 0;std::lock_guard lock(mutex);*out=totals;out->liveInstances=static_cast<unsigned>(instances.size());out->liveDevices=static_cast<unsigned>(devices.size());return 1;}
