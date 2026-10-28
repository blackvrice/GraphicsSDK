#include "../common/Adapter.hpp"
#include "../linux_workloads/Platform.hpp"
#include "../../tests/fixtures/SpirvFixtures.hpp"
#include "Frame.hpp"
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
namespace psobench {
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
    virtual~Adapter()=default;virtual std::string name()const=0;virtual void prepare()=0;
    virtual Times sample(unsigned,unsigned)=0;virtual std::vector<std::byte> read()=0;virtual void close()=0;
};
struct SDK final:Adapter {
    std::unique_ptr<gfx::Device>d;gfx::TexturePtr texture;bool validation;std::string deviceName;
    SDK(bool v,const std::string& expected):validation(v){
        auto aa=take(gfx::enumerateAdapters(gfx::Backend::Vulkan));auto it=std::find_if(aa.begin(),aa.end(),[&](auto&a){return expected.empty()?!a.softwareAdapter:a.name==expected;});
        if(it==aa.end()||(!expected.empty()&&std::count_if(aa.begin(),aa.end(),[&](auto&a){return a.name==expected;})!=1))throw std::runtime_error("Unique exact SDK GPU required");
        gfx::DeviceDesc desc;desc.backend=gfx::Backend::Vulkan;desc.adapter=it->id;desc.softwareAdapter=it->softwareAdapter;desc.validation=v;d=take(gfx::createDevice(desc));deviceName=d->caps().adapterName;
    }
    ~SDK(){try{close();}catch(...){std::abort();}}
    std::string name()const override{return deviceName;}
    void prepare()override{texture=take(d->createTexture({width,height,gfx::Format::RGBA8UNorm,true}));}
    Times sample(unsigned count,unsigned iteration)override{
        auto binary=[](std::span<const std::uint32_t> words,gfx::ShaderStage stage){auto b=std::as_bytes(words);return gfx::ShaderBinary{stage,{b.begin(),b.end()},gfx::ShaderFormat::SPIRV};};
        std::vector<gfx::GraphicsPipelineDesc> descriptions(count);
        for(unsigned i=0;i<count;++i){auto& p=descriptions[i];p.vertex=binary(fixtures::fullscreen,gfx::ShaderStage::Vertex);p.fragment=binary((iteration+i)%2?std::span<const std::uint32_t>(fixtures::green):std::span<const std::uint32_t>(fixtures::red),gfx::ShaderStage::Fragment);p.renderTargets=gfx::RenderTargetLayout{{gfx::Format::RGBA8UNorm},{}};}
        std::vector<gfx::PipelinePtr> pipelines(count);
        auto a=bench::Clock::now();for(unsigned i=0;i<count;++i)pipelines[i]=take(d->createGraphicsPipeline(descriptions[i]));auto b=bench::Clock::now();
        std::vector<std::weak_ptr<gfx::Pipeline>> wrappers;for(auto& p:pipelines)wrappers.push_back(p);
        auto cmd=take(d->createCommandList());take(cmd->begin());take(cmd->transition(texture,gfx::ResourceState::RenderTarget));take(cmd->beginRenderPass(texture,{0,iteration%2?1.f:0.f,1,1}));
        for(unsigned i=0;i<count;++i){take(cmd->setPipeline(pipelines[i]));take(cmd->setScissor(edge(i,count),top,edge(i+1,count)-edge(i,count),bottom-top));take(cmd->draw(3,1,0,0));}
        take(cmd->endRenderPass());take(cmd->end());take(d->queue().wait(take(d->queue().submit(*cmd))));take(d->queue().waitIdle());cmd.reset();
        auto c=bench::Clock::now();for(auto& p:pipelines)p.reset();auto e=bench::Clock::now();
        for(auto& p:wrappers)if(!p.expired())throw std::runtime_error("Public SDK pipeline wrapper retained");
        take(d->queue().waitIdle());return{bench::ms(a,b),bench::ms(c,e)};
    }
    std::vector<std::byte> read()override{
        auto f=texture->footprint();auto r=take(d->createBuffer({f.size,gfx::Memory::Readback}));auto c=take(d->createCommandList());take(c->begin());take(c->transition(texture,gfx::ResourceState::CopySource));take(c->copyTextureToBuffer(texture,r));take(c->end());take(d->queue().wait(take(d->queue().submit(*c))));auto padded=take(r->read(0,f.size));std::vector<std::byte> tight(width*height*4);
        for(unsigned y=0;y<height;++y)std::memcpy(tight.data()+y*width*4,padded.data()+y*f.rowPitch,width*4);return tight;
    }
    void close()override{if(!d)return;take(d->queue().waitIdle());texture.reset();if(validation&&take(d->validationErrorCount()))throw std::runtime_error("SDK validation errors");d.reset();}
};
struct SDL final:Adapter {
    SDL_GPUDevice*d{};SDL_GPUTexture*texture{};std::string deviceName;
    SDL(bool validation,bool low){
        SDL_SetLogOutputFunction(log,nullptr);sdl(SDL_Init(SDL_INIT_VIDEO));auto p=SDL_CreateProperties();sdl(p!=0);
        sdl(SDL_SetStringProperty(p,SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING,"vulkan"));sdl(SDL_SetBooleanProperty(p,SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN,true));sdl(SDL_SetBooleanProperty(p,SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN,validation));sdl(SDL_SetBooleanProperty(p,SDL_PROP_GPU_DEVICE_CREATE_PREFERLOWPOWER_BOOLEAN,low));
        SDL_GPUVulkanOptions options{};options.vulkan_api_version=VK_API_VERSION_1_1;sdl(SDL_SetPointerProperty(p,SDL_PROP_GPU_DEVICE_CREATE_VULKAN_OPTIONS_POINTER,&options));
        d=SDL_CreateGPUDeviceWithProperties(p);SDL_DestroyProperties(p);if(!d){SDL_Quit();throw std::runtime_error(SDL_GetError());}
        if(std::string_view(SDL_GetGPUDeviceDriver(d))!="vulkan")throw std::runtime_error("SDL backend fallback");deviceName=SDL_GetStringProperty(SDL_GetGPUDeviceProperties(d),SDL_PROP_GPU_DEVICE_NAME_STRING,"");
    }
    ~SDL(){try{close();}catch(...){std::abort();}}
    std::string name()const override{return deviceName;}
    void prepare()override{
        SDL_GPUTextureCreateInfo t{};t.type=SDL_GPU_TEXTURETYPE_2D;t.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;t.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;t.width=width;t.height=height;t.layer_count_or_depth=1;t.num_levels=1;texture=sdl(SDL_CreateGPUTexture(d,&t));
    }
    void complete(SDL_GPUCommandBuffer*c){auto f=sdl(SDL_SubmitGPUCommandBufferAndAcquireFence(c));bool ok=SDL_WaitForGPUFences(d,true,&f,1);SDL_ReleaseGPUFence(d,f);sdl(ok);}
    Times sample(unsigned count,unsigned iteration)override{
        SDL_GPUShaderCreateInfo vs{};vs.code_size=fixtures::fullscreen.size()*4;vs.code=reinterpret_cast<const Uint8*>(fixtures::fullscreen.data());vs.entrypoint="main";vs.format=SDL_GPU_SHADERFORMAT_SPIRV;vs.stage=SDL_GPU_SHADERSTAGE_VERTEX;
        std::vector<SDL_GPUShaderCreateInfo> fragments(count,vs);
        for(unsigned i=0;i<count;++i){auto words=(iteration+i)%2?std::span<const std::uint32_t>(fixtures::green):std::span<const std::uint32_t>(fixtures::red);fragments[i].code_size=words.size_bytes();fragments[i].code=reinterpret_cast<const Uint8*>(words.data());fragments[i].stage=SDL_GPU_SHADERSTAGE_FRAGMENT;}
        SDL_GPUColorTargetDescription color{};color.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;SDL_GPUGraphicsPipelineCreateInfo p{};p.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;p.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;p.rasterizer_state.enable_depth_clip=true;p.target_info.color_target_descriptions=&color;p.target_info.num_color_targets=1;
        std::vector<SDL_GPUGraphicsPipeline*> pipelines(count);
        auto a=bench::Clock::now();
        for(unsigned i=0;i<count;++i){auto vertex=sdl(SDL_CreateGPUShader(d,&vs)),fragment=sdl(SDL_CreateGPUShader(d,&fragments[i]));p.vertex_shader=vertex;p.fragment_shader=fragment;pipelines[i]=sdl(SDL_CreateGPUGraphicsPipeline(d,&p));SDL_ReleaseGPUShader(d,vertex);SDL_ReleaseGPUShader(d,fragment);}
        auto b=bench::Clock::now();
        SDL_GPUColorTargetInfo target{};target.texture=texture;target.clear_color={0,iteration%2?1.f:0.f,1,1};target.load_op=SDL_GPU_LOADOP_CLEAR;target.store_op=SDL_GPU_STOREOP_STORE;
        auto cmd=sdl(SDL_AcquireGPUCommandBuffer(d));auto pass=sdl(SDL_BeginGPURenderPass(cmd,&target,1,nullptr));
        for(unsigned i=0;i<count;++i){SDL_BindGPUGraphicsPipeline(pass,pipelines[i]);SDL_Rect sc{int(edge(i,count)),int(top),int(edge(i+1,count)-edge(i,count)),int(bottom-top)};SDL_SetGPUScissor(pass,&sc);SDL_DrawGPUPrimitives(pass,3,1,0,0);}
        SDL_EndGPURenderPass(pass);complete(cmd);sdl(SDL_WaitForGPUIdle(d));
        auto c=bench::Clock::now();for(auto pipeline:pipelines)SDL_ReleaseGPUGraphicsPipeline(d,pipeline);auto e=bench::Clock::now();
        sdl(SDL_WaitForGPUIdle(d));return{bench::ms(a,b),bench::ms(c,e)};
    }
    std::vector<std::byte> read()override{
        SDL_GPUTransferBufferCreateInfo t{SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,width*height*4,0};auto r=sdl(SDL_CreateGPUTransferBuffer(d,&t));auto c=sdl(SDL_AcquireGPUCommandBuffer(d));auto p=sdl(SDL_BeginGPUCopyPass(c));SDL_GPUTextureRegion from{};from.texture=texture;from.w=width;from.h=height;from.d=1;SDL_GPUTextureTransferInfo to{r,0,width,height};SDL_DownloadFromGPUTexture(p,&from,&to);SDL_EndGPUCopyPass(p);complete(c);
        auto data=sdl(SDL_MapGPUTransferBuffer(d,r,false));std::vector<std::byte> bytes(t.size);std::memcpy(bytes.data(),data,bytes.size());SDL_UnmapGPUTransferBuffer(d,r);SDL_ReleaseGPUTransferBuffer(d,r);return bytes;
    }
    void close()override{if(!d)return;sdl(SDL_WaitForGPUIdle(d));if(texture)SDL_ReleaseGPUTexture(d,texture);texture={};SDL_DestroyGPUDevice(d);d={};SDL_Quit();if(logErrors)throw std::runtime_error("SDL logged errors");}
};
}
int main(int argc,char**argv)try{
    using namespace psobench;const bool probe=(argc==4||argc==5)&&std::string_view(argv[1])=="--probe";
    if(!probe&&argc!=8)throw std::runtime_error("Usage: vulkan_pso <graphicssdk|sdl> <pso1|pso8> <high|low> <warmup 1..64> <samples 3..256> <validation 0|1> <expected GPU>; or --probe <sdk> <high|low> [expected GPU]");
    const std::string sdk=argv[probe?2:1],preference=argv[3];if((sdk!="sdl"&&sdk!="graphicssdk")||(preference!="high"&&preference!="low"))throw std::runtime_error("Usage: invalid SDK/preference");
    unsigned count=probe?0:work(argv[2]),warmup=probe?0:number(argv[4],1,64),samples=probe?0:number(argv[5],3,256);bool validation=!probe&&number(argv[6],0,1);const std::string expected=probe?(argc==5?argv[4]:""):argv[7];if((!probe||argc==5)&&expected.empty())throw std::runtime_error("Expected GPU name required");
    linuxwork::guard();
    if(validation){SDK required(true,expected);required.close();}
    std::unique_ptr<Adapter>a;if(sdk=="sdl")a=std::make_unique<SDL>(validation,preference=="low");else a=std::make_unique<SDK>(validation,expected);
    auto name=a->name();if(!expected.empty()&&name!=expected)throw std::runtime_error("Selected GPU differs from expected GPU");auto id=upload::identify(name);auto env=bench::environment(name);auto loaded=layer(validation);std::vector<Times> measured;
    if(!probe){a->prepare();for(unsigned i=0;i<warmup+samples;++i){auto result=a->sample(count,i);if(a->read()!=reference(i,count))throw std::runtime_error("Full pipeline stripe reference mismatch at iteration "+std::to_string(i));if(i>=warmup)measured.push_back(result);}}
    a->close();if(logErrors)throw std::runtime_error("SDL teardown errors");
    std::cout<<std::setprecision(17)<<"{\"schema_version\":1,\"sdk\":"<<quote(sdk)<<",\"api\":\"vulkan\",\"category\":\"cpu_api\",\"contract\":\"vulkan_pipeline_create_public_release_use_checked_v1\",\"probe\":"<<(probe?"true":"false")
        <<",\"scenario\":"<<quote(probe?"probe":argv[2])<<",\"preference\":"<<quote(preference)<<",\"configuration\":\"Release\",\"validation\":"<<(validation?"true":"false")<<",\"source_fingerprint\":"<<quote(GFX_VK_PSO_FINGERPRINT)<<",\"SDK_source_fingerprint\":"<<quote(gfx::buildFingerprint())<<",\"SDL_commit\":"<<quote(GFX_SDL_PIN)<<",\"shader_header_sha256\":"<<quote(GFX_PSO_SHADER_HEADER_SHA)
        <<",\"requested_instance_api_version\":4198400,\"device_name\":"<<quote(name)<<",\"device_uuid\":"<<quote(id.uuid)<<",\"driver_uuid\":"<<quote(id.driverUuid)<<",\"device_luid\":"<<"null"<<",\"native_vendor_id\":"<<id.vendor<<",\"native_device_id\":"<<id.device<<",\"native_driver_version\":"<<id.driver<<",\"native_api_version\":"<<id.api
        <<",\"identity_attribution\":\"unique_exact_name_to_native_vulkan_not_SDL_native_handle\",\"environment\":"<<env<<",\"loaded_validation_layer\":"<<quote(loaded)<<",\"warmup\":"<<warmup<<",\"sample_count\":"<<samples<<",\"width\":"<<width<<",\"height\":"<<height<<",\"texel_bytes\":4,\"pipelines_per_iteration\":"<<count<<",\"created_pipelines\":"<<std::uint64_t(warmup+samples)*count<<",\"released_pipeline_handles\":"<<std::uint64_t(warmup+samples)*count<<",\"used_pipeline_regions\":"<<std::uint64_t(warmup+samples)*count<<",\"verified_images\":"<<warmup+samples<<",\"verified_bytes\":"<<std::uint64_t(warmup+samples)*width*height*4<<",\"readback_verified\":"<<(probe?"false":"true")<<",\"includes_shader_object_create_release\":true,\"GPU_use_in_timing\":false,\"GPU_memory_reclamation_verified\":false,\"gpu_ms\":null,\"cache_state\":\"uncontrolled_driver_cache_repeated_public_creation\",\"columns\":[\"create_ms\",\"public_release_ms\"],\"samples_ms\":[";
    for(std::size_t i=0;i<measured.size();++i){if(i)std::cout<<',';std::cout<<'['<<measured[i][0]<<','<<measured[i][1]<<']';}std::cout<<"]}\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
