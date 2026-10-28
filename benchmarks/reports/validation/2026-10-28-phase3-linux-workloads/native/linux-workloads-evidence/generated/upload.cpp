#include "../common/Adapter.hpp"
#include "../linux_workloads/Platform.hpp"
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
#error This benchmark requires Release mode
#endif

namespace upload {
using bench::quote;
template<class T>T take(gfx::Result<T> r){if(!r)throw std::runtime_error(r.error().message+" native="+std::to_string(r.error().nativeCode));return std::move(*r);}
void take(gfx::Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
void sdl(bool b){if(!b)throw std::runtime_error(SDL_GetError());}
template<class T>T* sdl(T* p){if(!p)throw std::runtime_error(SDL_GetError());return p;}
struct Shape{unsigned w{},h{},bpp{};};
Shape shape(std::string_view s){if(s=="odd")return{65,37,4};if(s=="upload1080")return{1920,1080,4};if(s=="upload4k")return{3840,2160,4};if(s=="upload4k16f")return{3840,2160,8};throw std::runtime_error("Unsupported scenario");}
unsigned number(std::string_view s,unsigned lo,unsigned hi){unsigned n{};auto [end,e]=std::from_chars(s.data(),s.data()+s.size(),n);if(e!=std::errc{}||end!=s.data()+s.size()||n<lo||n>hi)throw std::runtime_error("Invalid bounded integer option");return n;}
std::atomic<unsigned> logErrors{};
void SDLCALL log(void*,int,SDL_LogPriority priority,const char* message){if(priority>=SDL_LOG_PRIORITY_ERROR)++logErrors;std::cerr<<"SDL: "<<message<<'\n';}
std::string layer(bool validation){return linuxwork::layer(validation);}
struct Adapter{
    virtual~Adapter()=default;
    virtual std::string name()const=0;
    virtual void prepare(Shape)=0;
    virtual void write(std::span<const std::byte>)=0;
    virtual std::vector<std::byte> read()=0;
    virtual void close()=0;
};
struct SDK final:Adapter{
    std::unique_ptr<gfx::Device>d;gfx::TexturePtr texture;gfx::BufferPtr staging;std::unique_ptr<gfx::CommandList>cmd;bool validation;std::string deviceName;
    SDK(bool v,const std::string& expected):validation(v){
        auto candidates=take(gfx::enumerateAdapters(gfx::Backend::Vulkan));auto it=std::find_if(candidates.begin(),candidates.end(),[&](auto& a){return expected.empty()?!a.softwareAdapter:a.name==expected;});
        if(it==candidates.end()||(!expected.empty()&&std::count_if(candidates.begin(),candidates.end(),[&](auto& a){return a.name==expected;})!=1))throw std::runtime_error("Exact unambiguous SDK adapter required");
        gfx::DeviceDesc desc;desc.backend=gfx::Backend::Vulkan;desc.validation=v;desc.adapter=it->id;desc.softwareAdapter=it->softwareAdapter;d=take(gfx::createDevice(desc));deviceName=d->caps().adapterName;
    }
    ~SDK(){try{close();}catch(...){std::abort();}}
    std::string name()const override{return deviceName;}
    void prepare(Shape s)override{texture=take(d->createTexture({s.w,s.h,s.bpp==8?gfx::Format::RGBA16Float:gfx::Format::RGBA8UNorm}));staging=take(d->createBuffer({texture->footprint().size,gfx::Memory::Upload}));cmd=take(d->createCommandList());}
    void write(std::span<const std::byte> pixels)override{
        const auto f=texture->footprint();
        if(f.rowPitch==f.rowBytes)take(staging->write(0,pixels));else{std::vector<std::byte> padded(static_cast<size_t>(f.size));for(unsigned y=0;y<f.height;++y)std::memcpy(padded.data()+std::size_t(y)*f.rowPitch,pixels.data()+std::size_t(y)*f.rowBytes,f.rowBytes);take(staging->write(0,padded));}
        take(cmd->begin());take(cmd->transition(texture,gfx::ResourceState::CopyDestination));take(cmd->copyBufferToTexture(staging,texture));take(cmd->end());take(d->queue().wait(take(d->queue().submit(*cmd))));
    }
    std::vector<std::byte> read()override{
        const auto f=texture->footprint();auto r=take(d->createBuffer({f.size,gfx::Memory::Readback}));auto c=take(d->createCommandList());take(c->begin());take(c->transition(texture,gfx::ResourceState::CopySource));take(c->copyTextureToBuffer(texture,r));take(c->end());take(d->queue().wait(take(d->queue().submit(*c))));auto padded=take(r->read(0,f.size));std::vector<std::byte> tight(std::size_t(f.rowBytes)*f.height);
        for(unsigned y=0;y<f.height;++y)std::memcpy(tight.data()+std::size_t(y)*f.rowBytes,padded.data()+std::size_t(y)*f.rowPitch,f.rowBytes);return tight;
    }
    void close()override{if(!d)return;take(d->queue().waitIdle());cmd.reset();texture.reset();staging.reset();if(validation&&take(d->validationErrorCount()))throw std::runtime_error("SDK Vulkan validation errors");d.reset();}
};
struct SDL final:Adapter{
    SDL_GPUDevice*d{};SDL_GPUTexture*texture{};SDL_GPUTransferBuffer*staging{};Shape size{};std::string deviceName;
    SDL(bool validation,bool low){
        SDL_SetLogOutputFunction(log,nullptr);sdl(SDL_Init(SDL_INIT_VIDEO));auto p=SDL_CreateProperties();sdl(p!=0);
        sdl(SDL_SetStringProperty(p,SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING,"vulkan"));sdl(SDL_SetBooleanProperty(p,SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN,true));
        sdl(SDL_SetBooleanProperty(p,SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN,validation));sdl(SDL_SetBooleanProperty(p,SDL_PROP_GPU_DEVICE_CREATE_PREFERLOWPOWER_BOOLEAN,low));
        d=SDL_CreateGPUDeviceWithProperties(p);SDL_DestroyProperties(p);if(!d){SDL_Quit();throw std::runtime_error(SDL_GetError());}
        if(std::string_view(SDL_GetGPUDeviceDriver(d))!="vulkan")throw std::runtime_error("SDL backend fallback rejected");
        deviceName=SDL_GetStringProperty(SDL_GetGPUDeviceProperties(d),SDL_PROP_GPU_DEVICE_NAME_STRING,"");
    }
    ~SDL(){try{close();}catch(...){std::abort();}}
    std::string name()const override{return deviceName;}
    void prepare(Shape s)override{size=s;SDL_GPUTextureCreateInfo t{};t.type=SDL_GPU_TEXTURETYPE_2D;t.format=s.bpp==8?SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT:SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;t.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER;t.width=s.w;t.height=s.h;t.layer_count_or_depth=1;t.num_levels=1;texture=sdl(SDL_CreateGPUTexture(d,&t));SDL_GPUTransferBufferCreateInfo u{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,s.w*s.h*s.bpp,0};staging=sdl(SDL_CreateGPUTransferBuffer(d,&u));}
    void complete(SDL_GPUCommandBuffer*c){auto f=sdl(SDL_SubmitGPUCommandBufferAndAcquireFence(c));bool ok=SDL_WaitForGPUFences(d,true,&f,1);SDL_ReleaseGPUFence(d,f);sdl(ok);}
    void write(std::span<const std::byte> pixels)override{
        auto mapped=sdl(SDL_MapGPUTransferBuffer(d,staging,false));std::memcpy(mapped,pixels.data(),pixels.size());SDL_UnmapGPUTransferBuffer(d,staging);
        auto c=sdl(SDL_AcquireGPUCommandBuffer(d));auto p=sdl(SDL_BeginGPUCopyPass(c));SDL_GPUTextureTransferInfo from{staging,0,size.w,size.h};SDL_GPUTextureRegion to{};to.texture=texture;to.w=size.w;to.h=size.h;to.d=1;
        SDL_UploadToGPUTexture(p,&from,&to,false);SDL_EndGPUCopyPass(p);complete(c);
    }
    std::vector<std::byte> read()override{
        SDL_GPUTransferBufferCreateInfo t{SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,size.w*size.h*size.bpp,0};auto r=sdl(SDL_CreateGPUTransferBuffer(d,&t));
        auto c=sdl(SDL_AcquireGPUCommandBuffer(d));auto p=sdl(SDL_BeginGPUCopyPass(c));SDL_GPUTextureRegion from{};from.texture=texture;from.w=size.w;from.h=size.h;from.d=1;SDL_GPUTextureTransferInfo to{r,0,size.w,size.h};SDL_DownloadFromGPUTexture(p,&from,&to);SDL_EndGPUCopyPass(p);complete(c);
        auto mapped=sdl(SDL_MapGPUTransferBuffer(d,r,false));std::vector<std::byte> result(t.size);std::memcpy(result.data(),mapped,result.size());SDL_UnmapGPUTransferBuffer(d,r);SDL_ReleaseGPUTransferBuffer(d,r);return result;
    }
    void close()override{if(!d)return;sdl(SDL_WaitForGPUIdle(d));if(texture)SDL_ReleaseGPUTexture(d,texture);if(staging)SDL_ReleaseGPUTransferBuffer(d,staging);texture={};staging={};SDL_DestroyGPUDevice(d);d={};SDL_Quit();if(logErrors)throw std::runtime_error("SDL logged GPU errors");}
};
std::unique_ptr<Adapter> adapter(std::string_view sdk,bool validation,bool low,const std::string& expected){if(sdk=="sdl")return std::make_unique<SDL>(validation,low);return std::make_unique<SDK>(validation,expected);}
void payload(std::vector<std::byte>& data,Shape s,unsigned seed){
    for(std::size_t i=0;i<std::size_t(s.w)*s.h;++i){auto v=std::uint32_t(i)*1315423911u+seed*2654435761u;v^=v>>16;
        if(s.bpp==4)std::memcpy(data.data()+i*4,&v,4);else{constexpr std::uint16_t halves[]{0,0x3400,0x3800,0x3c00,0xb400,0x4000,0x4200,0x0400};for(unsigned k=0;k<4;++k){auto h=halves[(v>>(k*3))&7];std::memcpy(data.data()+i*8+k*2,&h,2);}}
    }
}
}
int main(int argc,char** argv)try{
    using namespace upload;
    const bool probe=(argc==4||argc==5)&&std::string_view(argv[1])=="--probe";
    if(!probe&&argc!=8)throw std::runtime_error("Usage: vulkan_upload <graphicssdk|sdl> <odd|upload1080|upload4k|upload4k16f> <high|low> <warmup 1..64> <samples 3..256> <validation 0|1> <expected GPU>; or --probe <sdk> <high|low> [expected GPU]");
    const std::string sdk=argv[probe?2:1],preference=argv[3];
    if((sdk!="sdl"&&sdk!="graphicssdk")||(preference!="high"&&preference!="low"))throw std::runtime_error("Usage: unsupported SDK/preference");
    const auto s=probe?Shape{}:shape(argv[2]);const auto warmup=probe?0:number(argv[4],1,64),samples=probe?0:number(argv[5],3,256);const bool validation=!probe&&number(argv[6],0,1);const std::string expected=probe?(argc==5?argv[4]:""):argv[7];
    if((!probe||argc==5)&&expected.empty())throw std::runtime_error("Expected GPU name is required");
    linuxwork::guard();
    if(validation){SDK required(true,expected);required.close();}
    auto a=adapter(sdk,validation,preference=="low",expected);auto name=a->name();
    if(!expected.empty()&&name!=expected)throw std::runtime_error("Selected GPU differs from expected GPU");
    auto identity=identify(name);auto env=bench::environment(name);const auto loadedLayer=layer(validation);
    std::vector<double> measured;std::uint64_t verifiedBytes{};
    if(!probe){a->prepare(s);std::vector<std::byte> bytes(std::size_t(s.w)*s.h*s.bpp);
        for(unsigned i=0;i<warmup+samples;++i){payload(bytes,s,i+1);auto start=bench::Clock::now();a->write(bytes);auto end=bench::Clock::now();auto actual=a->read();if(actual!=bytes)throw std::runtime_error("Full changing-payload readback mismatch at iteration "+std::to_string(i));verifiedBytes+=bytes.size();if(i>=warmup)measured.push_back(bench::ms(start,end));}
    }
    a->close();if(logErrors)throw std::runtime_error("SDL logged errors during teardown");
    std::cout<<std::setprecision(17)<<"{\"schema_version\":1,\"sdk\":"<<quote(sdk)<<",\"api\":\"vulkan\",\"category\":\"end_to_end\",\"contract\":\"vulkan_upload_cpu_payload_to_texture_fence_complete_each_sample_readback_v1\",\"probe\":"<<(probe?"true":"false")
        <<",\"scenario\":"<<quote(probe?"probe":argv[2])<<",\"preference\":"<<quote(preference)<<",\"configuration\":\"Release\",\"validation\":"<<(validation?"true":"false")<<",\"source_fingerprint\":"<<quote(GFX_VK_UPLOAD_FINGERPRINT)<<",\"SDK_source_fingerprint\":"<<quote(gfx::buildFingerprint())<<",\"SDL_commit\":"<<quote(GFX_SDL_PIN)
        <<",\"device_name\":"<<quote(name)<<",\"device_uuid\":"<<quote(identity.uuid)<<",\"driver_uuid\":"<<quote(identity.driverUuid)<<",\"device_luid\":"<<"null"<<",\"native_vendor_id\":"<<identity.vendor<<",\"native_device_id\":"<<identity.device<<",\"native_driver_version\":"<<identity.driver<<",\"native_api_version\":"<<identity.api
        <<",\"identity_attribution\":\"unique_exact_name_to_native_vulkan_not_SDL_native_handle\",\"environment\":"<<env<<",\"loaded_validation_layer\":"<<quote(loadedLayer)<<",\"warmup\":"<<warmup<<",\"sample_count\":"<<samples<<",\"width\":"<<s.w<<",\"height\":"<<s.h<<",\"texel_bytes\":"<<s.bpp<<",\"verified_images\":"<<warmup+samples<<",\"verified_bytes\":"<<verifiedBytes<<",\"readback_verified\":"<<(probe?"false":"true")<<",\"gpu_ms\":null,\"samples_ms\":[";
    for(std::size_t i=0;i<measured.size();++i){if(i)std::cout<<',';std::cout<<measured[i];}std::cout<<"]}\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
