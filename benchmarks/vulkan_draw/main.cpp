#include "../common/Adapter.hpp"
#include "../vulkan_upload/Identity.hpp"
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
namespace drawbench {
using bench::quote;
template<class T>T take(gfx::Result<T> r){if(!r)throw std::runtime_error(r.error().message+" native="+std::to_string(r.error().nativeCode));return std::move(*r);}
void take(gfx::Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
void sdl(bool b){if(!b)throw std::runtime_error(SDL_GetError());}
template<class T>T* sdl(T* p){if(!p)throw std::runtime_error(SDL_GetError());return p;}
unsigned number(std::string_view s,unsigned lo,unsigned hi){unsigned n{};auto[p,e]=std::from_chars(s.data(),s.data()+s.size(),n);if(e!=std::errc{}||p!=s.data()+s.size()||n<lo||n>hi)throw std::runtime_error("Invalid bounded integer option");return n;}
std::atomic<unsigned> logErrors{};
void SDLCALL log(void*,int,SDL_LogPriority priority,const char* message){if(priority>=SDL_LOG_PRIORITY_ERROR)++logErrors;std::cerr<<"SDL: "<<message<<'\n';}
std::string layer(bool validation){
    auto m=GetModuleHandleW(L"VkLayer_khronos_validation.dll");if(!validation){if(m)throw std::runtime_error("Validation active in performance process");return{};}
    wchar_t path[32768]{};auto n=m?GetModuleFileNameW(m,path,32768):0;char expected[32768]{};auto e=GetEnvironmentVariableA("GFX_EXPECT_VULKAN_LAYER",expected,32768);
    if(!n||n==32768||!e||e>=32768||!std::filesystem::equivalent(path,expected))throw std::runtime_error("Pinned validation layer is not loaded");return std::filesystem::path(path).generic_string();
}
using Times=std::array<double,4>; // recording / submission / fence wait / total; CPU intervals only.
Times times(bench::Clock::time_point a,bench::Clock::time_point b,bench::Clock::time_point c,bench::Clock::time_point d){return{bench::ms(a,b),bench::ms(b,c),bench::ms(c,d),bench::ms(a,d)};}
struct Adapter {
    virtual~Adapter()=default;virtual std::string name()const=0;virtual void prepare()=0;
    virtual Times draw(Work,unsigned)=0;virtual std::vector<std::byte> read()=0;virtual void close()=0;
};
struct SDK final:Adapter {
    std::unique_ptr<gfx::Device>d;gfx::TexturePtr texture;gfx::PipelinePtr red,green;std::unique_ptr<gfx::CommandList>cmd;bool validation;std::string deviceName;
    SDK(bool v,const std::string& expected):validation(v){
        auto aa=take(gfx::enumerateAdapters(gfx::Backend::Vulkan));auto it=std::find_if(aa.begin(),aa.end(),[&](auto&a){return expected.empty()?!a.softwareAdapter:a.name==expected;});
        if(it==aa.end()||(!expected.empty()&&std::count_if(aa.begin(),aa.end(),[&](auto&a){return a.name==expected;})!=1))throw std::runtime_error("Unique exact SDK GPU required");
        gfx::DeviceDesc desc;desc.backend=gfx::Backend::Vulkan;desc.adapter=it->id;desc.softwareAdapter=it->softwareAdapter;desc.validation=v;d=take(gfx::createDevice(desc));deviceName=d->caps().adapterName;
    }
    ~SDK(){try{close();}catch(...){std::abort();}}
    std::string name()const override{return deviceName;}
    void prepare()override{
        texture=take(d->createTexture({width,height,gfx::Format::RGBA8UNorm,true}));cmd=take(d->createCommandList());
        auto binary=[](auto& words,gfx::ShaderStage stage){auto b=std::as_bytes(std::span(words));return gfx::ShaderBinary{stage,{b.begin(),b.end()},gfx::ShaderFormat::SPIRV};};
        gfx::GraphicsPipelineDesc p;p.vertex=binary(fixtures::fullscreen,gfx::ShaderStage::Vertex);p.fragment=binary(fixtures::red,gfx::ShaderStage::Fragment);p.renderTargets=gfx::RenderTargetLayout{{gfx::Format::RGBA8UNorm},{}};
        red=take(d->createGraphicsPipeline(p));p.fragment=binary(fixtures::green,gfx::ShaderStage::Fragment);green=take(d->createGraphicsPipeline(p));
    }
    Times draw(Work w,unsigned iteration)override{
        auto& pipeline=iteration%2?green:red;const gfx::Color clear{0,iteration%2?1.f:0.f,1,1};
        auto a=bench::Clock::now();take(cmd->begin());take(cmd->transition(texture,gfx::ResourceState::RenderTarget));take(cmd->beginRenderPass(texture,clear));take(cmd->setPipeline(pipeline));take(cmd->setScissor(left,top,right-left,bottom-top));
        for(unsigned i=0;i<w.calls;++i)take(cmd->draw(3,w.instances,0,0));take(cmd->endRenderPass());take(cmd->end());auto b=bench::Clock::now();
        auto fence=take(d->queue().submit(*cmd));auto c=bench::Clock::now();take(d->queue().wait(fence));auto e=bench::Clock::now();return times(a,b,c,e);
    }
    std::vector<std::byte> read()override{
        auto f=texture->footprint();auto r=take(d->createBuffer({f.size,gfx::Memory::Readback}));auto c=take(d->createCommandList());take(c->begin());take(c->transition(texture,gfx::ResourceState::CopySource));take(c->copyTextureToBuffer(texture,r));take(c->end());take(d->queue().wait(take(d->queue().submit(*c))));auto padded=take(r->read(0,f.size));std::vector<std::byte> tight(width*height*4);
        for(unsigned y=0;y<height;++y)std::memcpy(tight.data()+y*width*4,padded.data()+y*f.rowPitch,width*4);return tight;
    }
    void close()override{if(!d)return;take(d->queue().waitIdle());cmd.reset();texture.reset();red.reset();green.reset();if(validation&&take(d->validationErrorCount()))throw std::runtime_error("SDK validation errors");d.reset();}
};
struct SDL final:Adapter {
    SDL_GPUDevice*d{};SDL_GPUTexture*texture{};SDL_GPUGraphicsPipeline *red{},*green{};std::string deviceName;
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
        auto shader=[&](auto& words,SDL_GPUShaderStage stage){SDL_GPUShaderCreateInfo s{};s.code_size=words.size()*4;s.code=reinterpret_cast<const Uint8*>(words.data());s.entrypoint="main";s.format=SDL_GPU_SHADERFORMAT_SPIRV;s.stage=stage;return sdl(SDL_CreateGPUShader(d,&s));};
        auto vs=shader(fixtures::fullscreen,SDL_GPU_SHADERSTAGE_VERTEX),rs=shader(fixtures::red,SDL_GPU_SHADERSTAGE_FRAGMENT),gs=shader(fixtures::green,SDL_GPU_SHADERSTAGE_FRAGMENT);
        SDL_GPUColorTargetDescription color{};color.format=t.format;SDL_GPUGraphicsPipelineCreateInfo p{};p.vertex_shader=vs;p.fragment_shader=rs;p.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;p.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;p.rasterizer_state.enable_depth_clip=true;p.target_info.color_target_descriptions=&color;p.target_info.num_color_targets=1;
        red=sdl(SDL_CreateGPUGraphicsPipeline(d,&p));p.fragment_shader=gs;green=sdl(SDL_CreateGPUGraphicsPipeline(d,&p));SDL_ReleaseGPUShader(d,vs);SDL_ReleaseGPUShader(d,rs);SDL_ReleaseGPUShader(d,gs);
    }
    void complete(SDL_GPUCommandBuffer*c){auto f=sdl(SDL_SubmitGPUCommandBufferAndAcquireFence(c));bool ok=SDL_WaitForGPUFences(d,true,&f,1);SDL_ReleaseGPUFence(d,f);sdl(ok);}
    Times draw(Work w,unsigned iteration)override{
        auto pipeline=iteration%2?green:red;SDL_GPUColorTargetInfo color{};color.texture=texture;color.clear_color={0,iteration%2?1.f:0.f,1,1};color.load_op=SDL_GPU_LOADOP_CLEAR;color.store_op=SDL_GPU_STOREOP_STORE;
        auto a=bench::Clock::now();auto cmd=sdl(SDL_AcquireGPUCommandBuffer(d));auto pass=sdl(SDL_BeginGPURenderPass(cmd,&color,1,nullptr));SDL_BindGPUGraphicsPipeline(pass,pipeline);SDL_Rect sc{left,top,right-left,bottom-top};SDL_SetGPUScissor(pass,&sc);
        for(unsigned i=0;i<w.calls;++i)SDL_DrawGPUPrimitives(pass,3,w.instances,0,0);SDL_EndGPURenderPass(pass);auto b=bench::Clock::now();auto fence=sdl(SDL_SubmitGPUCommandBufferAndAcquireFence(cmd));auto c=bench::Clock::now();bool ok=SDL_WaitForGPUFences(d,true,&fence,1);SDL_ReleaseGPUFence(d,fence);sdl(ok);auto e=bench::Clock::now();return times(a,b,c,e);
    }
    std::vector<std::byte> read()override{
        SDL_GPUTransferBufferCreateInfo t{SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,width*height*4,0};auto r=sdl(SDL_CreateGPUTransferBuffer(d,&t));auto c=sdl(SDL_AcquireGPUCommandBuffer(d));auto p=sdl(SDL_BeginGPUCopyPass(c));SDL_GPUTextureRegion from{};from.texture=texture;from.w=width;from.h=height;from.d=1;SDL_GPUTextureTransferInfo to{r,0,width,height};SDL_DownloadFromGPUTexture(p,&from,&to);SDL_EndGPUCopyPass(p);complete(c);
        auto data=sdl(SDL_MapGPUTransferBuffer(d,r,false));std::vector<std::byte> bytes(t.size);std::memcpy(bytes.data(),data,bytes.size());SDL_UnmapGPUTransferBuffer(d,r);SDL_ReleaseGPUTransferBuffer(d,r);return bytes;
    }
    void close()override{if(!d)return;sdl(SDL_WaitForGPUIdle(d));if(texture)SDL_ReleaseGPUTexture(d,texture);if(red)SDL_ReleaseGPUGraphicsPipeline(d,red);if(green)SDL_ReleaseGPUGraphicsPipeline(d,green);texture={};red={};green={};SDL_DestroyGPUDevice(d);d={};SDL_Quit();if(logErrors)throw std::runtime_error("SDL logged errors");}
};
}
int main(int argc,char**argv)try{
    using namespace drawbench;const bool probe=(argc==4||argc==5)&&std::string_view(argv[1])=="--probe";
    if(!probe&&argc!=8)throw std::runtime_error("Usage: vulkan_draw <graphicssdk|sdl> <draw1|draw1000|instanced1000> <high|low> <warmup 1..64> <samples 3..256> <validation 0|1> <expected GPU>; or --probe <sdk> <high|low> [expected GPU]");
    const std::string sdk=argv[probe?2:1],preference=argv[3];if((sdk!="sdl"&&sdk!="graphicssdk")||(preference!="high"&&preference!="low"))throw std::runtime_error("Usage: invalid SDK/preference");
    auto w=probe?Work{}:work(argv[2]);auto warmup=probe?0:number(argv[4],1,64),samples=probe?0:number(argv[5],3,256);bool validation=!probe&&number(argv[6],0,1);const std::string expected=probe?(argc==5?argv[4]:""):argv[7];if((!probe||argc==5)&&expected.empty())throw std::runtime_error("Expected GPU name required");
    if(validation){SDK required(true,expected);required.close();}
    std::unique_ptr<Adapter>a;if(sdk=="sdl")a=std::make_unique<SDL>(validation,preference=="low");else a=std::make_unique<SDK>(validation,expected);
    auto name=a->name();if(!expected.empty()&&name!=expected)throw std::runtime_error("Selected GPU differs from expected GPU");auto id=upload::identify(name);auto env=bench::environment(name);auto loaded=layer(validation);std::vector<Times> measured;
    if(!probe){a->prepare();for(unsigned i=0;i<warmup+samples;++i){auto result=a->draw(w,i);if(a->read()!=reference(i))throw std::runtime_error("Changing full-image draw reference mismatch at iteration "+std::to_string(i));if(i>=warmup)measured.push_back(result);}}
    a->close();if(logErrors)throw std::runtime_error("SDL teardown errors");
    std::cout<<std::setprecision(17)<<"{\"schema_version\":1,\"sdk\":"<<quote(sdk)<<",\"api\":\"vulkan\",\"category\":\"end_to_end\",\"contract\":\"vulkan_scissored_fullscreen_triangle_draw_fence_each_sample_readback_v1\",\"probe\":"<<(probe?"true":"false")
        <<",\"scenario\":"<<quote(probe?"probe":argv[2])<<",\"preference\":"<<quote(preference)<<",\"configuration\":\"Release\",\"validation\":"<<(validation?"true":"false")<<",\"source_fingerprint\":"<<quote(GFX_VK_DRAW_FINGERPRINT)<<",\"SDK_source_fingerprint\":"<<quote(gfx::buildFingerprint())<<",\"SDL_commit\":"<<quote(GFX_SDL_PIN)<<",\"shader_header_sha256\":"<<quote(GFX_DRAW_SHADER_HEADER_SHA)
        <<",\"requested_instance_api_version\":4198400,\"device_name\":"<<quote(name)<<",\"device_uuid\":"<<quote(id.uuid)<<",\"driver_uuid\":"<<quote(id.driverUuid)<<",\"device_luid\":"<<quote(id.luid)<<",\"native_vendor_id\":"<<id.vendor<<",\"native_device_id\":"<<id.device<<",\"native_driver_version\":"<<id.driver<<",\"native_api_version\":"<<id.api
        <<",\"identity_attribution\":\"unique_exact_name_to_native_vulkan_and_dxgi_not_SDL_native_handle\",\"environment\":"<<env<<",\"loaded_validation_layer\":"<<quote(loaded)<<",\"warmup\":"<<warmup<<",\"sample_count\":"<<samples<<",\"width\":"<<width<<",\"height\":"<<height<<",\"texel_bytes\":4,\"draw_calls_per_frame\":"<<w.calls<<",\"instances_per_call\":"<<w.instances<<",\"vertices_per_instance\":3,\"vertices_per_frame\":"<<3*w.calls*w.instances<<",\"first_vertex\":0,\"first_instance\":0,\"scissor\":[5,3,55,31],\"verified_images\":"<<warmup+samples<<",\"verified_bytes\":"<<std::uint64_t(warmup+samples)*width*height*4<<",\"submitted_draw_calls\":"<<std::uint64_t(warmup+samples)*w.calls<<",\"readback_verified\":"<<(probe?"false":"true")<<",\"gpu_ms\":null,\"columns\":[\"record_ms\",\"submit_ms\",\"wait_ms\",\"total_ms\"],\"samples_ms\":[";
    for(std::size_t i=0;i<measured.size();++i){if(i)std::cout<<',';std::cout<<measured[i][3];}std::cout<<"],\"CPU_intervals_ms\":[";
    for(std::size_t i=0;i<measured.size();++i){if(i)std::cout<<',';std::cout<<'[';for(unsigned j=0;j<4;++j){if(j)std::cout<<',';std::cout<<measured[i][j];}std::cout<<']';}std::cout<<"]}\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
