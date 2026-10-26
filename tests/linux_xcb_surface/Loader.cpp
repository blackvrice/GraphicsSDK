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
#include <cstdio>
#include <memory>
namespace {
surfaceprobe::Snapshot totals;std::mutex mutex;std::map<VkPhysicalDevice,VkInstance> parents;
std::map<VkDevice,VkInstance> devices;std::map<VkQueue,VkDevice> queues;std::map<VkFence,VkDevice> presentFences;
int mode(){auto p=std::getenv("GFX_SURFACE_CONTROL");return p?std::atoi(p):0;}
struct Diagnostic { PFN_vkDebugUtilsMessengerCallbackEXT callback; void* user; };
std::map<VkDebugUtilsMessengerEXT,std::unique_ptr<Diagnostic>> diagnostics;
VKAPI_ATTR VkBool32 VKAPI_CALL message(VkDebugUtilsMessageSeverityFlagBitsEXT severity,VkDebugUtilsMessageTypeFlagsEXT type,const VkDebugUtilsMessengerCallbackDataEXT* data,void* user){
    auto& state=*static_cast<Diagnostic*>(user);
    std::fprintf(stderr,"validation: %s\n",data&&data->pMessage?data->pMessage:"<empty>");
    return state.callback(severity,type,data,state.user);
}
PFN_vkGetInstanceProcAddr original(){
    static void* library=dlopen(std::getenv("GFX_SURFACE_REAL_LOADER"),RTLD_NOW|RTLD_LOCAL);
    static auto get=library?reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library,"vkGetInstanceProcAddr")):nullptr;
    return get;
}
template<class T>T next(VkInstance i,const char* name){return reinterpret_cast<T>(original()(i,name));}
VKAPI_ATTR VkResult VKAPI_CALL createMessenger(VkInstance i,const VkDebugUtilsMessengerCreateInfoEXT* info,const VkAllocationCallbacks* a,VkDebugUtilsMessengerEXT* out){
    auto state=std::make_unique<Diagnostic>(Diagnostic{info->pfnUserCallback,info->pUserData});auto copy=*info;copy.pfnUserCallback=message;copy.pUserData=state.get();
    auto r=next<PFN_vkCreateDebugUtilsMessengerEXT>(i,"vkCreateDebugUtilsMessengerEXT")(i,&copy,a,out);
    if(r==VK_SUCCESS){std::lock_guard lock(mutex);diagnostics[*out]=std::move(state);}return r;
}
VKAPI_ATTR void VKAPI_CALL destroyMessenger(VkInstance i,VkDebugUtilsMessengerEXT m,const VkAllocationCallbacks* a){next<PFN_vkDestroyDebugUtilsMessengerEXT>(i,"vkDestroyDebugUtilsMessengerEXT")(i,m,a);std::lock_guard lock(mutex);diagnostics.erase(m);}
VkInstance parent(VkPhysicalDevice p){std::lock_guard lock(mutex);return parents.at(p);}
template<class T>T deviceNext(VkDevice d,const char* name){VkInstance i;{std::lock_guard lock(mutex);i=devices.at(d);}return reinterpret_cast<T>(next<PFN_vkGetDeviceProcAddr>(i,"vkGetDeviceProcAddr")(d,name));}
VKAPI_ATTR VkResult VKAPI_CALL createDevice(VkPhysicalDevice p,const VkDeviceCreateInfo* info,const VkAllocationCallbacks* a,VkDevice* out){auto i=parent(p);auto r=next<PFN_vkCreateDevice>(i,"vkCreateDevice")(p,info,a,out);if(r==VK_SUCCESS){std::lock_guard lock(mutex);devices[*out]=i;++totals.devices;}return r;}
VKAPI_ATTR void VKAPI_CALL destroyDevice(VkDevice d,const VkAllocationCallbacks* a){deviceNext<PFN_vkDestroyDevice>(d,"vkDestroyDevice")(d,a);std::lock_guard lock(mutex);devices.erase(d);std::erase_if(queues,[&](auto& q){return q.second==d;});++totals.destroyedDevices;}
VKAPI_ATTR void VKAPI_CALL getQueue(VkDevice d,uint32_t family,uint32_t index,VkQueue* out){deviceNext<PFN_vkGetDeviceQueue>(d,"vkGetDeviceQueue")(d,family,index,out);std::lock_guard lock(mutex);queues[*out]=d;}
VKAPI_ATTR VkResult VKAPI_CALL createChain(VkDevice d,const VkSwapchainCreateInfoKHR* info,const VkAllocationCallbacks* a,VkSwapchainKHR* out){auto r=deviceNext<PFN_vkCreateSwapchainKHR>(d,"vkCreateSwapchainKHR")(d,info,a,out);if(r==VK_SUCCESS){std::lock_guard lock(mutex);++totals.chains;totals.format=info->imageFormat;totals.presentMode=info->presentMode;totals.minImages=info->minImageCount;}return r;}
VKAPI_ATTR void VKAPI_CALL destroyChain(VkDevice d,VkSwapchainKHR s,const VkAllocationCallbacks* a){deviceNext<PFN_vkDestroySwapchainKHR>(d,"vkDestroySwapchainKHR")(d,s,a);std::lock_guard lock(mutex);++totals.destroyedChains;}
VKAPI_ATTR VkResult VKAPI_CALL chainImages(VkDevice d,VkSwapchainKHR s,uint32_t* count,VkImage* images){auto r=deviceNext<PFN_vkGetSwapchainImagesKHR>(d,"vkGetSwapchainImagesKHR")(d,s,count,images);if(r==VK_SUCCESS&&images){std::lock_guard lock(mutex);totals.actualImages=*count;}return r;}
VKAPI_ATTR VkResult VKAPI_CALL acquire(VkDevice d,VkSwapchainKHR s,uint64_t timeout,VkSemaphore semaphore,VkFence fence,uint32_t* index){auto r=deviceNext<PFN_vkAcquireNextImageKHR>(d,"vkAcquireNextImageKHR")(d,s,timeout,semaphore,fence,index);if(r==VK_SUCCESS||r==VK_SUBOPTIMAL_KHR){std::lock_guard lock(mutex);++totals.acquires;}return r;}
VKAPI_ATTR VkResult VKAPI_CALL present(VkQueue q,const VkPresentInfoKHR* info){VkDevice d;{std::lock_guard lock(mutex);d=queues.at(q);}auto r=deviceNext<PFN_vkQueuePresentKHR>(d,"vkQueuePresentKHR")(q,info);
    if(r==VK_SUCCESS||r==VK_SUBOPTIMAL_KHR){std::lock_guard lock(mutex);++totals.presents;auto p=static_cast<const VkBaseInStructure*>(info->pNext);while(p){if(p->sType==VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT){auto f=reinterpret_cast<const VkSwapchainPresentFenceInfoEXT*>(p);if(f->swapchainCount==1&&f->pFences&&f->pFences[0]){++totals.fencedPresents;presentFences[f->pFences[0]]=d;}}p=p->pNext;}}return r;}
