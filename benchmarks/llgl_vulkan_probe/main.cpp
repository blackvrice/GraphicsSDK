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
#include <sstream>
#include <stdexcept>
#include <vector>

#ifndef NDEBUG
#error Release required
#endif
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::string quote(const std::string& s){std::ostringstream o;o<<'"';for(unsigned char c:s){if(c=='"'||c=='\\')o<<'\\'<<c;else if(c<32)o<<"\\u00"<<"0123456789abcdef"[c>>4]<<"0123456789abcdef"[c&15];else o<<c;}o<<'"';return o.str();}
template<class T>std::string hex(const T& s){std::string out;for(unsigned char c:s){out+="0123456789abcdef"[c>>4];out+="0123456789abcdef"[c&15];}return out;}
std::string module(HMODULE h){require(h!=nullptr,"Required runtime module not loaded");std::array<wchar_t,32768> p{};auto n=GetModuleFileNameW(h,p.data(),DWORD(p.size()));require(n&&n<p.size(),"Module path unavailable");return std::filesystem::path(p.data()).generic_string();}
std::string modules(){
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());require(snapshot!=INVALID_HANDLE_VALUE,"Runtime module snapshot failed");
    MODULEENTRY32W entry{};entry.dwSize=sizeof(entry);std::vector<std::string> paths;
    if(Module32FirstW(snapshot,&entry))do{paths.push_back(std::filesystem::path(entry.szExePath).generic_string());}while(Module32NextW(snapshot,&entry));
    CloseHandle(snapshot);std::sort(paths.begin(),paths.end());std::string out="[";for(const auto& path:paths){if(out.size()>1)out+=',';out+=quote(path);}return out+"]";
}
std::atomic<unsigned> errors{};
VKAPI_ATTR VkBool32 VKAPI_CALL message(VkDebugUtilsMessageSeverityFlagBitsEXT severity,VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* data,void*){
    if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)++errors;
    if(data&&data->pMessage)std::cerr<<data->pMessage<<'\n';return VK_FALSE;
}
struct Messenger {
    VkInstance instance{};VkDebugUtilsMessengerEXT handle{};PFN_vkDestroyDebugUtilsMessengerEXT destroy{};
    void close(){if(handle){destroy(instance,handle,nullptr);handle={};}}
    ~Messenger(){close();}
};
}
int main(int argc,char**argv){
    try{
        require(argc==4,"usage: llgl_vulkan_probe EXPECTED_GPU plain|validation identity|smoke");
        const std::string expected=argv[1],mode=argv[2],operation=argv[3];
        require(!expected.empty()&&(mode=="plain"||mode=="validation")&&(operation=="identity"||operation=="smoke"),"Invalid probe arguments");
        const bool validation=mode=="validation",smoke=operation=="smoke";
        const char* layer="VK_LAYER_KHRONOS_validation";
        if(validation){
            unsigned count{};require(vkEnumerateInstanceLayerProperties(&count,nullptr)==VK_SUCCESS,"Layer enumeration failed");
            std::vector<VkLayerProperties> layers(count);require(vkEnumerateInstanceLayerProperties(&count,layers.data())==VK_SUCCESS,"Layer enumeration failed");
            require(std::count_if(layers.begin(),layers.end(),[&](const auto& p){return std::string(p.layerName)==layer;})==1,"Required validation layer absent; LLGL layer fallback rejected");
        }
        LLGL::Log::RegisterCallback([](LLGL::Log::ReportType type,const char* text,void*){if(type==LLGL::Log::ReportType::Error)++errors;if(text)std::cerr<<text;});
        LLGL::RendererConfigurationVulkan config{};
        if(validation)config.enabledLayers={&layer,1};
        LLGL::RenderSystemDescriptor desc{"Vulkan"};desc.flags=LLGL::RenderSystemFlags::PreferNVIDIA;
        if(validation)desc.flags|=LLGL::RenderSystemFlags::DebugDevice;
        desc.rendererConfig=&config;desc.rendererConfigSize=sizeof(config);
        LLGL::Report report;auto renderer=LLGL::RenderSystem::Load(desc,&report);
        require(renderer!=nullptr,"LLGL Vulkan load failed");require(!report.HasErrors(),report.GetText());
        require(renderer->GetRendererID()==LLGL::RendererID::Vulkan,"Actual renderer is not Vulkan");
        LLGL::Vulkan::RenderSystemNativeHandle native{};
        require(renderer->GetNativeHandle(&native,sizeof(native))&&native.instance&&native.physicalDevice&&native.device&&native.queue,"Actual Vulkan handles unavailable");
        VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};properties.pNext=&id;
        auto getProps=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(vkGetInstanceProcAddr(native.instance,"vkGetPhysicalDeviceProperties2"));
        require(getProps!=nullptr,"Native properties2 unavailable");getProps(native.physicalDevice,&properties);auto& p=properties.properties;
        require(expected==p.deviceName,"Wrong target GPU; no fallback");
        require(p.vendorID==0x10de&&p.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU&&id.deviceLUIDValid,"Discrete NVIDIA hardware and valid LUID required");
        unsigned count{};vkGetPhysicalDeviceQueueFamilyProperties(native.physicalDevice,&count,nullptr);std::vector<VkQueueFamilyProperties> queues(count);
        vkGetPhysicalDeviceQueueFamilyProperties(native.physicalDevice,&count,queues.data());
        require(native.queueFamily<count&&(queues[native.queueFamily].queueFlags&VK_QUEUE_GRAPHICS_BIT),"Wrong native queue family");
        VkQueue queue{};vkGetDeviceQueue(native.device,native.queueFamily,0,&queue);require(queue==native.queue,"Native queue index 0 mismatch");
        auto loader=module(GetModuleHandleW(L"vulkan-1.dll"));
        auto core=module(GetModuleHandleW(L"LLGL.dll"));auto backend=module(GetModuleHandleW(L"LLGL_Vulkan.dll"));
        std::string loadedLayer;Messenger messenger;
        if(validation){
            loadedLayer=module(GetModuleHandleW(L"VkLayer_khronos_validation.dll"));
            messenger.instance=native.instance;
            auto create=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(native.instance,"vkCreateDebugUtilsMessengerEXT"));
            messenger.destroy=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(native.instance,"vkDestroyDebugUtilsMessengerEXT"));
            require(create&&messenger.destroy,"Debug messenger entry unavailable");
            VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            info.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
            info.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;info.pfnUserCallback=message;
            require(create(native.instance,&info,nullptr,&messenger.handle)==VK_SUCCESS,"Debug messenger create failed");
        }else require(GetModuleHandleW(L"VkLayer_khronos_validation.dll")==nullptr,"Validation module present in plain probe");
        std::uint64_t verified{};unsigned rounds{};
        if(smoke){
            LLGL::BufferDescriptor sourceDesc{};sourceDesc.size=4096;sourceDesc.bindFlags=LLGL::BindFlags::CopySrc;
            LLGL::BufferDescriptor destDesc=sourceDesc;destDesc.bindFlags=LLGL::BindFlags::CopyDst;
            auto src=renderer->CreateBuffer(sourceDesc);auto dst=renderer->CreateBuffer(destDesc);
            auto commands=renderer->CreateCommandBuffer({});auto fence=renderer->CreateFence();auto& q=*renderer->GetCommandQueue();
            require(src&&dst&&commands&&fence,"LLGL smoke resources unavailable");
            for(unsigned pass=0;pass<3;++pass){
                std::array<unsigned char,4096> payload{},readback{};
                for(unsigned i=0;i<payload.size();++i)payload[i]=static_cast<unsigned char>((i*17+(i>>8)*31+pass*73)^0x5a);
                renderer->WriteBuffer(*src,0,payload.data(),payload.size());
                commands->Begin();commands->CopyBuffer(*dst,0,*src,0,payload.size());commands->End();
                q.Submit(*commands);q.Submit(*fence);require(q.WaitFence(*fence,5'000'000'000ull),"LLGL fence timeout");
                renderer->ReadBuffer(*dst,0,readback.data(),readback.size());require(payload==readback,"Full changing buffer reference mismatch");
                verified+=readback.size();++rounds;
            }
            q.WaitIdle();renderer->Release(*commands);renderer->Release(*fence);renderer->Release(*dst);renderer->Release(*src);
        }
        unsigned loaderVersion{};require(vkEnumerateInstanceVersion(&loaderVersion)==VK_SUCCESS,"Loader API version unavailable");
        auto runtime=modules();messenger.close();renderer.reset();require(errors==0,"LLGL/native validation error delivered");
        std::cout<<"{\"schema_version\":1,\"task\":\"P3-CMP-02\",\"sdk\":\"llgl\",\"api\":\"vulkan\",\"configuration\":\"Release\",\"source_fingerprint\":"<<quote(GFX_LLGL_PROBE_FINGERPRINT)<<",\"LLGL_commit\":"<<quote(GFX_LLGL_PIN)
          <<",\"device_name\":"<<quote(p.deviceName)<<",\"device_uuid\":"<<quote(hex(id.deviceUUID))<<",\"driver_uuid\":"<<quote(hex(id.driverUUID))<<",\"device_luid\":"<<quote(hex(id.deviceLUID))
          <<",\"native_vendor_id\":"<<p.vendorID<<",\"native_device_id\":"<<p.deviceID<<",\"native_driver_version\":"<<p.driverVersion<<",\"native_api_version\":"<<p.apiVersion
          <<",\"queue_family\":"<<native.queueFamily<<",\"queue_index\":0,\"queue_flags\":"<<queues[native.queueFamily].queueFlags<<",\"native_handle_verified\":true,\"validation\":"<<(validation?"true":"false")
          <<",\"loaded_validation_layer\":"<<quote(loadedLayer)<<",\"delivered_errors\":"<<errors<<",\"loader_path\":"<<quote(loader)<<",\"LLGL_path\":"<<quote(core)<<",\"backend_path\":"<<quote(backend)
          <<",\"runtime_modules\":"<<runtime<<",\"loader_api_version\":"<<loaderVersion<<",\"instance_API_request_observed\":false,\"operation\":"<<quote(operation)<<",\"verified_bytes\":"<<verified<<",\"reference_rounds\":"<<rounds<<",\"performance_samples\":0,\"gpu_ms\":null,\"workload_correctness_accepted\":false,\"performance_accepted\":false}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
