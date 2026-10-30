#include "../common/Adapter.hpp"
#include "../linux_workloads/Platform.hpp"
#include "Payload.hpp"
#include <graphics/Graphics.hpp>
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>

#ifndef NDEBUG
#error Release required
#endif
namespace allocbench {
using bench::quote;
template<class T>T take(gfx::Result<T> r){if(!r)throw std::runtime_error(r.error().message+" native="+std::to_string(r.error().nativeCode));return std::move(*r);}
void take(gfx::Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
void sdl(bool b){if(!b)throw std::runtime_error(SDL_GetError());}
template<class T>T* sdl(T* p){if(!p)throw std::runtime_error(SDL_GetError());return p;}
unsigned number(std::string_view s,unsigned lo,unsigned hi){unsigned n{};auto[p,e]=std::from_chars(s.data(),s.data()+s.size(),n);if(e!=std::errc{}||p!=s.data()+s.size()||n<lo||n>hi)throw std::runtime_error("Invalid bounded integer option");return n;}
std::atomic<unsigned> logErrors{};
void SDLCALL log(void*,int,SDL_LogPriority priority,const char* message){if(priority>=SDL_LOG_PRIORITY_ERROR)++logErrors;std::cerr<<"SDL: "<<message<<'\n';}
std::string layer(bool validation){return linuxwork::layer(validation);}
using Times=std::array<double,2>; // Separate CPU API intervals with untimed GPU use in between.
struct Adapter {
    virtual~Adapter()=default;virtual std::string name()const=0;
    virtual Times sample(Work,unsigned)=0;virtual void close()=0;
};
struct SDK final:Adapter {
    std::unique_ptr<gfx::Device>d;bool validation;std::string deviceName;
    SDK(bool v,const std::string& expected):validation(v){
        auto aa=take(gfx::enumerateAdapters(gfx::Backend::Vulkan));auto it=std::find_if(aa.begin(),aa.end(),[&](auto&a){return expected.empty()?!a.softwareAdapter:a.name==expected;});
        if(it==aa.end()||(!expected.empty()&&std::count_if(aa.begin(),aa.end(),[&](auto&a){return a.name==expected;})!=1))throw std::runtime_error("Unique exact SDK GPU required");
        gfx::DeviceDesc desc;desc.backend=gfx::Backend::Vulkan;desc.adapter=it->id;desc.softwareAdapter=it->softwareAdapter;desc.validation=v;d=take(gfx::createDevice(desc));deviceName=d->caps().adapterName;
    }
    ~SDK(){try{close();}catch(...){std::abort();}}
    std::string name()const override{return deviceName;}
    Times sample(Work w,unsigned iteration)override{
        std::vector<gfx::BufferPtr> buffers(w.texture?0:w.count),uploads(w.count),downloads(w.count);
        std::vector<gfx::TexturePtr> textures(w.texture?w.count:0);
        const gfx::BufferDesc bd{bufferBytes,gfx::Memory::Device};const gfx::TextureDesc td{width,height,gfx::Format::RGBA8UNorm};
        auto a=bench::Clock::now();
        for(unsigned i=0;i<w.count;++i){if(w.texture)textures[i]=take(d->createTexture(td));else buffers[i]=take(d->createBuffer(bd));}
        auto b=bench::Clock::now();
        std::vector<std::weak_ptr<gfx::Buffer>> bw;std::vector<std::weak_ptr<gfx::Texture>> tw;
        for(auto& p:buffers)bw.push_back(p);for(auto& p:textures)tw.push_back(p);
        const auto size=w.texture?textures[0]->footprint().size:bufferBytes;
        const auto pitch=w.texture?textures[0]->footprint().rowPitch:0;
        for(unsigned i=0;i<w.count;++i){
            uploads[i]=take(d->createBuffer({size,gfx::Memory::Upload}));downloads[i]=take(d->createBuffer({size,gfx::Memory::Readback}));
            auto data=payload(w,iteration,i);if(w.texture)data=pack(data,pitch);take(uploads[i]->write(0,data));
        }
        auto cmd=take(d->createCommandList());take(cmd->begin());
        for(unsigned i=0;i<w.count;++i){
            if(w.texture){take(cmd->transition(textures[i],gfx::ResourceState::CopyDestination));take(cmd->copyBufferToTexture(uploads[i],textures[i]));take(cmd->transition(textures[i],gfx::ResourceState::CopySource));take(cmd->copyTextureToBuffer(textures[i],downloads[i]));}
            else{take(cmd->transition(buffers[i],gfx::ResourceState::CopyDestination));take(cmd->copyBuffer(uploads[i],buffers[i],bufferBytes));take(cmd->transition(buffers[i],gfx::ResourceState::CopySource));take(cmd->copyBuffer(buffers[i],downloads[i],bufferBytes));}
        }
        take(cmd->end());take(d->queue().wait(take(d->queue().submit(*cmd))));take(d->queue().waitIdle());cmd.reset();
        for(unsigned i=0;i<w.count;++i){auto data=take(downloads[i]->read(0,size));if(w.texture)data=unpack(data,pitch);if(data!=payload(w,iteration,i))throw std::runtime_error("SDK full resource readback mismatch");}
        uploads.clear();downloads.clear();
        auto c=bench::Clock::now();for(auto& p:buffers)p.reset();for(auto& p:textures)p.reset();auto e=bench::Clock::now();
        for(auto& p:bw)if(!p.expired())throw std::runtime_error("SDK buffer wrapper retained");for(auto& p:tw)if(!p.expired())throw std::runtime_error("SDK texture wrapper retained");
        take(d->queue().waitIdle());return{bench::ms(a,b),bench::ms(c,e)};
    }
    void close()override{if(!d)return;take(d->queue().waitIdle());if(validation&&take(d->validationErrorCount()))throw std::runtime_error("SDK validation errors");d.reset();}
};
struct SDL final:Adapter {
    SDL_GPUDevice*d{};std::string deviceName;
    SDL(bool validation,bool low){
        SDL_SetLogOutputFunction(log,nullptr);sdl(SDL_Init(SDL_INIT_VIDEO));auto p=SDL_CreateProperties();sdl(p!=0);
        sdl(SDL_SetStringProperty(p,SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING,"vulkan"));sdl(SDL_SetBooleanProperty(p,SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN,true));sdl(SDL_SetBooleanProperty(p,SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN,validation));sdl(SDL_SetBooleanProperty(p,SDL_PROP_GPU_DEVICE_CREATE_PREFERLOWPOWER_BOOLEAN,low));
        SDL_GPUVulkanOptions options{};options.vulkan_api_version=VK_API_VERSION_1_1;sdl(SDL_SetPointerProperty(p,SDL_PROP_GPU_DEVICE_CREATE_VULKAN_OPTIONS_POINTER,&options));
        d=SDL_CreateGPUDeviceWithProperties(p);SDL_DestroyProperties(p);if(!d){SDL_Quit();throw std::runtime_error(SDL_GetError());}
        if(std::string_view(SDL_GetGPUDeviceDriver(d))!="vulkan")throw std::runtime_error("SDL backend fallback");deviceName=SDL_GetStringProperty(SDL_GetGPUDeviceProperties(d),SDL_PROP_GPU_DEVICE_NAME_STRING,"");
    }
    ~SDL(){try{close();}catch(...){std::abort();}}
    std::string name()const override{return deviceName;}
    void complete(SDL_GPUCommandBuffer*c){auto f=sdl(SDL_SubmitGPUCommandBufferAndAcquireFence(c));bool ok=SDL_WaitForGPUFences(d,true,&f,1);SDL_ReleaseGPUFence(d,f);sdl(ok);}
    Times sample(Work w,unsigned iteration)override{
        std::vector<SDL_GPUBuffer*>buffers(w.texture?0:w.count);std::vector<SDL_GPUTexture*>textures(w.texture?w.count:0);
        std::vector<SDL_GPUTransferBuffer*>uploads(w.count),downloads(w.count);
        SDL_GPUBufferCreateInfo bd{SDL_GPU_BUFFERUSAGE_VERTEX,bufferBytes,0};
        SDL_GPUTextureCreateInfo td{};td.type=SDL_GPU_TEXTURETYPE_2D;td.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;td.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER;td.width=width;td.height=height;td.layer_count_or_depth=1;td.num_levels=1;
        auto a=bench::Clock::now();for(unsigned i=0;i<w.count;++i){if(w.texture)textures[i]=sdl(SDL_CreateGPUTexture(d,&td));else buffers[i]=sdl(SDL_CreateGPUBuffer(d,&bd));}auto b=bench::Clock::now();
        SDL_GPUTransferBufferCreateInfo up{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,bytes(w),0},down{SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,bytes(w),0};
        for(unsigned i=0;i<w.count;++i){uploads[i]=sdl(SDL_CreateGPUTransferBuffer(d,&up));downloads[i]=sdl(SDL_CreateGPUTransferBuffer(d,&down));auto data=payload(w,iteration,i);auto p=sdl(SDL_MapGPUTransferBuffer(d,uploads[i],false));std::memcpy(p,data.data(),data.size());SDL_UnmapGPUTransferBuffer(d,uploads[i]);}
        auto cmd=sdl(SDL_AcquireGPUCommandBuffer(d));auto pass=sdl(SDL_BeginGPUCopyPass(cmd));
        for(unsigned i=0;i<w.count;++i){
            if(w.texture){SDL_GPUTextureRegion region{};region.texture=textures[i];region.w=width;region.h=height;region.d=1;SDL_GPUTextureTransferInfo from{uploads[i],0,width,height},to{downloads[i],0,width,height};SDL_UploadToGPUTexture(pass,&from,&region,false);SDL_DownloadFromGPUTexture(pass,&region,&to);}
            else{SDL_GPUBufferRegion region{buffers[i],0,bufferBytes};SDL_GPUTransferBufferLocation from{uploads[i],0},to{downloads[i],0};SDL_UploadToGPUBuffer(pass,&from,&region,false);SDL_DownloadFromGPUBuffer(pass,&region,&to);}
        }
        SDL_EndGPUCopyPass(pass);complete(cmd);sdl(SDL_WaitForGPUIdle(d));
        for(unsigned i=0;i<w.count;++i){auto p=sdl(SDL_MapGPUTransferBuffer(d,downloads[i],false));auto data=payload(w,iteration,i);bool match=std::memcmp(p,data.data(),data.size())==0;SDL_UnmapGPUTransferBuffer(d,downloads[i]);if(!match)throw std::runtime_error("SDL full resource readback mismatch");SDL_ReleaseGPUTransferBuffer(d,uploads[i]);SDL_ReleaseGPUTransferBuffer(d,downloads[i]);}
        auto c=bench::Clock::now();for(auto p:buffers)SDL_ReleaseGPUBuffer(d,p);for(auto p:textures)SDL_ReleaseGPUTexture(d,p);auto e=bench::Clock::now();
        sdl(SDL_WaitForGPUIdle(d));return{bench::ms(a,b),bench::ms(c,e)};
    }
    void close()override{if(!d)return;sdl(SDL_WaitForGPUIdle(d));SDL_DestroyGPUDevice(d);d={};SDL_Quit();if(logErrors)throw std::runtime_error("SDL logged errors");}
};
}
int main(int argc,char**argv)try{
    using namespace allocbench;const bool probe=(argc==4||argc==5)&&std::string_view(argv[1])=="--probe";
    if(!probe&&argc!=8)throw std::runtime_error("Usage: vulkan_allocation <graphicssdk|sdl> <buffer1|buffer1000|texture1|texture1000> <high|low> <warmup 1..64> <samples 3..256> <validation 0|1> <expected GPU>; or --probe <sdk> <high|low> [expected GPU]");
    const std::string sdk=argv[probe?2:1],preference=argv[3];if((sdk!="sdl"&&sdk!="graphicssdk")||(preference!="high"&&preference!="low"))throw std::runtime_error("Usage: invalid SDK/preference");
    Work w=probe?Work{}:work(argv[2]);unsigned warmup=probe?0:number(argv[4],1,64),samples=probe?0:number(argv[5],3,256);bool validation=!probe&&number(argv[6],0,1);const std::string expected=probe?(argc==5?argv[4]:""):argv[7];if((!probe||argc==5)&&expected.empty())throw std::runtime_error("Expected GPU name required");
    linuxwork::guard();
    if(validation){SDK required(true,expected);required.close();}
    std::unique_ptr<Adapter>a;if(sdk=="sdl")a=std::make_unique<SDL>(validation,preference=="low");else a=std::make_unique<SDK>(validation,expected);
    auto name=a->name();if(!expected.empty()&&name!=expected)throw std::runtime_error("Selected GPU differs from expected GPU");auto id=upload::identify(name);auto env=bench::environment(name);auto loaded=layer(validation);std::vector<Times> measured;
    if(!probe)for(unsigned i=0;i<warmup+samples;++i){auto result=a->sample(w,i);if(i>=warmup)measured.push_back(result);}
    a->close();if(logErrors)throw std::runtime_error("SDL teardown errors");
    auto resources=std::uint64_t(warmup+samples)*w.count;
    std::cout<<std::setprecision(17)<<"{\"schema_version\":1,\"sdk\":"<<quote(sdk)<<",\"api\":\"vulkan\",\"category\":\"cpu_api\",\"contract\":\"vulkan_resource_create_public_release_full_readback_v1\",\"probe\":"<<(probe?"true":"false")
        <<",\"scenario\":"<<quote(probe?"probe":argv[2])<<",\"preference\":"<<quote(preference)<<",\"configuration\":\"Release\",\"validation\":"<<(validation?"true":"false")<<",\"source_fingerprint\":"<<quote(GFX_VK_ALLOCATION_FINGERPRINT)<<",\"SDK_source_fingerprint\":"<<quote(gfx::buildFingerprint())<<",\"SDL_commit\":"<<quote(GFX_SDL_PIN)
        <<",\"requested_instance_api_version\":4198400,\"device_name\":"<<quote(name)<<",\"device_uuid\":"<<quote(id.uuid)<<",\"driver_uuid\":"<<quote(id.driverUuid)<<",\"device_luid\":"<<"null"<<",\"native_vendor_id\":"<<id.vendor<<",\"native_device_id\":"<<id.device<<",\"native_driver_version\":"<<id.driver<<",\"native_api_version\":"<<id.api
        <<",\"identity_attribution\":\"unique_exact_name_to_native_vulkan_not_SDL_native_handle\",\"environment\":"<<env<<",\"loaded_validation_layer\":"<<quote(loaded)<<",\"warmup\":"<<warmup<<",\"sample_count\":"<<samples<<",\"width\":"<<(w.texture?width:0)<<",\"height\":"<<(w.texture?height:0)<<",\"texel_bytes\":"<<(w.texture?4:0)<<",\"buffer_bytes\":"<<(w.texture?0:bufferBytes)
        <<",\"resource_kind\":"<<quote(w.texture?"texture":"buffer")<<",\"resources_per_iteration\":"<<w.count<<",\"created_resources\":"<<resources<<",\"released_resource_handles\":"<<resources<<",\"used_resources\":"<<resources<<",\"verified_resources\":"<<resources<<",\"verified_images\":"<<(w.texture?resources:0)<<",\"verified_buffers\":"<<(w.texture?0:resources)<<",\"verified_bytes\":"<<resources*bytes(w)
        <<",\"readback_verified\":"<<(probe?"false":"true")<<",\"allocation_policy\":\"public_device_resource_default_policy\",\"usage_policy\":"<<quote(w.texture?"sampled_rgba8_2d":sdk=="sdl"?"sdl_vertex_buffer":"sdk_general_device_buffer")<<",\"GPU_use_in_timing\":false,\"GPU_memory_reclamation_verified\":false,\"native_allocation_count_verified\":false,\"gpu_ms\":null,\"columns\":[\"create_ms\",\"public_release_ms\"],\"samples_ms\":[";
    for(std::size_t i=0;i<measured.size();++i){if(i)std::cout<<',';std::cout<<'['<<measured[i][0]<<','<<measured[i][1]<<']';}std::cout<<"]}\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