VKAPI_ATTR VkResult VKAPI_CALL wait(VkDevice d,uint32_t count,const VkFence* fences,VkBool32 all,uint64_t timeout){auto r=deviceNext<PFN_vkWaitForFences>(d,"vkWaitForFences")(d,count,fences,all,timeout);if(r==VK_SUCCESS){std::lock_guard lock(mutex);for(unsigned i=0;i<count;++i)if(presentFences.erase(fences[i]))++totals.waitedPresents;}return r;}
VKAPI_ATTR VkResult VKAPI_CALL formats(VkPhysicalDevice p,VkSurfaceKHR s,uint32_t* count,VkSurfaceFormatKHR* out){auto call=next<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(parent(p),"vkGetPhysicalDeviceSurfaceFormatsKHR");if(mode()!=6)return call(p,s,count,out);if(!out){*count=1;return VK_SUCCESS;}if(!*count)return VK_INCOMPLETE;*count=1;out[0]={VK_FORMAT_B8G8R8A8_SRGB,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL modes(VkPhysicalDevice p,VkSurfaceKHR s,uint32_t* count,VkPresentModeKHR* out){auto call=next<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(parent(p),"vkGetPhysicalDeviceSurfacePresentModesKHR");if(mode()!=7&&mode()!=8)return call(p,s,count,out);uint32_t n{};auto r=call(p,s,&n,nullptr);if(r!=VK_SUCCESS)return r;std::vector<VkPresentModeKHR> values(n);r=call(p,s,&n,values.data());if(r!=VK_SUCCESS)return r;values.resize(n);std::erase_if(values,[](auto m){return mode()==7?m!=VK_PRESENT_MODE_FIFO_KHR:m==VK_PRESENT_MODE_IMMEDIATE_KHR;});if(!out){*count=values.size();return VK_SUCCESS;}auto size=std::min<std::size_t>(*count,values.size());std::copy_n(values.data(),size,out);*count=size;return size<values.size()?VK_INCOMPLETE:VK_SUCCESS;}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL deviceGet(VkDevice,const char*);
PFN_vkVoidFunction deviceHook(const char* name){
#define H(n,f) if(!std::strcmp(name,"vk" #n))return reinterpret_cast<PFN_vkVoidFunction>(f);
    H(GetDeviceProcAddr,deviceGet) H(DestroyDevice,destroyDevice) H(GetDeviceQueue,getQueue) H(CreateSwapchainKHR,createChain) H(DestroySwapchainKHR,destroyChain) H(GetSwapchainImagesKHR,chainImages) H(AcquireNextImageKHR,acquire) H(QueuePresentKHR,present) H(WaitForFences,wait)
#undef H
    return nullptr;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL deviceGet(VkDevice d,const char* name){if(auto h=deviceHook(name))return h;return deviceNext<PFN_vkVoidFunction>(d,name);}
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
    if(auto h=deviceHook(name))return h;
    if(mode()==4&&!std::strcmp(name,"vkCreateXcbSurfaceKHR"))return nullptr;
#define H(n,f) if(!std::strcmp(name,"vk" #n))return reinterpret_cast<PFN_vkVoidFunction>(f);
    H(GetInstanceProcAddr,vkGetInstanceProcAddr) H(EnumerateInstanceExtensionProperties,extensions) H(CreateInstance,createInstance) H(DestroyInstance,destroyInstance)
    H(EnumeratePhysicalDevices,physicalDevices) H(CreateXcbSurfaceKHR,createSurface) H(DestroySurfaceKHR,destroySurface) H(GetPhysicalDeviceSurfaceSupportKHR,surfaceSupport) H(GetPhysicalDeviceXcbPresentationSupportKHR,visualSupport)
    H(CreateDevice,createDevice) H(GetPhysicalDeviceSurfaceFormatsKHR,formats) H(GetPhysicalDeviceSurfacePresentModesKHR,modes)
    H(CreateDebugUtilsMessengerEXT,createMessenger) H(DestroyDebugUtilsMessengerEXT,destroyMessenger)
#undef H
    return original()(i,name);
}
extern "C" int gfxSurfaceSnapshot(surfaceprobe::Snapshot* out,unsigned size){if(!out||size!=sizeof(*out))return 0;std::lock_guard lock(mutex);*out=totals;return 1;}
