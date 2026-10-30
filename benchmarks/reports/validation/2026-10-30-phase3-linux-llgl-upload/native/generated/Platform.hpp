#pragma once
#include <vulkan/vulkan.h>
#include "LinuxEnvironment.hpp"
#include <cstdlib>
#include <LLGL/LLGL.h>
#include <LLGL/Backend/Vulkan/NativeHandle.h>
#include <LLGL/RendererConfiguration.h>
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace llglupload {
inline void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
inline std::string quote(const std::string& s){std::ostringstream o;o<<'"';for(unsigned char c:s){if(c=='"'||c=='\\')o<<'\\'<<c;else if(c<32)o<<"\\u00"<<"0123456789abcdef"[c>>4]<<"0123456789abcdef"[c&15];else o<<c;}return o.str()+'"';}
template<class T>std::string hex(const T& s){std::string out;for(unsigned char c:s){out+="0123456789abcdef"[c>>4];out+="0123456789abcdef"[c&15];}return out;}
inline std::string module(const char* name){
    std::vector<std::string> found;for(const auto& p:linuxbench::modules())if(std::filesystem::path(p).filename()==name)found.push_back(p);
    require(found.size()==1,"Required runtime module absent or ambiguous");return found.front();
}
inline std::string modules(){std::string out="[";for(const auto& p:linuxbench::modules()){if(out.size()>1)out+=',';out+=quote(p);}return out+"]";}
inline std::atomic<unsigned> errors{};
inline std::mutex errorMutex;
inline std::vector<std::string> errorIds;
inline VKAPI_ATTR VkBool32 VKAPI_CALL message(VkDebugUtilsMessageSeverityFlagBitsEXT severity,VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* data,void*){
    if(data&&data->pMessage)std::cerr<<data->pMessage<<'\n';
    if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT){++errors;std::lock_guard lock(errorMutex);if(errorIds.size()<64)errorIds.push_back(data&&data->pMessageIdName?data->pMessageIdName:"");return VK_TRUE;}
    return VK_FALSE;
}
struct Messenger {
    VkInstance instance{};VkDebugUtilsMessengerEXT handle{};PFN_vkDestroyDebugUtilsMessengerEXT destroy{};
    void close(){if(handle){destroy(instance,handle,nullptr);handle={};}}
    ~Messenger(){close();}
};
struct Context {
    linuxbench::NativeQuery query;
    linuxbench::Identity selected;
    LLGL::RenderSystemPtr renderer;
    Messenger messenger;
    LLGL::Vulkan::RenderSystemNativeHandle native{};
    VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    VkQueueFlags flags{};unsigned loaderVersion{};std::string loadedLayer,loader,core,backend;
    PFN_vkGetPhysicalDeviceProperties2 getProperties{};
    Context(bool validation,const std::string& expected){
        const char* kind=std::getenv("GFX_LLGL_UPLOAD_KIND");require(!kind||std::string_view(kind)=="software-diagnostic","Software diagnostics only");
        selected=linuxbench::select(query.enumerate(validation),expected,"software-diagnostic");
        const char* layer="VK_LAYER_KHRONOS_validation";
        if(validation){unsigned count{};require(vkEnumerateInstanceLayerProperties(&count,nullptr)==VK_SUCCESS,"Layer enumeration failed");std::vector<VkLayerProperties> found(count);require(vkEnumerateInstanceLayerProperties(&count,found.data())==VK_SUCCESS,"Layer enumeration failed");require(std::count_if(found.begin(),found.end(),[&](auto& p){return std::string(p.layerName)==layer;})==1,"Required validation layer absent");}
        LLGL::Log::RegisterCallback([](LLGL::Log::ReportType type,const char* text,void*){if(type==LLGL::Log::ReportType::Error)++errors;if(text)std::cerr<<text;});
        LLGL::RendererConfigurationVulkan config{};if(validation)config.enabledLayers={&layer,1};
        LLGL::RenderSystemDescriptor desc{"Vulkan"};desc.flags=0;if(validation)desc.flags|=LLGL::RenderSystemFlags::DebugDevice;
        desc.rendererConfig=&config;desc.rendererConfigSize=sizeof(config);LLGL::Report report;renderer=LLGL::RenderSystem::Load(desc,&report);
        require(renderer!=nullptr,"LLGL Vulkan load failed");require(!report.HasErrors(),report.GetText());require(renderer->GetRendererID()==LLGL::RendererID::Vulkan,"Vulkan backend required");
        require(renderer->GetNativeHandle(&native,sizeof(native))&&native.instance&&native.device&&native.physicalDevice&&native.queue,"Actual native handles required");
        properties.pNext=&id;getProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(vkGetInstanceProcAddr(native.instance,"vkGetPhysicalDeviceProperties2"));require(getProperties!=nullptr,"Native identity query unavailable");getProperties(native.physicalDevice,&properties);
        require(hex(id.deviceUUID)==selected.uuid&&hex(id.driverUUID)==selected.driverUuid
            &&selected.name==properties.properties.deviceName&&selected.type==properties.properties.deviceType
            &&selected.vendor==properties.properties.vendorID&&selected.device==properties.properties.deviceID
            &&selected.driver==properties.properties.driverVersion&&selected.api==properties.properties.apiVersion,"Actual LLGL target differs from qualification");
        unsigned count{};vkGetPhysicalDeviceQueueFamilyProperties(native.physicalDevice,&count,nullptr);std::vector<VkQueueFamilyProperties> queues(count);vkGetPhysicalDeviceQueueFamilyProperties(native.physicalDevice,&count,queues.data());
        require(native.queueFamily<count&&(queues[native.queueFamily].queueFlags&VK_QUEUE_GRAPHICS_BIT),"Graphics queue required");flags=queues[native.queueFamily].queueFlags;VkQueue queue{};vkGetDeviceQueue(native.device,native.queueFamily,0,&queue);require(queue==native.queue,"Actual queue mismatch");
        require(vkEnumerateInstanceVersion(&loaderVersion)==VK_SUCCESS,"Loader version unavailable");loader=query.loaderPath;core=module("libLLGL.so");backend=module("libLLGL_Vulkan.so");
        if(validation){loadedLayer=module("libVkLayer_khronos_validation.so");messenger.instance=native.instance;
            auto create=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(native.instance,"vkCreateDebugUtilsMessengerEXT"));messenger.destroy=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(native.instance,"vkDestroyDebugUtilsMessengerEXT"));require(create&&messenger.destroy,"Debug messenger unavailable");
            VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};info.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;info.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;info.pfnUserCallback=message;require(create(native.instance,&info,nullptr,&messenger.handle)==VK_SUCCESS,"Messenger creation failed");
        }else for(const auto& path:linuxbench::modules())require(path.find("VkLayer_khronos_validation")==std::string::npos,"Unexpected validation module");
        require(errors==0,"Validation errors during context initialization");
    }
    std::string identity()const{
        auto& p=properties.properties;std::ostringstream o;
        o<<"\"device_name\":"<<quote(p.deviceName)<<",\"device_uuid\":"<<quote(hex(id.deviceUUID))<<",\"driver_uuid\":"<<quote(hex(id.driverUUID))<<",\"device_luid\":"<<"null"
         <<",\"native_vendor_id\":"<<p.vendorID<<",\"native_device_id\":"<<p.deviceID<<",\"native_driver_version\":"<<p.driverVersion<<",\"native_api_version\":"<<p.apiVersion<<",\"native_handle_verified\":true"
         <<",\"queue_family\":"<<native.queueFamily<<",\"queue_index\":0,\"queue_flags\":"<<flags<<",\"loader_api_version\":"<<loaderVersion<<",\"instance_API_request_observed\":false"
         <<",\"loader_path\":"<<quote(loader)<<",\"LLGL_path\":"<<quote(core)<<",\"backend_path\":"<<quote(backend)<<",\"loaded_validation_layer\":"<<quote(loadedLayer)<<",\"selected\":"<<selected.json()<<",\"environment\":"<<linuxbench::environment()
         <<",\"mode\":\"software-diagnostic\",\"unmodified_upstream\":false,\"physical_GPU_accepted\":false,\"P3_LBEN_01_completed\":false";return o.str();
    }
    void close(){messenger.close();renderer.reset();}
};
}
