// Own explicit test layer. Synthetic dispatchable outputs are null; poisoned
// non-dispatchable outputs are blocked here before validation or driver cleanup.
#define VK_NO_PROTOTYPES
#include <Windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include "Contract.hpp"
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
namespace {
using namespace loaderfault;
void* key(const void*h){return *static_cast<void*const*>(h);}
struct I {VkInstance handle{};PFN_vkGetInstanceProcAddr get{};PFN_GetPhysicalDeviceProcAddr physical{};std::set<VkDebugUtilsMessengerEXT> messengers;std::set<VkSurfaceKHR> surfaces;};
struct D {PFN_vkGetDeviceProcAddr get{};};
std::mutex mutex;std::map<void*,I> instances;std::map<void*,D> devices;Plan plan;Snapshot totals;
I instance(VkInstance i){std::lock_guard lock(mutex);return instances.at(key(i));}
int attempt(unsigned k){std::lock_guard lock(mutex);++totals.attempts[k];auto r=plan.consume(k);if(r)++totals.injections;return r;}
VKAPI_ATTR void VKAPI_CALL destroyInstance(VkInstance i,const VkAllocationCallbacks*a){
    auto k=key(i);auto p=instance(i);
    {std::lock_guard lock(mutex);if(!p.messengers.empty()||!p.surfaces.empty())++totals.errors;}
    reinterpret_cast<PFN_vkDestroyInstance>(p.get(i,"vkDestroyInstance"))(i,a);
    std::lock_guard lock(mutex);instances.erase(k);++totals.destroyed[Instance];
}
VKAPI_ATTR void VKAPI_CALL destroyDevice(VkDevice d,const VkAllocationCallbacks*a){
    const auto k=key(d);D p;{std::lock_guard lock(mutex);p=devices.at(k);}
    reinterpret_cast<PFN_vkDestroyDevice>(p.get(d,"vkDestroyDevice"))(d,a);
    std::lock_guard lock(mutex);devices.erase(k);++totals.destroyed[Device];
}
VKAPI_ATTR VkResult VKAPI_CALL createMessenger(VkInstance i,const VkDebugUtilsMessengerCreateInfoEXT*c,const VkAllocationCallbacks*a,VkDebugUtilsMessengerEXT*out){
    if(auto r=attempt(Messenger)){*out=plan.poison?reinterpret_cast<VkDebugUtilsMessengerEXT>(std::uintptr_t(0xf11)):VK_NULL_HANDLE;return static_cast<VkResult>(r);}
    auto p=instance(i);auto r=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(p.get(i,"vkCreateDebugUtilsMessengerEXT"))(i,c,a,out);
    if(r==VK_SUCCESS){std::lock_guard lock(mutex);instances.at(key(i)).messengers.insert(*out);++totals.created[Messenger];}return r;
}
VKAPI_ATTR void VKAPI_CALL destroyMessenger(VkInstance i,VkDebugUtilsMessengerEXT h,const VkAllocationCallbacks*a){
    auto p=instance(i);{std::lock_guard lock(mutex);if(h&&!instances.at(key(i)).messengers.erase(h)){++totals.invalid;return;}}
    reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(p.get(i,"vkDestroyDebugUtilsMessengerEXT"))(i,h,a);if(h){std::lock_guard lock(mutex);++totals.destroyed[Messenger];}
}
VKAPI_ATTR VkResult VKAPI_CALL createSurface(VkInstance i,const VkWin32SurfaceCreateInfoKHR*c,const VkAllocationCallbacks*a,VkSurfaceKHR*out){
    if(auto r=attempt(Surface)){*out=plan.poison?reinterpret_cast<VkSurfaceKHR>(std::uintptr_t(0xf11)):VK_NULL_HANDLE;return static_cast<VkResult>(r);}
    auto p=instance(i);auto r=reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(p.get(i,"vkCreateWin32SurfaceKHR"))(i,c,a,out);
    if(r==VK_SUCCESS){std::lock_guard lock(mutex);instances.at(key(i)).surfaces.insert(*out);++totals.created[Surface];}return r;
}
VKAPI_ATTR void VKAPI_CALL destroySurface(VkInstance i,VkSurfaceKHR h,const VkAllocationCallbacks*a){
    auto p=instance(i);{std::lock_guard lock(mutex);if(h&&!instances.at(key(i)).surfaces.erase(h)){++totals.invalid;return;}}
    reinterpret_cast<PFN_vkDestroySurfaceKHR>(p.get(i,"vkDestroySurfaceKHR"))(i,h,a);if(h){std::lock_guard lock(mutex);++totals.destroyed[Surface];}
}
VKAPI_ATTR VkResult VKAPI_CALL createInstance(const VkInstanceCreateInfo*i,const VkAllocationCallbacks*a,VkInstance*out){
    auto link=reinterpret_cast<const VkLayerInstanceCreateInfo*>(i->pNext);
    while(link&&(link->sType!=VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO||link->function!=VK_LAYER_LINK_INFO))link=reinterpret_cast<const VkLayerInstanceCreateInfo*>(link->pNext);
    if(!link||!link->u.pLayerInfo)return VK_ERROR_INITIALIZATION_FAILED;
    auto next=link->u.pLayerInfo->pfnNextGetInstanceProcAddr;auto phys=link->u.pLayerInfo->pfnNextGetPhysicalDeviceProcAddr;
    const_cast<VkLayerInstanceCreateInfo*>(link)->u.pLayerInfo=link->u.pLayerInfo->pNext;
    if(auto r=attempt(Instance)){*out=VK_NULL_HANDLE;return static_cast<VkResult>(r);}
    auto r=reinterpret_cast<PFN_vkCreateInstance>(next(nullptr,"vkCreateInstance"))(i,a,out);
    if(r==VK_SUCCESS){std::lock_guard lock(mutex);instances.emplace(key(*out),I{*out,next,phys,{},{}});++totals.created[Instance];}return r;
}
VKAPI_ATTR VkResult VKAPI_CALL createDevice(VkPhysicalDevice physical,const VkDeviceCreateInfo*i,const VkAllocationCallbacks*a,VkDevice*out){
    auto link=reinterpret_cast<const VkLayerDeviceCreateInfo*>(i->pNext);
    while(link&&(link->sType!=VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO||link->function!=VK_LAYER_LINK_INFO))link=reinterpret_cast<const VkLayerDeviceCreateInfo*>(link->pNext);
    if(!link||!link->u.pLayerInfo)return VK_ERROR_INITIALIZATION_FAILED;
    auto next=link->u.pLayerInfo->pfnNextGetInstanceProcAddr;auto gdpa=link->u.pLayerInfo->pfnNextGetDeviceProcAddr;I p;
    {std::lock_guard lock(mutex);p=instances.at(key(physical));}
    const_cast<VkLayerDeviceCreateInfo*>(link)->u.pLayerInfo=link->u.pLayerInfo->pNext;
    if(auto r=attempt(Device)){*out=VK_NULL_HANDLE;return static_cast<VkResult>(r);}
    auto r=reinterpret_cast<PFN_vkCreateDevice>(next(p.handle,"vkCreateDevice"))(physical,i,a,out);
    if(r==VK_SUCCESS){std::lock_guard lock(mutex);devices.emplace(key(*out),D{gdpa});++totals.created[Device];}return r;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getInstance(VkInstance,const char*);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getDevice(VkDevice,const char*);
PFN_vkVoidFunction hook(const char*n){
#define H(name,fn) if(!std::strcmp(n,"vk" #name))return reinterpret_cast<PFN_vkVoidFunction>(fn);
    H(GetInstanceProcAddr,getInstance) H(GetDeviceProcAddr,getDevice) H(CreateInstance,createInstance) H(DestroyInstance,destroyInstance) H(CreateDevice,createDevice) H(DestroyDevice,destroyDevice)
    H(CreateDebugUtilsMessengerEXT,createMessenger) H(DestroyDebugUtilsMessengerEXT,destroyMessenger) H(CreateWin32SurfaceKHR,createSurface) H(DestroySurfaceKHR,destroySurface)
#undef H
    return nullptr;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getInstance(VkInstance i,const char*n){if(auto h=hook(n))return h;if(!i)return nullptr;return instance(i).get(i,n);}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getDevice(VkDevice d,const char*n){
    if(!std::strcmp(n,"vkGetDeviceProcAddr"))return reinterpret_cast<PFN_vkVoidFunction>(getDevice);
    if(!std::strcmp(n,"vkDestroyDevice"))return reinterpret_cast<PFN_vkVoidFunction>(destroyDevice);
    if(!d)return nullptr;D p;{std::lock_guard lock(mutex);p=devices.at(key(d));}return p.get(d,n);
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getPhysical(VkInstance i,const char*n){if(!i)return nullptr;auto p=instance(i);return p.physical?p.physical(i,n):p.get(i,n);}
}
extern "C" __declspec(dllexport) VKAPI_ATTR VkResult VKAPI_CALL gfxNegotiateLayer(VkNegotiateLayerInterface*v){
    if(!v||v->sType!=LAYER_NEGOTIATE_INTERFACE_STRUCT||v->loaderLayerInterfaceVersion<2)return VK_ERROR_INITIALIZATION_FAILED;
    v->loaderLayerInterfaceVersion=2;v->pfnGetInstanceProcAddr=getInstance;v->pfnGetDeviceProcAddr=getDevice;v->pfnGetPhysicalDeviceProcAddr=getPhysical;return VK_SUCCESS;
}
extern "C" __declspec(dllexport) int __cdecl gfxLoaderArm(unsigned k,int r,unsigned poison){std::lock_guard lock(mutex);return plan.arm(k,r,poison)?1:0;}
extern "C" __declspec(dllexport) int __cdecl gfxLoaderSnapshot(loaderfault::Snapshot*out,unsigned size){
    if(!out||size!=sizeof(*out))return 0;std::lock_guard lock(mutex);*out=totals;out->pending=plan.pending();
    out->live[Instance]=instances.size();out->live[Device]=devices.size();
    for(auto&[k,p]:instances){(void)k;out->live[Messenger]+=p.messengers.size();out->live[Surface]+=p.surfaces.size();}return 1;
}
