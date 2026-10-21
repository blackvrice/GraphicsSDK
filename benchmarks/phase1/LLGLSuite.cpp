#include "common/Adapter.hpp"
#include "common/GpuTimestamp.hpp"
#include "common/Telemetry.hpp"
#include "Workloads.hpp"
#include <LLGL/LLGL.h>
#include <LLGL/Backend/Direct3D12/NativeHandle.h>
#include <d3dcompiler.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>

using Microsoft::WRL::ComPtr;
namespace {
void require(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
using Bytes=phase1::Bytes;
using phase1::quadSource;
Bytes compile(std::string_view source,const char* entry,const char* profile) {
    ComPtr<ID3DBlob> code,error;
    const auto result=D3DCompile(source.data(),source.size(),"phase1.hlsl",nullptr,nullptr,entry,profile,
        D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_WARNINGS_ARE_ERRORS,0,&code,&error);
    if(FAILED(result)) throw std::runtime_error(error?static_cast<const char*>(error->GetBufferPointer()):"D3DCompile failed");
    Bytes bytes(code->GetBufferSize()); std::memcpy(bytes.data(),code->GetBufferPointer(),bytes.size()); return bytes;
}
struct Suite {
    LLGL::RenderSystemPtr device;
    ComPtr<ID3D12InfoQueue> info;
    std::vector<std::function<void()>> release;
    LLGL::CommandBuffer* cmd{};
    LLGL::QueryHeap* query{};
    bool gpuTiming=false;
    std::optional<double> gpuMs;
    std::string env;
    unsigned checks=0;
    explicit Suite(bool validation,bool timestamps) {
        LLGL::RenderSystemDescriptor desc{"Direct3D12"}; if(validation) desc.flags=LLGL::RenderSystemFlags::DebugDevice;
        LLGL::Report report; device=LLGL::RenderSystem::Load(desc,&report); require(bool(device),report.GetText());
        if(validation) {
            LLGL::Direct3D12::RenderSystemNativeHandle h{};
            require(device->GetNativeHandle(&h,sizeof(h))&&h.device,"Native validation unavailable");
            const auto hr=h.device->QueryInterface(IID_PPV_ARGS(&info));
            h.device->Release(); if(h.factory) h.factory->Release(); if(h.commandQueue) h.commandQueue->Release();
            require(SUCCEEDED(hr),"Native info queue unavailable");
        }
        env=bench::environment(device->GetRendererInfo().deviceName.c_str()); cmd=keep(device->CreateCommandBuffer());
        if(timestamps){LLGL::QueryHeapDescriptor q;q.type=LLGL::QueryType::TimeElapsed;q.numQueries=1;query=keep(device->CreateQueryHeap(q));}
    }
    template<class T> T* keep(T* p) { require(p!=nullptr,"LLGL object creation failed"); release.push_back([this,p]{device->Release(*p);}); return p; }
    void validation() {
        if(!info) return;
        for(UINT64 i=0;i<info->GetNumStoredMessages();++i) {
            SIZE_T size=0; require(SUCCEEDED(info->GetMessage(i,nullptr,&size)),"Validation message size"); Bytes data(size);
            auto* m=reinterpret_cast<D3D12_MESSAGE*>(data.data()); require(SUCCEEDED(info->GetMessage(i,m,&size)),"Validation message");
            if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR) throw std::runtime_error(m->pDescription);
        }
        info->ClearStoredMessages();
    }
    void idle() { device->GetCommandQueue()->WaitIdle(); validation(); }
    void begin() { cmd->Begin();if(gpuTiming)cmd->BeginQuery(*query,0); }
    void submit() {
        if(gpuTiming)cmd->EndQuery(*query,0);
        cmd->End(); device->GetCommandQueue()->Submit(*cmd); idle();
        if(gpuTiming){std::uint64_t nanos=0;require(device->GetCommandQueue()->QueryResult(*query,0,1,&nanos,sizeof(nanos)),"Completed GPU query unavailable");gpuMs=bench::gpuTicksToMilliseconds(0,nanos,1e9);validation();}
    }
    void close() { idle(); for(auto i=release.rbegin();i!=release.rend();++i) (*i)(); release.clear(); device.reset(); validation(); }
    ~Suite() { if(device) { try { close(); } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; std::abort(); } } }
    LLGL::Shader* shader(const Bytes& code,LLGL::ShaderType type) {
        LLGL::ShaderDescriptor d; d.type=type; d.source=reinterpret_cast<const char*>(code.data()); d.sourceSize=code.size(); d.sourceType=LLGL::ShaderSourceType::BinaryBuffer;
        auto p=keep(device->CreateShader(d)); require(!p->GetReport()||!p->GetReport()->HasErrors(),p->GetReport()?p->GetReport()->GetText():"Shader creation"); return p;
    }
    LLGL::PipelineState* pipeline(const LLGL::GraphicsPipelineDescriptor& desc) {
        auto p=keep(device->CreatePipelineState(desc)); require(!p->GetReport()||!p->GetReport()->HasErrors(),p->GetReport()?p->GetReport()->GetText():"Pipeline creation"); return p;
    }
    LLGL::Texture* texture(unsigned width,unsigned height,LLGL::Format format,long flags) {
        LLGL::TextureDescriptor d; d.extent={width,height,1}; d.mipLevels=1; d.format=format; d.bindFlags=flags; d.miscFlags=LLGL::MiscFlags::NoInitialData;
        return keep(device->CreateTexture(d));
    }
    LLGL::RenderTarget* target(LLGL::Texture* color,unsigned w=64,unsigned h=64,LLGL::Texture* second=nullptr,LLGL::Texture* depth=nullptr) {
        LLGL::RenderTargetDescriptor d; d.resolution={w,h}; d.colorAttachments[0]=color;
        if(second) d.colorAttachments[1]=second; if(depth) d.depthStencilAttachment=depth;
        return keep(device->CreateRenderTarget(d));
    }
    Bytes read(LLGL::Texture* t,unsigned w,unsigned h) {
        Bytes bytes(static_cast<size_t>(w)*h*4); LLGL::TextureRegion r; r.extent={w,h,1};
        device->ReadTexture(*t,r,LLGL::MutableImageView{LLGL::ImageFormat::RGBA,LLGL::DataType::UInt8,bytes.data(),bytes.size()}); idle(); return bytes;
    }
    void comparePixels(const Bytes& actual,const Bytes& expected,unsigned tolerance=1) {
        require(actual.size()==expected.size(),"Reference image size mismatch");
        for(size_t i=0;i<actual.size();++i) if(std::abs(int(actual[i])-int(expected[i]))>int(tolerance))
            throw std::runtime_error("CPU image mismatch at byte "+std::to_string(i)+": actual="+std::to_string(int(actual[i]))+" expected="+std::to_string(int(expected[i])));
        ++checks;
    }
    LLGL::Buffer* buffer(const std::vector<unsigned>& data,long flags) {
        LLGL::BufferDescriptor d; d.size=data.size()*sizeof(unsigned); d.stride=sizeof(unsigned); d.bindFlags=flags; return keep(device->CreateBuffer(d,data.data()));
    }
};
using Task=phase1::Task;
Task cacheTask(Suite& s,const std::string& name) {
    auto source=std::string(quadSource); auto saved=std::make_shared<Bytes>(compile(source,"VS","vs_5_1"));
    if(name.starts_with("shader_")) {
        const bool hit=name=="shader_warm";
        auto storage=std::make_shared<std::map<std::string,Bytes>>(); const auto key=source+"|VS|vs_5_1|O3|warnings_as_errors";
        if(hit) (*storage)[key]=*saved;
        auto output=std::make_shared<Bytes>();
        auto counters=std::make_shared<std::array<unsigned,2>>();
        return {[storage,output,source,key,hit,counters]{ if(!hit) storage->clear(); auto found=storage->find(key); if(found==storage->end()){++(*counters)[1];found=storage->emplace(key,compile(source,"VS","vs_5_1")).first;}else ++(*counters)[0]; *output=found->second; },
            [&s,output,saved]{require(*output==*saved,"Cached shader differs from fresh compilation");++s.checks;},
            hit?"application_shader_cache_prewarmed_lookup_plus_byte_copy_no_compiler_call":"application_shader_cache_empty_per_sample_compile_and_insert_driver_os_cache_uncontrolled",0,0,0,
            [counters]{return "{\"layer\":\"application_shader_bytecode\",\"hits\":"+std::to_string((*counters)[0])+",\"misses\":"+std::to_string((*counters)[1])+"}";}};
    }
    auto color=s.texture(64,64,LLGL::Format::RGBA8UNorm,LLGL::BindFlags::ColorAttachment|LLGL::BindFlags::CopySrc);
    auto target=s.target(color); auto vs=s.shader(*saved,LLGL::ShaderType::Vertex); auto fs=s.shader(compile(source,"Red","ps_5_1"),LLGL::ShaderType::Fragment);
    LLGL::GraphicsPipelineDescriptor d; d.vertexShader=vs; d.fragmentShader=fs; d.renderPass=target->GetRenderPass(); d.rasterizer.cullMode=LLGL::CullMode::Disabled;
    auto retained=s.pipeline(d); auto selected=std::make_shared<LLGL::PipelineState*>(retained);
    auto latest=std::make_shared<LLGL::PipelineState*>(nullptr);s.release.push_back([&s,latest]{if(*latest)s.device->Release(**latest);});
    auto map=std::make_shared<std::map<std::string,LLGL::PipelineState*>>();const std::string key=source+"|VS|Red|rgba8|triangles|cull_none|no_depth|no_blend";
    auto counters=std::make_shared<std::array<unsigned,2>>();
    const bool hit=name=="pso_hit";
    if(hit)(*map)[key]=retained;
    return {[&s,d,selected,latest,hit,map,key,counters]{
        if(!hit){map->clear();if(*latest){s.device->Release(**latest);*latest=nullptr;}}
        auto entry=map->find(key);
        if(entry!=map->end()){*selected=entry->second;++(*counters)[0];}
        else {auto p=s.device->CreatePipelineState(d);require(p&&(!p->GetReport()||!p->GetReport()->HasErrors()),"PSO miss creation failed");*latest=p;(*map)[key]=p;*selected=p;++(*counters)[1];}
    },[&s,selected,target,color]{s.begin();s.cmd->SetViewport(LLGL::Extent2D{64,64});s.cmd->BeginRenderPass(*target);s.cmd->SetPipelineState(**selected);s.cmd->Draw(6,0);s.cmd->EndRenderPass();s.submit();Bytes expected(64*64*4);for(size_t i=0;i<expected.size();i+=4){expected[i]=expected[i+3]=std::byte{255};}s.comparePixels(s.read(color,64,64),expected);},
        hit?"application_pso_cache_exact_key_prewarmed_handle_lookup_no_creation":"application_pso_cache_miss_release_previous_create_insert_identical_descriptor_driver_cache_uncontrolled",0,0,0,
        [counters]{return "{\"layer\":\"application_pso_handle\",\"hits\":"+std::to_string((*counters)[0])+",\"misses\":"+std::to_string((*counters)[1])+"}";}};
}
Task computeTask(Suite& s,const std::string& name) {
    constexpr unsigned count=256*256;
    const std::string op=name.substr(8);
    auto definition=phase1::computeDefinition(op,false);
    auto input=std::move(definition.input),expected=std::move(definition.expected);auto source=std::move(definition.source);
    auto shader=s.shader(compile(source,"CS","cs_5_1"),LLGL::ShaderType::Compute);
    LLGL::PipelineLayoutDescriptor layout; layout.bindings={{LLGL::ResourceType::Buffer,LLGL::BindFlags::Sampled,LLGL::StageFlags::ComputeStage,0},{LLGL::ResourceType::Buffer,LLGL::BindFlags::Storage,LLGL::StageFlags::ComputeStage,1}};
    auto bindings=s.keep(s.device->CreatePipelineLayout(layout));
    LLGL::ComputePipelineDescriptor desc;desc.computeShader=shader;desc.pipelineLayout=bindings;
    auto p=s.keep(s.device->CreatePipelineState(desc)); require(!p->GetReport()||!p->GetReport()->HasErrors(),p->GetReport()?p->GetReport()->GetText():"Compute pipeline failed");
    auto a=s.buffer(input,LLGL::BindFlags::Sampled); auto b=s.buffer(std::vector<unsigned>(count,0),LLGL::BindFlags::Storage|LLGL::BindFlags::Sampled|LLGL::BindFlags::CopySrc);
    LLGL::Texture* image=nullptr;LLGL::RenderTarget* target=nullptr;LLGL::PipelineState* graphics=nullptr;
    if(op=="graphics") {
        image=s.texture(64,64,LLGL::Format::RGBA8UNorm,LLGL::BindFlags::ColorAttachment|LLGL::BindFlags::CopySrc);target=s.target(image);
        const auto text=std::string(quadSource)+"StructuredBuffer<uint> Values:register(t0);float4 FromCompute(V v):SV_Target0 {uint n=Values[uint(v.p.y)*64+uint(v.p.x)];return float4(float(n&255)/255,0,0,1);}";
        LLGL::PipelineLayoutDescriptor l;l.bindings={{LLGL::ResourceType::Buffer,LLGL::BindFlags::Sampled,LLGL::StageFlags::FragmentStage,0}};auto layout=s.keep(s.device->CreatePipelineLayout(l));
        LLGL::GraphicsPipelineDescriptor d;d.vertexShader=s.shader(compile(text,"VS","vs_5_1"),LLGL::ShaderType::Vertex);d.fragmentShader=s.shader(compile(text,"FromCompute","ps_5_1"),LLGL::ShaderType::Fragment);d.pipelineLayout=layout;d.renderPass=target->GetRenderPass();d.rasterizer.cullMode=LLGL::CullMode::Disabled;graphics=s.pipeline(d);
    }
    return {[&s,p,a,b,graphics,target]{s.begin();s.cmd->SetPipelineState(*p);s.cmd->SetResource(0,*a);s.cmd->SetResource(1,*b);s.cmd->Dispatch(count/64,1,1);if(graphics){s.cmd->SetViewport(LLGL::Extent2D{64,64});s.cmd->BeginRenderPass(*target);s.cmd->SetPipelineState(*graphics);s.cmd->SetResource(0,*b);s.cmd->Draw(6,0);s.cmd->EndRenderPass();}s.submit();},
        [&s,b,expected,image]{std::vector<unsigned> actual(count);s.device->ReadBuffer(*b,0,actual.data(),actual.size()*4);s.idle();require(actual==expected,"Compute CPU reference mismatch");++s.checks;if(image){Bytes pixels(4096*4);for(unsigned i=0;i<4096;++i){pixels[i*4]=static_cast<std::byte>(expected[i]&255);pixels[i*4+3]=std::byte{255};}s.comparePixels(s.read(image,64,64),pixels);}},
        "public_compute_uint32_256x256_group64_full_cpu_reference_"+op,graphics?1u:0u,1,count*4};
}
Task graphicsTask(Suite& s,const std::string& name) {
    const bool sprites=name.starts_with("sprites_"); const bool instanced=name=="mesh_instanced";
    const unsigned count=name=="sprites_100000"?100000:10000;
    auto color=s.texture(64,64,LLGL::Format::RGBA8UNorm,LLGL::BindFlags::ColorAttachment|LLGL::BindFlags::CopySrc|LLGL::BindFlags::Sampled);
    auto second=sprites?nullptr:s.texture(64,64,LLGL::Format::RGBA8UNorm,LLGL::BindFlags::ColorAttachment|LLGL::BindFlags::CopySrc);
    auto depth=sprites?nullptr:s.texture(64,64,LLGL::Format::D32Float,LLGL::BindFlags::DepthStencilAttachment);
    auto target=s.target(color,64,64,second,depth);
    auto source=phase1::graphicsSource();
    LLGL::PipelineLayoutDescriptor layout;layout.bindings={{LLGL::ResourceType::Texture,LLGL::BindFlags::Sampled,LLGL::StageFlags::FragmentStage,0}};
    auto sampling=s.keep(s.device->CreatePipelineLayout(layout));
    auto vs=s.shader(compile(source,sprites?"Sprite":"Mesh","vs_5_1"),LLGL::ShaderType::Vertex);
    auto fs=s.shader(compile(source,sprites?"Atlas":"MRT","ps_5_1"),LLGL::ShaderType::Fragment);
    LLGL::GraphicsPipelineDescriptor desc; desc.vertexShader=vs;desc.fragmentShader=fs;desc.renderPass=target->GetRenderPass();desc.rasterizer.cullMode=LLGL::CullMode::Disabled;
    std::array<LLGL::VertexAttribute,1> attrs{LLGL::VertexAttribute{"POSITION",LLGL::Format::RGB32Float,0,0,12,0}};
    if(sprites) {desc.pipelineLayout=sampling;desc.rasterizer.scissorTestEnabled=true;auto& b=desc.blend.targets[0];b.blendEnabled=true;b.srcColor=LLGL::BlendOp::SrcAlpha;b.dstColor=LLGL::BlendOp::InvSrcAlpha;b.srcAlpha=LLGL::BlendOp::One;b.dstAlpha=LLGL::BlendOp::InvSrcAlpha;}
    else {desc.depth.testEnabled=true;desc.depth.writeEnabled=true;desc.inputVertexAttribs.push_back(attrs[0]);}
    auto pipeline=s.pipeline(desc);
    LLGL::Texture* atlas=nullptr;LLGL::Buffer* vertices=nullptr;LLGL::RenderTarget* output=nullptr;LLGL::Texture* post=nullptr;LLGL::PipelineState* postPipeline=nullptr;
    const auto& texels=phase1::atlasTexels;
    if(sprites) {
        LLGL::TextureDescriptor d;d.extent={2,2,1};d.mipLevels=1;d.format=LLGL::Format::RGBA8UNorm;d.bindFlags=LLGL::BindFlags::Sampled;
        LLGL::ImageView image{LLGL::ImageFormat::RGBA,LLGL::DataType::UInt8,texels.data(),texels.size()};atlas=s.keep(s.device->CreateTexture(d,&image));
    } else {
        const auto& data=phase1::meshVertices;
        LLGL::BufferDescriptor d;d.size=sizeof(data);d.bindFlags=LLGL::BindFlags::VertexBuffer;d.stride=12; vertices=s.keep(s.device->CreateBuffer(d,data.data()));
        post=s.texture(64,64,LLGL::Format::RGBA8UNorm,LLGL::BindFlags::ColorAttachment|LLGL::BindFlags::CopySrc);output=s.target(post);
        LLGL::GraphicsPipelineDescriptor p;p.vertexShader=s.shader(compile(source,"VS","vs_5_1"),LLGL::ShaderType::Vertex);p.fragmentShader=s.shader(compile(source,"Invert","ps_5_1"),LLGL::ShaderType::Fragment);p.pipelineLayout=sampling;p.renderPass=output->GetRenderPass();p.rasterizer.cullMode=LLGL::CullMode::Disabled;postPipeline=s.pipeline(p);
    }
    auto expected=phase1::graphicsReference(sprites,count);
    return {[&s,pipeline,target,atlas,vertices,sampling,output,postPipeline,color,sprites,instanced,count]{
        s.begin();s.cmd->SetViewport(LLGL::Extent2D{64,64});s.cmd->BeginRenderPass(*target);
        LLGL::ClearValue clear;clear.color[0]=clear.color[1]=0;clear.color[2]=clear.color[3]=1;clear.depth=1;s.cmd->Clear(LLGL::ClearFlags::Color|(sprites?0:LLGL::ClearFlags::Depth),clear);
        s.cmd->SetPipelineState(*pipeline);
        if(sprites){s.cmd->SetScissor(LLGL::Scissor{8,8,48,48});s.cmd->SetResource(0,*atlas);s.cmd->DrawInstanced(6,0,count);}
        else {s.cmd->SetVertexBuffer(*vertices);if(instanced)s.cmd->DrawInstanced(6,0,2000);else for(unsigned i=0;i<1000;++i)s.cmd->DrawInstanced(6,0,2);}
        s.cmd->EndRenderPass();
        if(!sprites){s.cmd->BeginRenderPass(*output);s.cmd->SetPipelineState(*postPipeline);s.cmd->SetResource(0,*color);s.cmd->Draw(6,0);s.cmd->EndRenderPass();}
        s.submit();
    },[&s,color,second,post,expected,sprites]{s.comparePixels(s.read(color,64,64),expected,2);if(!sprites){Bytes green(64*64*4),cyan(64*64*4);for(unsigned i=0;i<4096;++i){green[i*4+1]=green[i*4+3]=std::byte{255};cyan[i*4+1]=cyan[i*4+2]=cyan[i*4+3]=std::byte{255};}s.comparePixels(s.read(second,64,64),green);s.comparePixels(s.read(post,64,64),cyan);}},
        phase1::graphicsContract(sprites,count),
        sprites?1u:instanced?2u:1001u,0,0};
}
Task resourceTask(Suite& s,const std::string& name) {
    if(name=="multi_upload") {
        struct Layer {LLGL::Texture* texture; LLGL::Buffer* buffer; std::vector<unsigned> pixels;};
        auto layers=std::make_shared<std::vector<Layer>>();
        for(unsigned i=0;i<4;++i){std::vector<unsigned> pixels(1920*1080);for(unsigned j=0;j<pixels.size();++j)pixels[j]=0xff000000u|((j*17+i*47)&0xffffff);
            auto t=s.texture(1920,1080,LLGL::Format::RGBA8UNorm,LLGL::BindFlags::Sampled|LLGL::BindFlags::CopySrc|LLGL::BindFlags::CopyDst);
            auto b=s.buffer(pixels,LLGL::BindFlags::CopySrc|LLGL::BindFlags::CopyDst);layers->push_back({t,b,std::move(pixels)});}
        return {[&s,layers]{for(auto& l:*layers)s.device->WriteBuffer(*l.buffer,0,l.pixels.data(),l.pixels.size()*4);s.begin();LLGL::TextureRegion r;r.extent={1920,1080,1};for(auto& l:*layers)s.cmd->CopyTextureFromBuffer(*l.texture,r,*l.buffer,0,1920*4);s.submit();},
            [&s,layers]{for(auto& l:*layers){auto actual=s.read(l.texture,1920,1080);require(actual.size()==l.pixels.size()*4&&std::memcmp(actual.data(),l.pixels.data(),actual.size())==0,"Multi-texture reference mismatch");++s.checks;}},
            "B_four_persistent_1080p_rgba8_public_writebuffer_then_batched_texture_copies_wait_one_frame",0,0,4ull*1920*1080*4};
    }
    constexpr unsigned size=16384;
    auto data=std::make_shared<std::vector<unsigned>>(size);for(unsigned i=0;i<size;++i)(*data)[i]=i*19+3;
    auto source=s.buffer(*data,LLGL::BindFlags::CopySrc);auto destination=s.buffer(std::vector<unsigned>(size,0),LLGL::BindFlags::CopyDst|LLGL::BindFlags::CopySrc);
    const bool reuse=name=="resource_reuse";require(reuse||name=="resource_inflight","Unknown resource scenario");
    auto fences=std::make_shared<std::array<LLGL::Fence*,4>>();auto commands=std::make_shared<std::array<LLGL::CommandBuffer*,4>>();
    for(unsigned i=0;i<4;++i){(*fences)[i]=s.keep(s.device->CreateFence());(*commands)[i]=s.keep(s.device->CreateCommandBuffer());}
    return {[&s,data,source,destination,reuse,fences,commands]{
        if(reuse){s.begin();for(unsigned i=0;i<64;++i)s.cmd->CopyBuffer(*destination,0,*source,0,data->size()*4);s.submit();return;}
        std::array<LLGL::Buffer*,4> pending{};
        for(unsigned i=0;i<4;++i){LLGL::BufferDescriptor d;d.size=data->size()*4;d.bindFlags=LLGL::BindFlags::CopyDst|LLGL::BindFlags::CopySrc;pending[i]=s.device->CreateBuffer(d);require(pending[i]!=nullptr,"In-flight allocation failed");
            auto c=(*commands)[i];c->Begin();c->CopyBuffer(*pending[i],0,*source,0,d.size);c->CopyBuffer(*destination,0,*pending[i],0,d.size);c->End();s.device->GetCommandQueue()->Submit(*c);s.device->GetCommandQueue()->Submit(*(*fences)[i]);}
        for(unsigned i=0;i<4;++i){require(s.device->GetCommandQueue()->WaitFence(*(*fences)[i],UINT64_MAX),"In-flight fence timeout");s.device->Release(*pending[i]);}s.idle();
    },[&s,data,destination]{std::vector<unsigned> actual(data->size());s.device->ReadBuffer(*destination,0,actual.data(),actual.size()*4);s.idle();require(actual==*data,"Resource copy reference mismatch");++s.checks;},
        reuse?"C_reuse_persistent_64KB_buffers_64_copies_one_submit_wait":"C_four_submissions_retain_transient_resources_until_each_public_fence_then_release_no_premature_free",0,0,reuse?64ull*size*4:8ull*size*4};
}
Task bindingTask(Suite& s) {
    auto color=s.texture(64,64,LLGL::Format::RGBA8UNorm,LLGL::BindFlags::ColorAttachment|LLGL::BindFlags::CopySrc);auto target=s.target(color);
    const auto text=std::string(quadSource)+"Texture2D<float4> T:register(t0);float4 Sample(V v):SV_Target0{return T.Load(int3(0,0,0));}float4 Inverse(V v):SV_Target0{return float4(1-T.Load(int3(0,0,0)).rgb,1);}";
    LLGL::PipelineLayoutDescriptor l;l.bindings={{LLGL::ResourceType::Texture,LLGL::BindFlags::Sampled,LLGL::StageFlags::FragmentStage,0}};auto layout=s.keep(s.device->CreatePipelineLayout(l));
    LLGL::GraphicsPipelineDescriptor d;d.vertexShader=s.shader(compile(text,"VS","vs_5_1"),LLGL::ShaderType::Vertex);d.pipelineLayout=layout;d.renderPass=target->GetRenderPass();d.rasterizer.cullMode=LLGL::CullMode::Disabled;d.rasterizer.scissorTestEnabled=true;
    d.fragmentShader=s.shader(compile(text,"Sample","ps_5_1"),LLGL::ShaderType::Fragment);auto p0=s.pipeline(d);d.fragmentShader=s.shader(compile(text,"Inverse","ps_5_1"),LLGL::ShaderType::Fragment);auto p1=s.pipeline(d);
    std::array<LLGL::Texture*,2> tex{};for(unsigned i=0;i<2;++i){std::array<unsigned char,4> pixel{static_cast<unsigned char>(i?0:255),static_cast<unsigned char>(i?255:0),0,255};LLGL::ImageView image{LLGL::ImageFormat::RGBA,LLGL::DataType::UInt8,pixel.data(),pixel.size()};LLGL::TextureDescriptor t;t.extent={1,1,1};t.mipLevels=1;t.format=LLGL::Format::RGBA8UNorm;t.bindFlags=LLGL::BindFlags::Sampled;tex[i]=s.keep(s.device->CreateTexture(t,&image));}
    auto expected=std::make_shared<Bytes>(4096*4);for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){auto i=(y*64+x)*4;(*expected)[i]=(*expected)[i+3]=std::byte{255};if((x/8)%2)(*expected)[i+2]=std::byte{255};}
    return {[&s,target,p0,p1,tex]{s.begin();s.cmd->SetViewport(LLGL::Extent2D{64,64});s.cmd->BeginRenderPass(*target);for(unsigned i=0;i<256;++i){s.cmd->SetPipelineState(i%2?*p1:*p0);s.cmd->SetResource(0,*tex[i%2]);s.cmd->SetScissor(LLGL::Scissor{int(i%8)*8,int((i/8)%8)*8,8,8});s.cmd->Draw(6,0);}s.cmd->EndRenderPass();s.submit();},
        [&s,color,expected]{s.comparePixels(s.read(color,64,64),*expected);},"D_256_pipeline_texture_binding_scissor_switches_full_CPU_image_reference",256,0,0};
}
Task videoTask(Suite& s,const std::string& name) {
    const bool ten=name=="video_p010",layers=name=="video_layers",filter=name=="video_filter";
    const unsigned w=(name=="video_4k"||ten)?3840:1920,h=w==3840?2160:1080;
    const unsigned unit=ten?2:1,pitch=(w*unit+255)&~255u;
    auto luma=std::make_shared<Bytes>(static_cast<size_t>(pitch)*h,std::byte{0}),chroma=std::make_shared<Bytes>(static_cast<size_t>(pitch)*(h/2),std::byte{0});
    auto put=[ten](Bytes& b,size_t at,unsigned value){if(ten){const auto v=static_cast<std::uint16_t>(value<<6);std::memcpy(b.data()+at,&v,2);}else b[at]=static_cast<std::byte>(value);};
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)put(*luma,static_cast<size_t>(y)*pitch+x*unit,(ten?64:16)+(x+y)%(ten?877:220));
    for(unsigned y=0;y<h/2;++y)for(unsigned x=0;x<w/2;++x){auto at=static_cast<size_t>(y)*pitch+x*unit*2;put(*chroma,at,ten?512:90);put(*chroma,at+unit,ten?512:170);}
    auto yTex=s.texture(w,h,ten?LLGL::Format::R16UInt:LLGL::Format::R8UInt,LLGL::BindFlags::Sampled|LLGL::BindFlags::CopyDst);
    auto uvTex=s.texture(w/2,h/2,ten?LLGL::Format::RG16UInt:LLGL::Format::RG8UInt,LLGL::BindFlags::Sampled|LLGL::BindFlags::CopyDst);
    auto upload=[&s](size_t size){LLGL::BufferDescriptor d;d.size=size;d.bindFlags=LLGL::BindFlags::CopySrc|LLGL::BindFlags::CopyDst;return s.keep(s.device->CreateBuffer(d));};
    auto yBuffer=upload(luma->size()),uvBuffer=upload(chroma->size());
    auto color=s.texture(w,h,LLGL::Format::RGBA8UNorm,LLGL::BindFlags::ColorAttachment|LLGL::BindFlags::Sampled|LLGL::BindFlags::CopySrc);auto target=s.target(color,w,h);
    LLGL::PipelineLayoutDescriptor l;l.bindings={{LLGL::ResourceType::Texture,LLGL::BindFlags::Sampled,LLGL::StageFlags::FragmentStage,0},{LLGL::ResourceType::Texture,LLGL::BindFlags::Sampled,LLGL::StageFlags::FragmentStage,1}};auto layout=s.keep(s.device->CreatePipelineLayout(l));
    auto source=phase1::videoSource(ten);
    auto vs=s.shader(compile(source,"VS","vs_5_1"),LLGL::ShaderType::Vertex);
    LLGL::GraphicsPipelineDescriptor d;d.vertexShader=vs;d.fragmentShader=s.shader(compile(source,"Video","ps_5_1"),LLGL::ShaderType::Fragment);d.pipelineLayout=layout;d.renderPass=target->GetRenderPass();d.rasterizer.cullMode=LLGL::CullMode::Disabled;auto p=s.pipeline(d);
    LLGL::PipelineState* overlay=nullptr;LLGL::PipelineState* filtering=nullptr;LLGL::RenderTarget* finalTarget=nullptr;LLGL::Texture* finalColor=color;
    auto secondLuma=std::make_shared<Bytes>(),secondChroma=std::make_shared<Bytes>();LLGL::Texture *secondY=nullptr,*secondUV=nullptr;LLGL::Buffer *secondYBuffer=nullptr,*secondUVBuffer=nullptr;
    if(layers){
        secondLuma->assign(luma->size(),std::byte{0});secondChroma->assign(chroma->size(),std::byte{128});
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)(*secondLuma)[static_cast<size_t>(y)*pitch+x]=static_cast<std::byte>(112+x%32);
        secondY=s.texture(w,h,LLGL::Format::R8UInt,LLGL::BindFlags::Sampled|LLGL::BindFlags::CopyDst);secondUV=s.texture(w/2,h/2,LLGL::Format::RG8UInt,LLGL::BindFlags::Sampled|LLGL::BindFlags::CopyDst);secondYBuffer=upload(secondLuma->size());secondUVBuffer=upload(secondChroma->size());
        auto b=d;b.fragmentShader=s.shader(compile(source,"OverlayVideo","ps_5_1"),LLGL::ShaderType::Fragment);b.blend.targets[0].blendEnabled=true;b.blend.targets[0].srcColor=LLGL::BlendOp::SrcAlpha;b.blend.targets[0].dstColor=LLGL::BlendOp::InvSrcAlpha;b.blend.targets[0].srcAlpha=LLGL::BlendOp::One;b.blend.targets[0].dstAlpha=LLGL::BlendOp::InvSrcAlpha;overlay=s.pipeline(b);
    }
    if(filter){finalColor=s.texture(w,h,LLGL::Format::RGBA8UNorm,LLGL::BindFlags::ColorAttachment|LLGL::BindFlags::CopySrc);finalTarget=s.target(finalColor,w,h);LLGL::PipelineLayoutDescriptor b;b.bindings.push_back({LLGL::ResourceType::Texture,LLGL::BindFlags::Sampled,LLGL::StageFlags::FragmentStage,0});auto sample=s.keep(s.device->CreatePipelineLayout(b));auto f=d;f.fragmentShader=s.shader(compile(source,"Filter","ps_5_1"),LLGL::ShaderType::Fragment);f.pipelineLayout=sample;f.renderPass=finalTarget->GetRenderPass();filtering=s.pipeline(f);}
    auto expected=std::make_shared<Bytes>(phase1::videoReference(w,h,ten,layers,filter));
    return {[&s,luma,chroma,yBuffer,uvBuffer,yTex,uvTex,pitch,w,h,target,p,overlay,filtering,finalTarget,color,secondLuma,secondChroma,secondY,secondUV,secondYBuffer,secondUVBuffer]{
        if(overlay){s.device->WriteBuffer(*secondYBuffer,0,secondLuma->data(),secondLuma->size());s.device->WriteBuffer(*secondUVBuffer,0,secondChroma->data(),secondChroma->size());}
        s.device->WriteBuffer(*yBuffer,0,luma->data(),luma->size());s.device->WriteBuffer(*uvBuffer,0,chroma->data(),chroma->size());s.begin();
        LLGL::TextureRegion y;y.extent={w,h,1};LLGL::TextureRegion uv;uv.extent={w/2,h/2,1};s.cmd->CopyTextureFromBuffer(*yTex,y,*yBuffer,0,pitch);s.cmd->CopyTextureFromBuffer(*uvTex,uv,*uvBuffer,0,pitch);
        if(overlay){s.cmd->CopyTextureFromBuffer(*secondY,y,*secondYBuffer,0,pitch);s.cmd->CopyTextureFromBuffer(*secondUV,uv,*secondUVBuffer,0,pitch);}
        s.cmd->SetViewport(LLGL::Extent2D{w,h});s.cmd->BeginRenderPass(*target);s.cmd->SetPipelineState(*p);s.cmd->SetResource(0,*yTex);s.cmd->SetResource(1,*uvTex);s.cmd->Draw(6,0);if(overlay){s.cmd->SetPipelineState(*overlay);s.cmd->SetResource(0,*secondY);s.cmd->SetResource(1,*secondUV);s.cmd->Draw(6,0);}s.cmd->EndRenderPass();
        if(filtering){s.cmd->BeginRenderPass(*finalTarget);s.cmd->SetPipelineState(*filtering);s.cmd->SetResource(0,*color);s.cmd->Draw(6,0);s.cmd->EndRenderPass();}s.submit();
    },[&s,finalColor,w,h,expected]{s.comparePixels(s.read(finalColor,w,h),*expected,2);},
        phase1::videoContract(w,h,ten,layers,filter),layers||filter?2u:1u,0,static_cast<std::uint64_t>(w)*h*3*unit/2*(layers?2:1)};
}
} // namespace

