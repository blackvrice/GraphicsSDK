// Test-only proxy in a child LD_LIBRARY_PATH; forwards to the actual system loader.
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_XCB_KHR
#include <vulkan/vulkan.h>
#include "Control.hpp"
#include <dlfcn.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>
namespace {
surfaceprobe::Snapshot totals;std::mutex mutex;std::map<VkPhysicalDevice,VkInstance> parents;
int mode(){auto p=std::getenv("GFX_SURFACE_CONTROL");return p?std::atoi(p):0;}
PFN_vkGetInstanceProcAddr original(){
    static void* library=dlopen(std::getenv("GFX_SURFACE_REAL_LOADER"),RTLD_NOW|RTLD_LOCAL);
    static auto get=library?reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library,"vkGetInstanceProcAddr")):nullptr;
    return get;
}
template<class T>T next(VkInstance i,const char* name){return reinterpret_cast<T>(original()(i,name));}
VkInstance parent(VkPhysicalDevice p){std::lock_guard lock(mutex);return parents.at(p);}
VKAPI_ATTR VkResult VKAPI_CALL extensions(const char* layer,uint32_t* count,VkExtensionProperties* out){
    auto call=next<PFN_vkEnumerateInstanceExtensionProperties>({},"vkEnumerateInstanceExtensionProperties");
    if(mode()!=1||layer)return call(layer,count,out);
    uint32_t size{};auto r=call(nullptr,&size,nullptr);if(r!=VK_SUCCESS)return r;
    std::vector<VkExtensionProperties> values(size);r=call(nullptr,&size,values.data());if(r!=VK_SUCCESS)return r;values.resize(size);
    std::erase_if(values,[](auto& p){return !std::strcmp(p.extensionName,"VK_KHR_xcb_surface");});
    if(!out){*count=values.size();return VK_SUCCESS;}
    auto copied=std::min<std::size_t>(*count,values.size());std::copy_n(values.data(),copied,out);*count=copied;return copied<values.size()?VK_INCOMPLETE:VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL createInstance(const VkInstanceCreateInfo* info,const VkAllocationCallbacks* a,VkInstance* out){
    auto r=next<PFN_vkCreateInstance>({},"vkCreateInstance")(info,a,out);if(r==VK_SUCCESS){std::lock_guard lock(mutex);++totals.instances;
        for(unsigned i=0;i<info->enabledExtensionCount;++i)if(!std::strcmp(info->ppEnabledExtensionNames[i],"VK_KHR_xcb_surface"))++totals.xcbEnabled;}return r;
}
VKAPI_ATTR void VKAPI_CALL destroyInstance(VkInstance i,const VkAllocationCallbacks* a){
    next<PFN_vkDestroyInstance>(i,"vkDestroyInstance")(i,a);std::lock_guard lock(mutex);std::erase_if(parents,[&](auto& p){return p.second==i;});++totals.destroyedInstances;
}
VKAPI_ATTR VkResult VKAPI_CALL physicalDevices(VkInstance i,uint32_t* count,VkPhysicalDevice* out){
    auto r=next<PFN_vkEnumeratePhysicalDevices>(i,"vkEnumeratePhysicalDevices")(i,count,out);
    if((r==VK_SUCCESS||r==VK_INCOMPLETE)&&out){std::lock_guard lock(mutex);for(unsigned n=0;n<*count;++n)parents[out[n]]=i;}return r;
}
VKAPI_ATTR VkResult VKAPI_CALL createSurface(VkInstance i,const VkXcbSurfaceCreateInfoKHR* info,const VkAllocationCallbacks* a,VkSurfaceKHR* out){
    auto r=next<PFN_vkCreateXcbSurfaceKHR>(i,"vkCreateXcbSurfaceKHR")(i,info,a,out);if(r==VK_SUCCESS){std::lock_guard lock(mutex);++totals.surfaces;}return r;
}
VKAPI_ATTR void VKAPI_CALL destroySurface(VkInstance i,VkSurfaceKHR surface,const VkAllocationCallbacks* a){next<PFN_vkDestroySurfaceKHR>(i,"vkDestroySurfaceKHR")(i,surface,a);std::lock_guard lock(mutex);++totals.destroyedSurfaces;}
VKAPI_ATTR VkResult VKAPI_CALL surfaceSupport(VkPhysicalDevice p,uint32_t family,VkSurfaceKHR s,VkBool32* out){
    {std::lock_guard lock(mutex);++totals.surfaceQueries;totals.family=family;}
    if(mode()==5)return VK_ERROR_SURFACE_LOST_KHR;
    auto r=next<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(parent(p),"vkGetPhysicalDeviceSurfaceSupportKHR")(p,family,s,out);
    if(r==VK_SUCCESS){std::lock_guard lock(mutex);totals.nativeSurfaceTrue+=*out==VK_TRUE;if(mode()==2)*out=VK_FALSE;}return r;
}
VKAPI_ATTR VkBool32 VKAPI_CALL visualSupport(VkPhysicalDevice p,uint32_t family,xcb_connection_t* connection,xcb_visualid_t visual){
    auto r=next<PFN_vkGetPhysicalDeviceXcbPresentationSupportKHR>(parent(p),"vkGetPhysicalDeviceXcbPresentationSupportKHR")(p,family,connection,visual);
    std::lock_guard lock(mutex);++totals.visualQueries;totals.nativeVisualTrue+=r==VK_TRUE;return mode()==3?VK_FALSE:r;
}
}
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance i,const char* name){
    if(!original())return nullptr;
    if(mode()==4&&!std::strcmp(name,"vkCreateXcbSurfaceKHR"))return nullptr;
#define H(n,f) if(!std::strcmp(name,"vk" #n))return reinterpret_cast<PFN_vkVoidFunction>(f);
    H(GetInstanceProcAddr,vkGetInstanceProcAddr) H(EnumerateInstanceExtensionProperties,extensions) H(CreateInstance,createInstance) H(DestroyInstance,destroyInstance)
    H(EnumeratePhysicalDevices,physicalDevices) H(CreateXcbSurfaceKHR,createSurface) H(DestroySurfaceKHR,destroySurface) H(GetPhysicalDeviceSurfaceSupportKHR,surfaceSupport) H(GetPhysicalDeviceXcbPresentationSupportKHR,visualSupport)
#undef H
    return original()(i,name);
}
extern "C" int gfxSurfaceSnapshot(surfaceprobe::Snapshot* out,unsigned size){if(!out||size!=sizeof(*out))return 0;std::lock_guard lock(mutex);*out=totals;return 1;}
