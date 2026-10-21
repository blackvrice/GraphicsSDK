#include "Workloads.hpp"
#include "common/Telemetry.hpp"
#include <SDL3/SDL.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <atomic>
#include <iomanip>
#include <iostream>

namespace {
using namespace phase1;
std::atomic<bool> logError{false};
void SDLCALL logMessage(void*,int,SDL_LogPriority priority,const char* message) {
    if(priority>=SDL_LOG_PRIORITY_ERROR) logError=true;
    std::cerr<<message<<'\n';
}
Bytes compile(const std::string& source,const char* entry,const char* profile) {
    Microsoft::WRL::ComPtr<ID3DBlob> code,error;
    auto hr=D3DCompile(source.data(),source.size(),"phase1.hlsl",nullptr,nullptr,entry,profile,
        D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_WARNINGS_ARE_ERRORS,0,&code,&error);
    if(FAILED(hr))throw std::runtime_error(error?static_cast<const char*>(error->GetBufferPointer()):"D3DCompile failed");
    Bytes bytes(code->GetBufferSize());std::memcpy(bytes.data(),code->GetBufferPointer(),bytes.size());return bytes;
}
// SDL's public shader ABI uses fragment space2 and TEXCOORD vertex inputs.
std::string graphicsABI(std::string source) {
    for(auto [from,to]:{std::pair{"register(t0)","register(t0,space2)"},
                       std::pair{"register(t1)","register(t1,space2)"},
                       std::pair{"p:POSITION","p:TEXCOORD0"}}) {
        size_t at=0;while((at=source.find(from,at))!=std::string::npos){source.replace(at,std::strlen(from),to);at+=std::strlen(to);}
    }
    return source;
}
struct Suite {
    SDL_GPUDevice* device{};
    SDL_GPUSampler* sampler{};
    std::vector<std::function<void()>> release;
    std::string name,env;
    unsigned checks=0;
    explicit Suite(bool validation) {
        SDL_SetLogOutputFunction(logMessage,nullptr);
        SDL_SetAssertionHandler([](const SDL_AssertData* a,void*){std::cerr<<"SDL assertion: "<<a->condition<<'\n';return SDL_ASSERTION_ABORT;},nullptr);
        require(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
        device=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXBC,validation,"direct3d12");require(device!=nullptr,SDL_GetError());
        name=SDL_GetStringProperty(SDL_GetGPUDeviceProperties(device),SDL_PROP_GPU_DEVICE_NAME_STRING,"unknown");env=bench::environment(name);
        SDL_GPUSamplerCreateInfo d{};sampler=SDL_CreateGPUSampler(device,&d);require(sampler!=nullptr,SDL_GetError());
        release.push_back([this]{SDL_ReleaseGPUSampler(device,sampler);});
    }
    template<class T,class F> T* keep(T* object,F destroy) {
        require(object!=nullptr,SDL_GetError());release.push_back([this,object,destroy]{destroy(device,object);});return object;
    }
    void idle(){require(SDL_WaitForGPUIdle(device),SDL_GetError());require(!logError,"SDL/native validation logged an error");}
    SDL_GPUCommandBuffer* begin(){auto c=SDL_AcquireGPUCommandBuffer(device);require(c!=nullptr,SDL_GetError());return c;}
    void submit(SDL_GPUCommandBuffer* c){require(SDL_SubmitGPUCommandBuffer(c),SDL_GetError());idle();}
    void close(){idle();for(auto i=release.rbegin();i!=release.rend();++i)(*i)();release.clear();idle();SDL_DestroyGPUDevice(device);device=nullptr;SDL_Quit();require(!logError,"SDL/native teardown logged an error");}
    ~Suite(){if(device)try{close();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';std::abort();}}
    SDL_GPUTransferBuffer* transfer(unsigned size,SDL_GPUTransferBufferUsage usage){
        SDL_GPUTransferBufferCreateInfo d{usage,size,0};return keep(SDL_CreateGPUTransferBuffer(device,&d),SDL_ReleaseGPUTransferBuffer);
    }
    void write(SDL_GPUTransferBuffer* transfer,const void* data,size_t size){
        auto p=SDL_MapGPUTransferBuffer(device,transfer,false);require(p!=nullptr,SDL_GetError());std::memcpy(p,data,size);SDL_UnmapGPUTransferBuffer(device,transfer);
    }
    SDL_GPUBuffer* buffer(const void* data,unsigned size,SDL_GPUBufferUsageFlags usage){
        SDL_GPUBufferCreateInfo d{usage,size,0};auto b=keep(SDL_CreateGPUBuffer(device,&d),SDL_ReleaseGPUBuffer);
        if(data){auto t=transfer(size,SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD);write(t,data,size);auto c=begin();auto p=SDL_BeginGPUCopyPass(c);require(p!=nullptr,SDL_GetError());
            SDL_GPUTransferBufferLocation from{t,0};SDL_GPUBufferRegion to{b,0,size};SDL_UploadToGPUBuffer(p,&from,&to,false);SDL_EndGPUCopyPass(p);submit(c);}
        return b;
    }
    SDL_GPUTexture* texture(unsigned w,unsigned h,SDL_GPUTextureFormat format,SDL_GPUTextureUsageFlags usage){
        SDL_GPUTextureCreateInfo d{};d.type=SDL_GPU_TEXTURETYPE_2D;d.format=format;d.usage=usage;d.width=w;d.height=h;d.layer_count_or_depth=1;d.num_levels=1;
        return keep(SDL_CreateGPUTexture(device,&d),SDL_ReleaseGPUTexture);
    }
    void upload(SDL_GPUCopyPass* p,SDL_GPUTransferBuffer* t,SDL_GPUTexture* texture,unsigned w,unsigned h,unsigned pitch,unsigned bpp){
        SDL_GPUTextureTransferInfo from{t,0,pitch/bpp,h};SDL_GPUTextureRegion to{};to.texture=texture;to.w=w;to.h=h;to.d=1;SDL_UploadToGPUTexture(p,&from,&to,false);
    }
    SDL_GPUShader* shader(const std::string& source,const char* entry,bool vertex,unsigned textures=0,unsigned buffers=0,bool storageTextures=false){
        auto bytes=compile(graphicsABI(source),entry,vertex?"vs_5_1":"ps_5_1");SDL_GPUShaderCreateInfo d{};
        d.code=reinterpret_cast<const Uint8*>(bytes.data());d.code_size=bytes.size();d.entrypoint=entry;d.format=SDL_GPU_SHADERFORMAT_DXBC;
        d.stage=vertex?SDL_GPU_SHADERSTAGE_VERTEX:SDL_GPU_SHADERSTAGE_FRAGMENT;d.num_samplers=storageTextures?0:textures;d.num_storage_textures=storageTextures?textures:0;d.num_storage_buffers=buffers;
        return keep(SDL_CreateGPUShader(device,&d),SDL_ReleaseGPUShader);
    }
    SDL_GPUGraphicsPipeline* pipeline(const std::string& source,const char* vs,const char* fs,unsigned textures=0,unsigned buffers=0,bool blend=false,bool mesh=false,bool storageTextures=false){
        SDL_GPUGraphicsPipelineCreateInfo d{};d.vertex_shader=shader(source,vs,true);d.fragment_shader=shader(source,fs,false,textures,buffers,storageTextures);
        d.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;d.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;d.rasterizer_state.enable_depth_clip=true;
        std::array<SDL_GPUColorTargetDescription,2> colors{};for(auto& c:colors)c.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        if(blend){auto& b=colors[0].blend_state;b.enable_blend=true;b.src_color_blendfactor=SDL_GPU_BLENDFACTOR_SRC_ALPHA;b.dst_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;b.color_blend_op=SDL_GPU_BLENDOP_ADD;b.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;b.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;b.alpha_blend_op=SDL_GPU_BLENDOP_ADD;}
        d.target_info.color_target_descriptions=colors.data();d.target_info.num_color_targets=mesh?2:1;
        SDL_GPUVertexBufferDescription vb{0,12,SDL_GPU_VERTEXINPUTRATE_VERTEX,0};SDL_GPUVertexAttribute attribute{0,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,0};
        if(mesh){d.vertex_input_state.vertex_buffer_descriptions=&vb;d.vertex_input_state.num_vertex_buffers=1;d.vertex_input_state.vertex_attributes=&attribute;d.vertex_input_state.num_vertex_attributes=1;
            d.target_info.has_depth_stencil_target=true;d.target_info.depth_stencil_format=SDL_GPU_TEXTUREFORMAT_D32_FLOAT;d.depth_stencil_state.enable_depth_test=true;d.depth_stencil_state.enable_depth_write=true;d.depth_stencil_state.compare_op=SDL_GPU_COMPAREOP_LESS;}
        return keep(SDL_CreateGPUGraphicsPipeline(device,&d),SDL_ReleaseGPUGraphicsPipeline);
    }
    SDL_GPURenderPass* render(SDL_GPUCommandBuffer* c,SDL_GPUTexture* color,unsigned w,unsigned h,SDL_GPUTexture* second=nullptr,SDL_GPUTexture* depth=nullptr,bool clear=false){
        std::array<SDL_GPUColorTargetInfo,2> colors{};colors[0].texture=color;colors[1].texture=second;
        for(auto& a:colors){a.load_op=clear?SDL_GPU_LOADOP_CLEAR:SDL_GPU_LOADOP_DONT_CARE;a.store_op=SDL_GPU_STOREOP_STORE;a.clear_color={0,0,1,1};}
        SDL_GPUDepthStencilTargetInfo z{};z.texture=depth;z.clear_depth=1;z.load_op=SDL_GPU_LOADOP_CLEAR;z.store_op=SDL_GPU_STOREOP_STORE;z.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;z.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
        auto p=SDL_BeginGPURenderPass(c,colors.data(),second?2:1,depth?&z:nullptr);require(p!=nullptr,SDL_GetError());SDL_GPUViewport v{0,0,float(w),float(h),0,1};SDL_SetGPUViewport(p,&v);return p;
    }
    void sample(SDL_GPURenderPass* p,std::initializer_list<SDL_GPUTexture*> textures){
        std::vector<SDL_GPUTextureSamplerBinding> bindings;for(auto t:textures)bindings.push_back({t,sampler});SDL_BindGPUFragmentSamplers(p,0,bindings.data(),static_cast<unsigned>(bindings.size()));
    }
    Bytes read(SDL_GPUTexture* texture,unsigned w,unsigned h){
        const unsigned pitch=(w*4+255)&~255u;auto d=SDLDownloadInfo(pitch*h);auto t=SDL_CreateGPUTransferBuffer(device,&d);
        require(t!=nullptr,SDL_GetError());auto c=begin();auto p=SDL_BeginGPUCopyPass(c);require(p!=nullptr,SDL_GetError());
        SDL_GPUTextureRegion from{};from.texture=texture;from.w=w;from.h=h;from.d=1;SDL_GPUTextureTransferInfo to{t,0,pitch/4,h};SDL_DownloadFromGPUTexture(p,&from,&to);SDL_EndGPUCopyPass(p);submit(c);
        auto data=static_cast<const std::byte*>(SDL_MapGPUTransferBuffer(device,t,false));require(data!=nullptr,SDL_GetError());Bytes bytes(static_cast<size_t>(w)*h*4);
        for(unsigned y=0;y<h;++y)std::memcpy(bytes.data()+size_t(y)*w*4,data+size_t(y)*pitch,w*4);SDL_UnmapGPUTransferBuffer(device,t);SDL_ReleaseGPUTransferBuffer(device,t);return bytes;
    }
    static SDL_GPUTransferBufferCreateInfo SDLDownloadInfo(unsigned size){return {SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,size,0};}
    std::vector<unsigned> read(SDL_GPUBuffer* b,unsigned count){
        auto d=SDLDownloadInfo(count*4);auto t=SDL_CreateGPUTransferBuffer(device,&d);require(t!=nullptr,SDL_GetError());auto c=begin();auto p=SDL_BeginGPUCopyPass(c);require(p!=nullptr,SDL_GetError());
        SDL_GPUBufferRegion from{b,0,count*4};SDL_GPUTransferBufferLocation to{t,0};SDL_DownloadFromGPUBuffer(p,&from,&to);SDL_EndGPUCopyPass(p);submit(c);
        auto data=SDL_MapGPUTransferBuffer(device,t,false);require(data!=nullptr,SDL_GetError());std::vector<unsigned> bytes(count);std::memcpy(bytes.data(),data,count*4);SDL_UnmapGPUTransferBuffer(device,t);SDL_ReleaseGPUTransferBuffer(device,t);return bytes;
    }
    void comparePixels(const Bytes& a,const Bytes& b,unsigned tolerance=1){require(a.size()==b.size(),"Image size mismatch");for(size_t i=0;i<a.size();++i)if(std::abs(int(a[i])-int(b[i]))>int(tolerance))throw std::runtime_error("CPU image mismatch at byte "+std::to_string(i)+": actual="+std::to_string(int(a[i]))+" expected="+std::to_string(int(b[i])));++checks;}
};
constexpr auto rgba=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
constexpr auto colorUsage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;
Task computeTask(Suite& s,const std::string& name){
    constexpr unsigned count=256*256;const auto op=name.substr(8);auto definition=computeDefinition(op,true);
    auto code=compile(definition.source,"CS","cs_5_1");SDL_GPUComputePipelineCreateInfo d{};d.code=reinterpret_cast<const Uint8*>(code.data());d.code_size=code.size();d.entrypoint="CS";d.format=SDL_GPU_SHADERFORMAT_DXBC;
    d.num_readonly_storage_buffers=1;d.num_readwrite_storage_buffers=1;d.threadcount_x=64;d.threadcount_y=d.threadcount_z=1;
    auto pipeline=s.keep(SDL_CreateGPUComputePipeline(s.device,&d),SDL_ReleaseGPUComputePipeline);
    auto a=s.buffer(definition.input.data(),count*4,SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ);std::vector<unsigned> zero(count);
    auto b=s.buffer(zero.data(),count*4,SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE|SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
    SDL_GPUTexture* image=nullptr;SDL_GPUGraphicsPipeline* graphics=nullptr;
    if(op=="graphics"){image=s.texture(64,64,rgba,colorUsage);auto text=std::string(quadSource)+"ByteAddressBuffer Values:register(t0);float4 FromCompute(V v):SV_Target0 {uint n=Values.Load((uint(v.p.y)*64+uint(v.p.x))*4);return float4(float(n&255)/255,0,0,1);}";graphics=s.pipeline(text,"VS","FromCompute",0,1);}
    return {[&s,pipeline,a,b,graphics,image]{auto c=s.begin();SDL_GPUStorageBufferReadWriteBinding out{b,false,0,0,0};auto p=SDL_BeginGPUComputePass(c,nullptr,0,&out,1);require(p!=nullptr,SDL_GetError());SDL_BindGPUComputePipeline(p,pipeline);SDL_BindGPUComputeStorageBuffers(p,0,&a,1);SDL_DispatchGPUCompute(p,count/64,1,1);SDL_EndGPUComputePass(p);
            if(graphics){auto r=s.render(c,image,64,64);SDL_BindGPUGraphicsPipeline(r,graphics);SDL_BindGPUFragmentStorageBuffers(r,0,&b,1);SDL_DrawGPUPrimitives(r,6,1,0,0);SDL_EndGPURenderPass(r);}s.submit(c);},
        [&s,b,image,expected=std::move(definition.expected)]{require(s.read(b,count)==expected,"Compute CPU reference mismatch");++s.checks;if(image){Bytes pixels(4096*4);for(unsigned i=0;i<4096;++i){pixels[i*4]=static_cast<std::byte>(expected[i]&255);pixels[i*4+3]=std::byte{255};}s.comparePixels(s.read(image,64,64),pixels);}},
        "public_compute_uint32_256x256_group64_full_cpu_reference_"+op,graphics?1u:0u,1,count*4};
}
Task graphicsTask(Suite& s,const std::string& name){
    const bool sprites=name.starts_with("sprites_"),instanced=name=="mesh_instanced";const unsigned count=name=="sprites_100000"?100000:10000;
    auto color=s.texture(64,64,rgba,colorUsage);auto second=sprites?nullptr:s.texture(64,64,rgba,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);auto depth=sprites?nullptr:s.texture(64,64,SDL_GPU_TEXTUREFORMAT_D32_FLOAT,SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
    auto source=graphicsSource();auto pipeline=s.pipeline(source,sprites?"Sprite":"Mesh",sprites?"Atlas":"MRT",sprites?1:0,0,sprites,!sprites);
    SDL_GPUTexture* atlas=nullptr;SDL_GPUTexture* post=nullptr;SDL_GPUBuffer* vertices=nullptr;SDL_GPUGraphicsPipeline* postPipeline=nullptr;
    if(sprites){atlas=s.texture(2,2,rgba,SDL_GPU_TEXTUREUSAGE_SAMPLER);Bytes rows(512);std::memcpy(rows.data(),atlasTexels.data(),8);std::memcpy(rows.data()+256,atlasTexels.data()+8,8);auto t=s.transfer(512,SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD);s.write(t,rows.data(),rows.size());auto c=s.begin();auto p=SDL_BeginGPUCopyPass(c);require(p!=nullptr,SDL_GetError());s.upload(p,t,atlas,2,2,256,4);SDL_EndGPUCopyPass(p);s.submit(c);}
    else{vertices=s.buffer(meshVertices.data(),static_cast<unsigned>(sizeof(meshVertices)),SDL_GPU_BUFFERUSAGE_VERTEX);post=s.texture(64,64,rgba,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);postPipeline=s.pipeline(source,"VS","Invert",1);}
    auto expected=graphicsReference(sprites,count);
    return {[&s,pipeline,color,second,depth,atlas,vertices,post,postPipeline,sprites,instanced,count]{auto c=s.begin();auto p=s.render(c,color,64,64,second,depth,true);SDL_BindGPUGraphicsPipeline(p,pipeline);
            if(sprites){SDL_Rect scissor{8,8,48,48};SDL_SetGPUScissor(p,&scissor);s.sample(p,{atlas});SDL_DrawGPUPrimitives(p,6,count,0,0);}
            else{SDL_GPUBufferBinding v{vertices,0};SDL_BindGPUVertexBuffers(p,0,&v,1);if(instanced)SDL_DrawGPUPrimitives(p,6,2000,0,0);else for(unsigned i=0;i<1000;++i)SDL_DrawGPUPrimitives(p,6,2,0,0);}SDL_EndGPURenderPass(p);
            if(postPipeline){auto r=s.render(c,post,64,64);SDL_BindGPUGraphicsPipeline(r,postPipeline);s.sample(r,{color});SDL_DrawGPUPrimitives(r,6,1,0,0);SDL_EndGPURenderPass(r);}s.submit(c);},
        [&s,color,second,post,expected=std::move(expected),sprites]{s.comparePixels(s.read(color,64,64),expected,2);if(!sprites){Bytes green(4096*4),cyan(4096*4);for(unsigned i=0;i<4096;++i){green[i*4+1]=green[i*4+3]=std::byte{255};cyan[i*4+1]=cyan[i*4+2]=cyan[i*4+3]=std::byte{255};}s.comparePixels(s.read(second,64,64),green);s.comparePixels(s.read(post,64,64),cyan);}},
        graphicsContract(sprites,count),sprites?1u:instanced?2u:1001u,0,0};
}
Task videoTask(Suite& s,const std::string& name){
    const bool ten=name=="video_p010",layers=name=="video_layers",filter=name=="video_filter";const unsigned w=(name=="video_4k"||ten)?3840:1920,h=w==3840?2160:1080;
    const unsigned unit=ten?2:1,pitch=(w*unit+255)&~255u;
    struct Plane {SDL_GPUTexture* texture;SDL_GPUTransferBuffer* transfer;Bytes bytes;unsigned w,h,bpp;};auto planes=std::make_shared<std::vector<Plane>>();
    for(unsigned layer=0;layer<(layers?2u:1u);++layer)for(unsigned uv=0;uv<2;++uv){const auto pw=w/(uv?2:1),ph=h/(uv?2:1),bpp=unit*(uv?2:1);
        const auto format=ten?(uv?SDL_GPU_TEXTUREFORMAT_R16G16_UINT:SDL_GPU_TEXTUREFORMAT_R16_UINT):(uv?SDL_GPU_TEXTUREFORMAT_R8G8_UINT:SDL_GPU_TEXTUREFORMAT_R8_UINT);
        Plane p{s.texture(pw,ph,format,SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ),s.transfer(pitch*ph,SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD),Bytes(size_t(pitch)*ph),pw,ph,bpp};
        for(unsigned y=0;y<ph;++y)for(unsigned x=0;x<pw;++x)for(unsigned c=0;c<(uv?2u:1u);++c){unsigned value=uv?(ten?512:layer?128:c?170:90):(layer?112+x%32:(ten?64:16)+(x+y)%(ten?877:220));const size_t at=size_t(y)*pitch+x*bpp+c*unit;if(ten){auto v=static_cast<std::uint16_t>(value<<6);std::memcpy(p.bytes.data()+at,&v,2);}else p.bytes[at]=static_cast<std::byte>(value);}
        planes->push_back(std::move(p));}
    auto color=s.texture(w,h,rgba,colorUsage);auto source=videoSource(ten);auto pipeline=s.pipeline(source,"VS","Video",2,0,false,false,true);
    auto overlay=layers?s.pipeline(source,"VS","OverlayVideo",2,0,true,false,true):nullptr;auto filtering=filter?s.pipeline(source,"VS","Filter",1):nullptr;
    auto finalColor=filter?s.texture(w,h,rgba,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET):color;auto expected=std::make_shared<Bytes>(videoReference(w,h,ten,layers,filter));
    return {[&s,planes,pitch,w,h,color,pipeline,overlay,filtering,finalColor]{for(auto& p:*planes)s.write(p.transfer,p.bytes.data(),p.bytes.size());auto c=s.begin();auto copy=SDL_BeginGPUCopyPass(c);require(copy!=nullptr,SDL_GetError());for(auto& p:*planes)s.upload(copy,p.transfer,p.texture,p.w,p.h,pitch,p.bpp);SDL_EndGPUCopyPass(copy);
            auto p=s.render(c,color,w,h);SDL_BindGPUGraphicsPipeline(p,pipeline);SDL_GPUTexture* first[]{(*planes)[0].texture,(*planes)[1].texture};SDL_BindGPUFragmentStorageTextures(p,0,first,2);SDL_DrawGPUPrimitives(p,6,1,0,0);
            if(overlay){SDL_BindGPUGraphicsPipeline(p,overlay);SDL_GPUTexture* second[]{(*planes)[2].texture,(*planes)[3].texture};SDL_BindGPUFragmentStorageTextures(p,0,second,2);SDL_DrawGPUPrimitives(p,6,1,0,0);}SDL_EndGPURenderPass(p);
            if(filtering){auto r=s.render(c,finalColor,w,h);SDL_BindGPUGraphicsPipeline(r,filtering);s.sample(r,{color});SDL_DrawGPUPrimitives(r,6,1,0,0);SDL_EndGPURenderPass(r);}s.submit(c);},
        [&s,finalColor,w,h,expected]{s.comparePixels(s.read(finalColor,w,h),*expected,2);},videoContract(w,h,ten,layers,filter),layers||filter?2u:1u,0,static_cast<std::uint64_t>(w)*h*3*unit/2*(layers?2:1)};
}
} // namespace
int main(int argc,char** argv)try{
    if(argc==2&&std::string_view(argv[1])=="--list"){std::cout<<'[';for(size_t i=0;i<advancedScenarios.size();++i){if(i)std::cout<<',';std::cout<<bench::quote(advancedScenarios[i]);}std::cout<<"]\n";return 0;}
    require(argc==5&&std::string_view(argv[4])=="--cpu-only","Usage: phase1_sdl <scenario> <samples> <validation:0|1> --cpu-only");
    const std::string scenario=argv[1];size_t consumed=0;const auto samples=std::stoul(argv[2],&consumed);
    require(std::find(advancedScenarios.begin(),advancedScenarios.end(),scenario)!=advancedScenarios.end(),"Unsupported SDL phase1 scenario");require(consumed==std::strlen(argv[2])&&samples>=1&&samples<=10000,"Invalid sample count");require(std::string(argv[3])=="0"||std::string(argv[3])=="1","Invalid validation flag");
    Suite s(std::string(argv[3])=="1");Task task=scenario.starts_with("compute_")?computeTask(s,scenario):scenario.starts_with("video_")?videoTask(s,scenario):graphicsTask(s,scenario);
    task.run();task.verify();constexpr unsigned warmup=3;std::vector<double> values;bench::Telemetry observer(s.name,static_cast<unsigned>(samples),false);
    for(size_t i=0;i<samples+warmup;++i){if(i==warmup)observer.begin();s.idle();auto begin=bench::Clock::now();task.run();auto end=bench::Clock::now();if(i>=warmup){values.push_back(bench::ms(begin,end));observer.checkpoint(false,static_cast<unsigned>(i-warmup));}}
    auto observation=observer.finish();task.verify();require(s.checks>=2,"Missing pre/post references");auto env=s.env;auto checks=s.checks;s.close();
    std::cout<<std::setprecision(17)<<"{\"schema_version\":1,\"sdk\":\"sdl\",\"api\":\"d3d12\",\"category\":\"phase1_extension\",\"scenario\":"<<bench::quote(scenario)<<",\"samples\":[";
    for(size_t i=0;i<values.size();++i){if(i)std::cout<<',';std::cout<<"{\"frame_ms\":"<<values[i]<<",\"gpu_ms\":null}";}
    std::cout<<"],\"warmup\":3,\"validation\":"<<(std::string(argv[3])=="1"?"true":"false")<<",\"readback_verified\":true,\"reference_verified\":true,\"reference_kind\":\"full_gpu_readback_CPU_reference\",\"reference_checks\":"<<checks
        <<",\"draw_calls\":"<<task.draws<<",\"dispatch_calls\":"<<task.dispatches<<",\"payload_bytes\":"<<task.payload<<",\"contract\":"<<bench::quote(task.contract)
        <<",\"gpu_scope\":\"disabled\",\"cache_stats\":null,\"telemetry\":"<<observation<<",\"environment\":"<<env<<",\"sdk_build_source_sha256\":\""<<GFX_SDL_BUILD_SHA256<<"\"}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