constexpr std::array scenarioNames{"shader_cold","shader_warm","pso_hit","pso_miss","compute_blur","compute_color","compute_reduce","compute_buffer","compute_graphics","sprites_10000","sprites_100000","mesh_draw","mesh_instanced","multi_upload","resource_reuse","resource_inflight","switch_binding","video_1080","video_4k","video_p010","video_layers","video_filter"};
int main(int argc,char** argv) try {
    if(argc==2&&std::string_view(argv[1])=="--list") {std::cout<<'[';for(size_t i=0;i<scenarioNames.size();++i){if(i)std::cout<<',';std::cout<<bench::quote(scenarioNames[i]);}std::cout<<"]\n";return 0;}
    require(argc==4||(argc==5&&std::string_view(argv[4])=="--cpu-only"),"Usage: phase1_llgl <scenario> <samples> <validation:0|1> [--cpu-only]");
    const std::string scenario=argv[1]; size_t consumed=0;const auto samples=std::stoul(argv[2],&consumed);
    require(std::find(scenarioNames.begin(),scenarioNames.end(),scenario)!=scenarioNames.end(),"Unsupported phase1 scenario");
    require(consumed==std::strlen(argv[2])&&samples>=1&&samples<=10000,"Invalid sample count");require(std::string(argv[3])=="0"||std::string(argv[3])=="1","Invalid validation flag");
    Suite suite(std::string(argv[3])=="1",argc==4); Task task;
    if(scenario.starts_with("shader_")||scenario=="pso_hit"||scenario=="pso_miss") task=cacheTask(suite,scenario);
    else if(scenario.starts_with("compute_")) task=computeTask(suite,scenario);
    else if(scenario.starts_with("sprites_")||scenario=="mesh_draw"||scenario=="mesh_instanced") task=graphicsTask(suite,scenario);
    else if(scenario=="multi_upload"||scenario=="resource_reuse"||scenario=="resource_inflight") task=resourceTask(suite,scenario);
    else if(scenario.starts_with("video_")) task=videoTask(suite,scenario);
    else if(scenario=="switch_binding") task=bindingTask(suite);
    else throw std::runtime_error("Unsupported phase1 scenario");
    suite.gpuTiming=argc==4&&!(scenario.starts_with("shader_")||scenario.starts_with("pso_")||scenario.starts_with("resource_"));
    task.run();task.verify(); // Untimed independent correctness reference before warmup.
    constexpr unsigned warmup=3;std::vector<std::pair<double,std::optional<double>>> values;
    bench::Telemetry observer(suite.device->GetRendererInfo().deviceName.c_str(),static_cast<unsigned>(samples),false);
    for(size_t i=0;i<samples+warmup;++i){if(i==warmup)observer.begin();suite.idle();auto begin=bench::Clock::now();task.run();auto end=bench::Clock::now();if(i>=warmup){values.push_back({bench::ms(begin,end),suite.gpuMs});observer.checkpoint(false,static_cast<unsigned>(i-warmup));}}
    const auto observation=observer.finish();task.verify(); require(suite.checks>=2,"Missing pre/post reference validation");const auto checks=suite.checks;const auto env=suite.env;const auto cacheStats=task.stats();suite.close();
    std::cout<<std::setprecision(17)<<"{\"schema_version\":1,\"sdk\":\"llgl\",\"api\":\"d3d12\",\"category\":\"phase1_extension\",\"scenario\":"<<bench::quote(scenario)<<",\"samples\":[";
    for(size_t i=0;i<values.size();++i){if(i)std::cout<<',';std::cout<<"{\"frame_ms\":"<<values[i].first<<",\"gpu_ms\":";if(values[i].second)std::cout<<*values[i].second;else std::cout<<"null";std::cout<<'}';}
    std::cout<<"],\"warmup\":"<<warmup<<",\"validation\":"<<(std::string(argv[3])=="1"?"true":"false")<<",\"readback_verified\":"<<(scenario.starts_with("shader_")?"null":"true")<<",\"reference_verified\":true,\"reference_kind\":"<<bench::quote(scenario.starts_with("shader_")?"compiler_binary_exact_match":"full_gpu_readback_CPU_reference")<<",\"reference_checks\":"<<checks
        <<",\"draw_calls\":"<<task.draws<<",\"dispatch_calls\":"<<task.dispatches<<",\"payload_bytes\":"<<task.payload<<",\"contract\":"<<bench::quote(task.contract)
        <<",\"gpu_scope\":"<<bench::quote(suite.gpuTiming?"explicit_llgl_command_buffer_including_clear_copies_draws_excluding_WriteBuffer_internal_submissions_v1":"disabled")<<",\"cache_stats\":"<<cacheStats
        <<",\"telemetry\":"<<observation<<",\"environment\":"<<env<<",\"sdk_build_source_sha256\":\""<<GFX_LLGL_BUILD_SHA256<<"\"}\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
