#include <vulkan/vulkan.h>
#include <LLGL/LLGL.h>
#include <LLGL/Backend/Vulkan/NativeHandle.h>
#include <LLGL/RendererConfiguration.h>
#include "../common/LinuxEnvironment.hpp"
#include <array>
#include <atomic>
#include <iostream>
#include <mutex>

#ifndef NDEBUG
#error Release required
#endif
namespace {
using linuxbench::require;
using linuxbench::quote;
std::atomic<unsigned> errors{}, logErrors{};
std::mutex mutex;
std::vector<std::string> diagnosticIds;
VKAPI_ATTR VkBool32 VKAPI_CALL callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    if(data && data->pMessage) std::cerr << data->pMessage << '\n';
    if(severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        ++errors;
        std::lock_guard lock(mutex);
        diagnosticIds.emplace_back(data && data->pMessageIdName ? data->pMessageIdName : "");
        return VK_TRUE; // Abort the deliberate host-only invalid-parameter control.
    }
    return VK_FALSE;
}
struct Messenger {
    VkInstance instance{}; VkDebugUtilsMessengerEXT handle{};
    PFN_vkDestroyDebugUtilsMessengerEXT destroy{};
    void close() {if(handle) {destroy(instance,handle,nullptr); handle={};}}
    ~Messenger() {close();}
};
std::string array(const std::set<std::string>& paths) {
    std::string result="[";
    for(const auto& path: paths) {if(result.size()>1) result+=','; result+=quote(path);}
    return result+"]";
}
std::string module(const std::set<std::string>& paths, const char* name) {
    std::vector<std::string> found;
    for(const auto& path: paths) if(std::filesystem::path(path).filename()==name) found.push_back(path);
    require(found.size()==1,"Required runtime module absent or ambiguous");
    return found.front();
}
}
int main(int argc,char** argv) try {
    linuxbench::NativeQuery query;
    if(argc==2 && std::string_view(argv[1])=="inventory") {
        auto devices=query.enumerate();
        std::cout<<"{\"inventory\":"<<linuxbench::inventory(devices)
                 <<",\"logical_device_created\":false,\"performance_samples\":0}\n";
        return 0;
    }
    require(argc==5,"Usage: linux_llgl_probe UUID plain|validation identity|smoke|callback-control software-diagnostic");
    const std::string uuid=argv[1],mode=argv[2],op=argv[3],kind=argv[4];
    require(kind=="software-diagnostic","Software diagnostics only");
    require((mode=="plain"||mode=="validation") && (op=="identity"||op=="smoke"||op=="callback-control"),"Invalid probe arguments");
    const bool validation=mode=="validation",control=op=="callback-control",smoke=op=="smoke";
    require(!control || validation,"Callback control requires validation");
    const auto selected=linuxbench::select(query.enumerate(validation),uuid,kind);
    LLGL::Log::RegisterCallback([](LLGL::Log::ReportType type,const char* text,void*) {
        if(type==LLGL::Log::ReportType::Error) ++logErrors;
        if(text) std::cerr<<text;
    });
    const char* layer="VK_LAYER_KHRONOS_validation";
    LLGL::RendererConfigurationVulkan config{};
    if(validation) config.enabledLayers={&layer,1};
    LLGL::RenderSystemDescriptor desc{"Vulkan"};
    if(validation) desc.flags=LLGL::RenderSystemFlags::DebugDevice;
    desc.rendererConfig=&config; desc.rendererConfigSize=sizeof(config);
    LLGL::Report report; auto renderer=LLGL::RenderSystem::Load(desc,&report);
    require(renderer!=nullptr,"LLGL Vulkan load failed"); require(!report.HasErrors(),report.GetText());
    require(renderer->GetRendererID()==LLGL::RendererID::Vulkan,"Actual renderer is not Vulkan");
    LLGL::Vulkan::RenderSystemNativeHandle native{};
    require(renderer->GetNativeHandle(&native,sizeof(native)) && native.instance && native.physicalDevice && native.device && native.queue,"Actual native handles absent");
    auto properties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(vkGetInstanceProcAddr(native.instance,"vkGetPhysicalDeviceProperties2"));
    require(properties!=nullptr,"Native properties2 unavailable");
    VkPhysicalDeviceIDProperties ids{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2}; p.pNext=&ids;
    properties(native.physicalDevice,&p);
    require(linuxbench::hex(ids.deviceUUID)==selected.uuid && linuxbench::hex(ids.driverUUID)==selected.driverUuid
        && selected.name==p.properties.deviceName && selected.type==p.properties.deviceType
        && selected.vendor==p.properties.vendorID && selected.device==p.properties.deviceID
        && selected.driver==p.properties.driverVersion && selected.api==p.properties.apiVersion,"Actual LLGL device differs from qualified native target");
    unsigned n{}; vkGetPhysicalDeviceQueueFamilyProperties(native.physicalDevice,&n,nullptr);
    std::vector<VkQueueFamilyProperties> families(n); vkGetPhysicalDeviceQueueFamilyProperties(native.physicalDevice,&n,families.data());
    require(native.queueFamily<n && families[native.queueFamily].queueCount && (families[native.queueFamily].queueFlags&VK_QUEUE_GRAPHICS_BIT),"Actual graphics queue required");
    VkQueue queue{}; vkGetDeviceQueue(native.device,native.queueFamily,0,&queue);
    require(queue==native.queue,"Actual queue index 0 mismatch");
    Messenger messenger;
    if(validation) {
        messenger.instance=native.instance;
        auto create=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(native.instance,"vkCreateDebugUtilsMessengerEXT"));
        messenger.destroy=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(native.instance,"vkDestroyDebugUtilsMessengerEXT"));
        require(create&&messenger.destroy,"Actual debug-utils callbacks unavailable");
        VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        info.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        info.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        info.pfnUserCallback=callback;
        require(create(native.instance,&info,nullptr,&messenger.handle)==VK_SUCCESS,"Actual debug messenger creation failed");
    }
    require(logErrors==0,"LLGL initialization logged errors");
    std::uint64_t verified{}; unsigned rounds{};
    if(smoke) {
        LLGL::BufferDescriptor srcDesc{}; srcDesc.size=4096; srcDesc.bindFlags=LLGL::BindFlags::CopySrc;
        auto dstDesc=srcDesc; dstDesc.bindFlags=LLGL::BindFlags::CopyDst;
        auto src=renderer->CreateBuffer(srcDesc),dst=renderer->CreateBuffer(dstDesc);
        auto commands=renderer->CreateCommandBuffer({}); auto fence=renderer->CreateFence();
        require(src&&dst&&commands&&fence,"Smoke resources absent"); auto& q=*renderer->GetCommandQueue();
        for(unsigned pass=0;pass<3;++pass) {
            std::array<unsigned char,4096> payload{},readback{};
            for(unsigned i=0;i<payload.size();++i) payload[i]=static_cast<unsigned char>((i*17+(i>>8)*31+pass*73)^0x5a);
            renderer->WriteBuffer(*src,0,payload.data(),payload.size());
            commands->Begin(); commands->CopyBuffer(*dst,0,*src,0,payload.size()); commands->End();
            q.Submit(*commands); q.Submit(*fence); require(q.WaitFence(*fence,5'000'000'000ull),"LLGL fence timeout");
            renderer->ReadBuffer(*dst,0,readback.data(),readback.size()); require(payload==readback,"Full changing buffer reference mismatch");
            verified+=readback.size(); ++rounds;
        }
        q.WaitIdle(); renderer->Release(*commands); renderer->Release(*fence); renderer->Release(*dst); renderer->Release(*src);
    }
    if(control) {
        VkPhysicalDeviceProperties2 invalid{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        properties(native.physicalDevice,&invalid); // No GPU command is recorded or submitted.
        require(errors==1 && std::find(diagnosticIds.begin(),diagnosticIds.end(),"VUID-VkPhysicalDeviceProperties2-sType-sType")!=diagnosticIds.end(),"Expected actual validation callback absent");
    }
    unsigned loaderVersion{}; require(vkEnumerateInstanceVersion(&loaderVersion)==VK_SUCCESS,"Loader version unavailable");
    const auto paths=linuxbench::modules();
    const auto core=module(paths,"libLLGL.so"),backend=module(paths,"libLLGL_Vulkan.so");
    std::string loaded;
    if(validation) loaded=module(paths,"libVkLayer_khronos_validation.so");
    else for(const auto& path:paths) require(path.find("VkLayer_khronos_validation")==std::string::npos,"Validation library in plain probe");
    messenger.close(); renderer.reset();
    require(control || (errors==0 && logErrors==0),"Unexpected LLGL/native validation error");
    std::cout<<"{\"schema_version\":1,\"sdk\":\"llgl\",\"task\":\"P3-LBEN-LLGL-readiness\",\"profile_id\":\"llgl-fixed-pin-transfer-src-v1\",\"unmodified_upstream\":false"
        <<",\"source_fingerprint\":"<<quote(GFX_LLGL_FINGERPRINT)<<",\"LLGL_commit\":"<<quote(GFX_LLGL_PIN)
        <<",\"selected\":"<<selected.json()<<",\"environment\":"<<linuxbench::environment()<<",\"mode\":"<<quote(kind)<<",\"operation\":"<<quote(op)
        <<",\"native_handle_verified\":true,\"queue_family\":"<<native.queueFamily<<",\"queue_index\":0,\"queue_flags\":"<<families[native.queueFamily].queueFlags
        <<",\"validation\":"<<(validation?"true":"false")<<",\"control_only\":"<<(control?"true":"false")<<",\"delivered_errors\":"<<errors<<",\"LLGL_log_errors\":"<<logErrors
        <<",\"verified_bytes\":"<<verified<<",\"reference_rounds\":"<<rounds<<",\"submitted_control_work\":0,\"diagnostic_ids\":[";
    for(std::size_t i=0;i<diagnosticIds.size();++i) {if(i)std::cout<<','; std::cout<<quote(diagnosticIds[i]);}
    std::cout<<"],\"loader_path\":"<<quote(query.loaderPath)<<",\"loader_api_version\":"<<loaderVersion<<",\"LLGL_path\":"<<quote(core)<<",\"backend_path\":"<<quote(backend)
        <<",\"loaded_validation_layer\":"<<quote(loaded)<<",\"runtime_modules\":"<<array(paths)
        <<",\"instance_API_request_observed\":false,\"performance_samples\":0,\"samples_ms\":[],\"gpu_ms\":null,\"physical_GPU_accepted\":false,\"workload_correctness_accepted\":false,\"performance_accepted\":false}\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1;}
