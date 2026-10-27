#include "../common/LinuxEnvironment.hpp"
#include <graphics/Graphics.hpp>
#include <SDL3/SDL.h>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <type_traits>
#ifndef NDEBUG
#error Release probe required
#endif
namespace {
using namespace linuxbench;
std::atomic<unsigned> errors{};
void SDLCALL log(void*,int,SDL_LogPriority priority,const char* message){if(priority>=SDL_LOG_PRIORITY_ERROR)++errors;std::cerr<<"SDL: "<<message<<'\n';}
template<class T>T take(gfx::Result<T> value){if(!value)throw std::runtime_error(value.error().message);if constexpr(!std::is_void_v<T>)return std::move(*value);}
template<class T>T* sdl(T* value){if(!value)throw std::runtime_error(SDL_GetError());return value;}
void sdl(bool value){if(!value)throw std::runtime_error(SDL_GetError());}
struct SDLSession {SDL_GPUDevice* device{};~SDLSession(){if(device)SDL_DestroyGPUDevice(device);SDL_Quit();}};
}
int main(int argc,char** argv)try{
    using namespace linuxbench;const bool inventoryOnly=argc==2&&std::string_view(argv[1])=="inventory";
    require(inventoryOnly||(argc==6&&std::string_view(argv[1])=="probe"),"Usage: linux_comparison inventory | probe <graphicssdk|sdl> <uuid> <0|1> <physical|software-diagnostic>");
    std::string sdk=inventoryOnly?"native":argv[2],mode=inventoryOnly?"inventory":argv[5];
    require(inventoryOnly||((sdk=="graphicssdk"||sdk=="sdl")&&(std::string_view(argv[4])=="0"||std::string_view(argv[4])=="1")&&(mode=="physical"||mode=="software-diagnostic")),"Invalid bounded probe option");
    bool validation=!inventoryOnly&&std::string_view(argv[4])=="1";NativeQuery native;const auto devices=native.enumerate(validation);
    std::string selected="null",loadedLayer;std::unique_ptr<gfx::Device> device;SDLSession session;
    if(!inventoryOnly){
        auto identity=select(devices,argv[3],mode);selected=identity.json();
        if(sdk=="graphicssdk"){
            auto adapters=take(gfx::enumerateAdapters(gfx::Backend::Vulkan));
            require(std::count_if(adapters.begin(),adapters.end(),[&](auto& a){return a.id.value==identity.id;})==1,"SDK UUID-derived identity absent or ambiguous");
            auto a=*std::find_if(adapters.begin(),adapters.end(),[&](auto& a){return a.id.value==identity.id;});
            require(a.name==identity.name&&a.vendorId==identity.vendor&&a.deviceId==identity.device&&a.softwareAdapter==(identity.type==4),"SDK/native identity mismatch");
            gfx::DeviceDesc desc;desc.backend=gfx::Backend::Vulkan;desc.adapter=a.id;desc.softwareAdapter=a.softwareAdapter;desc.validation=validation;
            device=take(gfx::createDevice(desc));require(device->caps().adapterId==a.id&&device->caps().adapterName==identity.name&&device->caps().softwareAdapter==a.softwareAdapter,"SDK exact selection changed");
            take(device->queue().waitIdle());if(validation)require(take(device->validationErrorCount())==0,"SDK validation errors");
        }else{
            SDL_SetLogOutputFunction(log,nullptr);sdl(SDL_Init(SDL_INIT_VIDEO));auto properties=SDL_CreateProperties();require(properties!=0,"SDL properties unavailable");
            sdl(SDL_SetStringProperty(properties,SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING,"vulkan"));
            sdl(SDL_SetBooleanProperty(properties,SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN,true));sdl(SDL_SetBooleanProperty(properties,SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN,validation));
            sdl(SDL_SetBooleanProperty(properties,SDL_PROP_GPU_DEVICE_CREATE_PREFERLOWPOWER_BOOLEAN,false));
            SDL_GPUVulkanOptions options{};options.vulkan_api_version=VK_API_VERSION_1_1;sdl(SDL_SetPointerProperty(properties,SDL_PROP_GPU_DEVICE_CREATE_VULKAN_OPTIONS_POINTER,&options));
            session.device=SDL_CreateGPUDeviceWithProperties(properties);SDL_DestroyProperties(properties);sdl(session.device);
            require(std::string_view(SDL_GetGPUDeviceDriver(session.device))=="vulkan","SDL backend fallback");
            require(identity.name==SDL_GetStringProperty(SDL_GetGPUDeviceProperties(session.device),SDL_PROP_GPU_DEVICE_NAME_STRING,""),"SDL selected device differs; no fallback accepted");sdl(SDL_WaitForGPUIdle(session.device));
        }
    }
    auto paths=modules();for(auto& path:paths)if(std::filesystem::path(path).filename()=="libVkLayer_khronos_validation.so"){require(loadedLayer.empty(),"Ambiguous loaded validation library");loadedLayer=path;}
    if(validation){auto expected=std::getenv("GFX_EXPECT_VULKAN_LAYER");require(expected&&loadedLayer==std::filesystem::canonical(expected).string(),"Pinned validation layer not loaded");}
    else require(loadedLayer.empty(),"Validation active in plain probe");
    device.reset();if(session.device){SDL_DestroyGPUDevice(session.device);session.device=nullptr;SDL_Quit();}native.close();require(errors==0,"SDL validation/log errors");
    std::cout<<"{\"schema_version\":1,\"task\":\"P3-BEN-07\",\"contract\":\"linux_native_uuid_public_name_probe_v1\",\"sdk\":"<<quote(sdk)<<",\"mode\":"<<quote(mode)
        <<",\"probe\":true,\"validation\":"<<(validation?"true":"false")<<",\"logical_device_created\":"<<(inventoryOnly?"false":"true")
        <<",\"source_fingerprint\":"<<quote(GFX_LINUX_COMPARISON_FINGERPRINT)<<",\"SDK_source_fingerprint\":"<<quote(gfx::buildFingerprint())<<",\"SDL_commit\":"<<quote(GFX_SDL_PIN)
        <<",\"identity_attribution\":\"unique_exact_name_to_native_vulkan_not_SDL_native_handle\",\"SDL_native_handle_verified\":false,\"selected\":"<<selected<<",\"inventory\":"<<inventory(devices)
        <<",\"loader_path\":"<<quote(native.loaderPath)<<",\"loaded_validation_layer\":"<<quote(loadedLayer)<<",\"environment\":"<<environment()
        <<",\"validation_error_count\":0,\"sample_count\":0,\"samples_ms\":[],\"gpu_ms\":null,\"readback_verified\":false,\"benchmark_accepted\":false,\"runtime_modules\":[";
    bool first=true;for(auto& path:paths){if(!first)std::cout<<',';first=false;std::cout<<quote(path);}std::cout<<"]}\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
