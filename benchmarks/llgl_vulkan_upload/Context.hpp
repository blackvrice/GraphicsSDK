#pragma once
#include <Windows.h>
#include <TlHelp32.h>
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
inline std::string module(HMODULE h){require(h!=nullptr,"Required runtime module absent");std::array<wchar_t,32768> p{};auto n=GetModuleFileNameW(h,p.data(),DWORD(p.size()));require(n&&n<p.size(),"Module path unavailable");return std::filesystem::path(p.data()).generic_string();}
inline std::string modules(){
    HANDLE h=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());require(h!=INVALID_HANDLE_VALUE,"Runtime snapshot failed");
    MODULEENTRY32W entry{};entry.dwSize=sizeof(entry);std::vector<std::string> paths;
    if(Module32FirstW(h,&entry))do{paths.push_back(std::filesystem::path(entry.szExePath).generic_string());}while(Module32NextW(h,&entry));
    CloseHandle(h);std::sort(paths.begin(),paths.end());std::string out="[";for(auto& p:paths){if(out.size()>1)out+=',';out+=quote(p);}return out+"]";
}
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
    LLGL::RenderSystemPtr renderer;
    Messenger messenger;
    LLGL::Vulkan::RenderSystemNativeHandle native{};
    VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    VkQueueFlags flags{};unsigned loaderVersion{};std::string loadedLayer,loader,core,backend;
    PFN_vkGetPhysicalDeviceProperties2 getProperties{};
    Context(bool validation,const std::string& expected){
        const char* layer="VK_LAYER_KHRONOS_validation";
        if(validation){unsigned count{};require(vkEnumerateInstanceLayerProperties(&count,nullptr)==VK_SUCCESS,"Layer enumeration failed");std::vector<VkLayerProperties> found(count);require(vkEnumerateInstanceLayerProperties(&count,found.data())==VK_SUCCESS,"Layer enumeration failed");require(std::count_if(found.begin(),found.end(),[&](auto& p){return std::string(p.layerName)==layer;})==1,"Required validation layer absent");}
        LLGL::Log::RegisterCallback([](LLGL::Log::ReportType type,const char* text,void*){if(type==LLGL::Log::ReportType::Error)++errors;if(text)std::cerr<<text;});
        LLGL::RendererConfigurationVulkan config{};if(validation)config.enabledLayers={&layer,1};
        LLGL::RenderSystemDescriptor desc{"Vulkan"};desc.flags=LLGL::RenderSystemFlags::PreferNVIDIA;if(validation)desc.flags|=LLGL::RenderSystemFlags::DebugDevice;
        desc.rendererConfig=&config;desc.rendererConfigSize=sizeof(config);LLGL::Report report;renderer=LLGL::RenderSystem::Load(desc,&report);
        require(renderer!=nullptr,"LLGL Vulkan load failed");require(!report.HasErrors(),report.GetText());require(renderer->GetRendererID()==LLGL::RendererID::Vulkan,"Vulkan backend required");
        require(renderer->GetNativeHandle(&native,sizeof(native))&&native.instance&&native.device&&native.physicalDevice&&native.queue,"Actual native handles required");
        properties.pNext=&id;getProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(vkGetInstanceProcAddr(native.instance,"vkGetPhysicalDeviceProperties2"));require(getProperties!=nullptr,"Native identity query unavailable");getProperties(native.physicalDevice,&properties);
        require(expected==properties.properties.deviceName,"Wrong target GPU; no fallback");require(properties.properties.vendorID==4318&&properties.properties.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU&&id.deviceLUIDValid,"Discrete NVIDIA and LUID required");
        unsigned count{};vkGetPhysicalDeviceQueueFamilyProperties(native.physicalDevice,&count,nullptr);std::vector<VkQueueFamilyProperties> queues(count);vkGetPhysicalDeviceQueueFamilyProperties(native.physicalDevice,&count,queues.data());
        require(native.queueFamily<count&&(queues[native.queueFamily].queueFlags&VK_QUEUE_GRAPHICS_BIT),"Graphics queue required");flags=queues[native.queueFamily].queueFlags;VkQueue queue{};vkGetDeviceQueue(native.device,native.queueFamily,0,&queue);require(queue==native.queue,"Actual queue mismatch");
        require(vkEnumerateInstanceVersion(&loaderVersion)==VK_SUCCESS,"Loader version unavailable");loader=module(GetModuleHandleW(L"vulkan-1.dll"));core=module(GetModuleHandleW(L"LLGL.dll"));backend=module(GetModuleHandleW(L"LLGL_Vulkan.dll"));
        if(validation){loadedLayer=module(GetModuleHandleW(L"VkLayer_khronos_validation.dll"));messenger.instance=native.instance;
            auto create=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(native.instance,"vkCreateDebugUtilsMessengerEXT"));messenger.destroy=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(native.instance,"vkDestroyDebugUtilsMessengerEXT"));require(create&&messenger.destroy,"Debug messenger unavailable");
            VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};info.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;info.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;info.pfnUserCallback=message;require(create(native.instance,&info,nullptr,&messenger.handle)==VK_SUCCESS,"Messenger creation failed");
        }else require(GetModuleHandleW(L"VkLayer_khronos_validation.dll")==nullptr,"Unexpected validation module");
        require(errors==0,"Validation errors during context initialization");
    }
    std::string identity()const{
        auto& p=properties.properties;std::ostringstream o;
        o<<"\"device_name\":"<<quote(p.deviceName)<<",\"device_uuid\":"<<quote(hex(id.deviceUUID))<<",\"driver_uuid\":"<<quote(hex(id.driverUUID))<<",\"device_luid\":"<<quote(hex(id.deviceLUID))
         <<",\"native_vendor_id\":"<<p.vendorID<<",\"native_device_id\":"<<p.deviceID<<",\"native_driver_version\":"<<p.driverVersion<<",\"native_api_version\":"<<p.apiVersion<<",\"native_handle_verified\":true"
         <<",\"queue_family\":"<<native.queueFamily<<",\"queue_index\":0,\"queue_flags\":"<<flags<<",\"loader_api_version\":"<<loaderVersion<<",\"instance_API_request_observed\":false"
         <<",\"loader_path\":"<<quote(loader)<<",\"LLGL_path\":"<<quote(core)<<",\"backend_path\":"<<quote(backend)<<",\"loaded_validation_layer\":"<<quote(loadedLayer);return o.str();
    }
    void close(){messenger.close();renderer.reset();}
};
}
