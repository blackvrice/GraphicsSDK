#include <graphics/Graphics.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
namespace {
using F=gfx::Format;using S=gfx::ResourceState;using V=gfx::ShaderVisibility;using B=gfx::BindingType;
unsigned assertions{},images{},draws{};std::uint64_t verifiedBytes{};std::string fixture;std::filesystem::path output;
void check(bool v,const char* text){++assertions;if(!v)throw std::runtime_error(text);}
template<class T>T take(gfx::Result<T> r){if(!r)throw std::runtime_error(r.error().message+" native="+std::to_string(r.error().nativeCode));return std::move(*r);}
void take(gfx::Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
template<class T>void reject(const gfx::Result<T>& r,gfx::ErrorCode e,const char* text){check(!r&&r.error().code==e,text);}
gfx::DeviceDesc desc(){gfx::DeviceDesc d;d.backend=gfx::Backend::Metal;return d;}
gfx::ShaderBinary shader(std::string source,gfx::ShaderStage stage){gfx::ShaderBinary b;b.format=gfx::ShaderFormat::MSL;b.stage=stage;b.bytes.resize(source.size());std::memcpy(b.bytes.data(),source.data(),source.size());return b;}
gfx::GraphicsPipelineDesc pipeline(bool input=false,bool instance=false,bool uniform=false,unsigned textures=0,bool center=true,bool mrt=false,F format=F::RGBA8UNorm){
    std::string source="#define GFX_INPUT "+std::to_string(input)+"\n#define GFX_INSTANCE "+std::to_string(instance)+"\n#define GFX_UNIFORM "+std::to_string(uniform)+"\n#define GFX_TEXTURES "+std::to_string(textures)+"\n#define GFX_CENTER "+std::to_string(center)+"\n#define GFX_MRT "+std::to_string(mrt)+"\n"+fixture;
    gfx::GraphicsPipelineDesc p;p.vertex=shader(source,gfx::ShaderStage::Vertex);p.fragment=shader(source,gfx::ShaderStage::Fragment);p.colorFormat=format;
    if(input){p.vertexLayout.buffers={{0,12}};p.vertexLayout.attributes={{"POSITION",0,gfx::VertexFormat::Float2,0,0},{"COLOR",0,gfx::VertexFormat::UNorm8x4,0,8}};}
    if(instance){p.vertexLayout.buffers.push_back({1,16,gfx::VertexInputRate::Instance});p.vertexLayout.attributes.push_back({"TINT",0,gfx::VertexFormat::Float4,1,0});}
    if(uniform)p.bindingLayout.entries={{B::ConstantBuffer,0,1,0,V::Vertex},{B::ConstantBuffer,1,1,0,V::Fragment}};
    if(textures){p.bindingLayout.entries.push_back({B::Texture2D,0,textures,0,V::Fragment});p.bindingLayout.samplers={{0,0,V::Fragment,gfx::SamplerFilter::Point,gfx::SamplerAddress::Clamp}};}
    return p;
}
void empty(gfx::Device& d){take(d.queue().waitIdle());auto s=take(d.resourceStatistics());check(!s.trackedObjects&&!s.uniqueResources&&!s.resourceAllocationBytes&&!s.pendingSubmissions&&!s.pendingObjectReferences,"Graphics wrappers/native resources/submissions reclaimed");}
void finish(gfx::Device& d,gfx::CommandList& c){take(c.end());take(d.queue().wait(take(d.queue().submit(c))));}
void draw(gfx::CommandList& c,unsigned count=3,unsigned instances=1,unsigned first=0,unsigned firstInstance=0){take(c.draw(count,instances,first,firstInstance));++draws;}
struct Back{gfx::BufferPtr b;gfx::TextureDesc desc;gfx::TextureFootprint f;};
Back stage(gfx::Device& d,gfx::CommandList& c,const gfx::TexturePtr& t){auto f=t->footprint();auto b=take(d.createBuffer({f.size,gfx::Memory::Readback}));auto up=take(d.createBuffer({f.size,gfx::Memory::Upload}));take(up->write(0,std::vector<std::byte>(std::size_t(f.size),std::byte{0xa7})));take(c.copyBuffer(up,b,f.size));take(c.transition(t,S::CopySource));take(c.copyTextureToBuffer(t,b));return {b,t->description(),f};}
float half(std::uint16_t b){unsigned e=(b>>10)&31,f=b&1023;return std::ldexp(e?1.f+f/1024.f:f/1024.f,e?int(e)-15:-14)*(b&0x8000?-1.f:1.f);}
void verify(std::string name,const Back& b,gfx::Color expected,float depth=0,bool split=false,bool blend=false){
    auto bytes=take(b.b->read(0,b.f.size));for(unsigned y=0;y<b.desc.height;++y){auto base=std::size_t(y)*b.f.rowPitch;
        for(unsigned x=0;x<b.desc.width;++x){auto color=expected;if(split)color=x<32?gfx::Color{1,0,0,1}:gfx::Color{0,1,0,1};if(blend)color=(x>=5&&x<24&&y>=3&&y<14)?gfx::Color{.5,0,.5,1}:gfx::Color{0,0,1,1};
            if(b.desc.format==F::D32Float){float value;std::memcpy(&value,bytes.data()+base+x*4,4);check(std::isfinite(value)&&std::abs(value-depth)<1e-6f,"Every rasterized depth sample");continue;}
            std::array values{color.r,color.g,color.b,color.a};if(b.desc.format==F::BGRA8UNorm)std::swap(values[0],values[2]);for(unsigned c=0;c<4;++c){float value;
                if(b.desc.format==F::RGBA16Float){std::uint16_t bits;std::memcpy(&bits,bytes.data()+base+x*8+c*2,2);value=half(bits);}else value=std::to_integer<unsigned>(bytes[base+x*4+c])/255.f;
                check(std::isfinite(value)&&std::abs(value-values[c])<=(b.desc.format==F::RGBA16Float?.002f:1.01f/255),"Every draw pixel matches CPU reference");}}
        check(std::all_of(bytes.begin()+base+b.f.rowBytes,bytes.begin()+base+b.f.rowPitch,[](auto v){return v==std::byte{0xa7};}),"Every graphics row padding preserved");}
    if(!output.empty()){std::ofstream file(output/(name+".bin"),std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));check(file.good(),"Save full graphics readback");}
    verifiedBytes+=bytes.size();++images;
}
struct Params{std::array<float,4> tint;std::array<float,4> info;};
gfx::BufferPtr constants(gfx::Device& d,gfx::CommandList& c,gfx::Color color,float z=.5f,unsigned texture=0){auto b=take(d.createBuffer({768,gfx::Memory::Upload}));Params p{{color.r,color.g,color.b,color.a},{z,float(texture),0,0}};take(b->write(256,std::as_bytes(std::span(&p,1))));take(c.transition(b,S::ConstantBuffer));return b;}
gfx::ResourceBindings bindings(const gfx::BufferPtr& vs,const gfx::BufferPtr& fs){gfx::ResourceBindings r;r.constants={{0,0,vs,256,256},{1,0,fs,256,256}};return r;}
struct Vertex{float x,y;std::array<unsigned char,4> color;};static_assert(sizeof(Vertex)==12);
gfx::BufferPtr vertices(gfx::Device& d,gfx::CommandList& c,gfx::Color color,bool indexed){std::array<unsigned char,4> rgba{static_cast<unsigned char>(color.r*255),static_cast<unsigned char>(color.g*255),static_cast<unsigned char>(color.b*255),255};
    std::vector<Vertex> v{{0,0,rgba}};if(indexed)v.insert(v.end(),{{-1,-1,rgba},{1,-1,rgba},{-1,1,rgba},{1,1,rgba}});else v.insert(v.end(),{{-1,-1,rgba},{1,-1,rgba},{-1,1,rgba},{-1,1,rgba},{1,-1,rgba},{1,1,rgba}});
    auto b=take(d.createBuffer({16+v.size()*12,gfx::Memory::Upload}));take(b->write(16,std::as_bytes(std::span(v))));take(c.transition(b,S::VertexBuffer));take(c.bindVertexBuffer(0,b,12,16,v.size()*12));return b;
}
void basic(gfx::Device& d,unsigned mode){
    bool input=mode!=0,instance=mode==4,indexed=mode==2||mode==3;auto p=take(d.createGraphicsPipeline(pipeline(input,instance)));auto t=take(d.createTexture({65,17,F::RGBA8UNorm,true}));auto c=take(d.createCommandList());take(c->begin());
    gfx::BufferPtr v,ib,inst;if(input)v=vertices(d,*c,mode==1?gfx::Color{0,1,0,1}:mode==4?gfx::Color{1,1,1,1}:gfx::Color{0,0,1,1},indexed);
    if(instance){std::array<std::array<float,4>,3> data{{{1,0,0,1},{0,1,0,1},{0,0,1,1}}};inst=take(d.createBuffer({sizeof(data),gfx::Memory::Upload}));take(inst->write(0,std::as_bytes(std::span(data))));take(c->transition(inst,S::VertexBuffer));take(c->bindVertexBuffer(1,inst,16));}
    if(indexed){if(mode==2){std::array<std::uint16_t,9> data{0,0,0,0,1,2,2,1,3};ib=take(d.createBuffer({6+sizeof(data),gfx::Memory::Upload}));take(ib->write(6,std::as_bytes(std::span(data))));take(c->transition(ib,S::IndexBuffer));take(c->bindIndexBuffer(ib,gfx::IndexFormat::UInt16,6,sizeof(data)));}
        else{std::array<std::uint32_t,9> data{0,0,0,0,1,2,2,1,3};ib=take(d.createBuffer({16+sizeof(data),gfx::Memory::Upload}));take(ib->write(16,std::as_bytes(std::span(data))));take(c->transition(ib,S::IndexBuffer));take(c->bindIndexBuffer(ib,gfx::IndexFormat::UInt32,16,sizeof(data)));}}
    take(c->transition(t,S::RenderTarget));take(c->beginRenderPass(t,{0,0,0,1}));take(c->setPipeline(p));
    if(indexed){take(c->drawIndexed(6,1,3,1));++draws;}else draw(*c,input?6:3,instance?2:1,input?1:0,instance?1:0);
    take(c->endRenderPass());auto back=stage(d,*c,t);std::weak_ptr<gfx::Pipeline> weak=p;p.reset();v.reset();ib.reset();inst.reset();t.reset();check(weak.expired(),"Recording owns native pipeline, not wrapper");finish(d,*c);c.reset();
    verify(std::array{"procedural","vertex","index16","index32","instance"}[mode],back,mode==0?gfx::Color{1,0,0,1}:mode==1?gfx::Color{0,1,0,1}:gfx::Color{0,0,1,1});
}
void snapshots(gfx::Device& d){
    auto p=take(d.createGraphicsPipeline(pipeline(false,false,true)));auto t=take(d.createTexture({65,17,F::RGBA8UNorm,true}));auto c=take(d.createCommandList());take(c->begin());auto vs=constants(d,*c,{1,1,1,1}),red=constants(d,*c,{1,0,0,1}),green=constants(d,*c,{0,1,0,1});
    take(c->transition(t,S::RenderTarget));take(c->beginRenderPass(t,{}));take(c->setPipeline(p));take(c->bindResources(bindings(vs,red)));take(c->setScissor(0,0,32,17));draw(*c);
    take(c->bindResources(bindings(vs,green)));auto bad=bindings(vs,red);bad.constants.push_back(bad.constants.back());reject(c->bindResources(bad),gfx::ErrorCode::InvalidArgument,"Failed snapshot preserves previous complete binding");
    take(c->setScissor(32,0,33,17));draw(*c);take(c->endRenderPass());auto back=stage(d,*c,t);vs.reset();red.reset();green.reset();p.reset();t.reset();finish(d,*c);c.reset();verify("snapshots",back,{},0,true);
}
gfx::TexturePtr checker(gfx::Device& d,gfx::CommandList& c,bool solid){auto t=take(d.createTexture({2,2}));auto f=t->footprint();std::vector<std::byte> data(std::size_t(f.size));std::array<std::array<unsigned char,4>,4> colors{{{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,255,255}}};
    for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)std::memcpy(data.data()+y*f.rowPitch+x*4,colors[solid?0:y*2+x].data(),4);auto up=take(d.createBuffer({f.size,gfx::Memory::Upload}));take(up->write(0,data));take(c.transition(t,S::CopyDestination));take(c.copyBufferToTexture(up,t));take(c.transition(t,S::ShaderRead));return t;
}
void sampling(gfx::Device& d,unsigned mode){bool convenience=mode==0,center=mode==0||mode==3;auto pd=pipeline(false,false,!convenience,convenience?1:2,center);if(convenience){pd.bindingLayout={};pd.sampledTexture=true;}else{pd.bindingLayout.samplers[0].filter=mode>=3?gfx::SamplerFilter::Linear:gfx::SamplerFilter::Point;pd.bindingLayout.samplers[0].address=mode==2||mode==4?gfx::SamplerAddress::Repeat:gfx::SamplerAddress::Clamp;}
    auto p=take(d.createGraphicsPipeline(pd));auto t=take(d.createTexture({65,17,F::RGBA8UNorm,true}));auto c=take(d.createCommandList());take(c->begin());auto image=checker(d,*c,false);gfx::TexturePtr unused;gfx::BufferPtr vs,fs;
    if(!convenience){unused=checker(d,*c,true);vs=constants(d,*c,{1,1,1,1});fs=constants(d,*c,{1,1,1,1},.5f,1);}
    take(c->transition(t,S::RenderTarget));take(c->beginRenderPass(t,{}));take(c->setPipeline(p));if(convenience)take(c->bindTexture(image));else{auto r=bindings(vs,fs);r.textures={{2,0,unused},{2,1,image}};take(c->bindResources(r));}draw(*c);take(c->endRenderPass());auto back=stage(d,*c,t);image.reset();unused.reset();vs.reset();fs.reset();p.reset();t.reset();finish(d,*c);c.reset();
    auto color=center?gfx::Color{.5,.5,.5,1}:mode==1?gfx::Color{0,1,0,1}:gfx::Color{0,0,1,1};verify(std::array{"sampled","point-clamp","point-repeat","linear-center","linear-repeat"}[mode],back,color);
}
void depthMrt(gfx::Device& d){auto pd=pipeline(false,false,true,0,true,true);pd.renderTargets=gfx::RenderTargetLayout{{F::RGBA8UNorm,F::BGRA8UNorm},F::D32Float};pd.depthState={true,true,gfx::CompareOp::Less};auto write=take(d.createGraphicsPipeline(pd));pd.depthState.writeEnabled=false;auto read=take(d.createGraphicsPipeline(pd));
    auto a=take(d.createTexture({65,17,F::RGBA8UNorm,true})),b=take(d.createTexture({65,17,F::BGRA8UNorm,true})),z=take(d.createTexture({65,17,F::D32Float,false,true}));auto c=take(d.createCommandList());take(c->begin());
    auto near=constants(d,*c,{1,1,1,1},.25f),far=constants(d,*c,{1,1,1,1},.75f),front=constants(d,*c,{1,1,1,1},.1f),green=constants(d,*c,{0,1,0,1}),red=constants(d,*c,{1,0,0,1}),blue=constants(d,*c,{0,0,1,1});
    take(c->transition(a,S::RenderTarget));take(c->transition(b,S::RenderTarget));take(c->transition(z,S::DepthWrite));gfx::RenderPassDesc pass{{{a,gfx::LoadOp::Clear,{}},{b,gfx::LoadOp::Clear,{}}},{z,gfx::LoadOp::Clear,1}};
    take(c->beginRenderPass(pass));take(c->setPipeline(write));take(c->bindResources(bindings(near,green)));draw(*c);take(c->bindResources(bindings(far,red)));draw(*c);take(c->endRenderPass());take(c->transition(z,S::DepthRead));pass.colors[0].load=pass.colors[1].load=gfx::LoadOp::Load;pass.depth.load=gfx::LoadOp::Load;pass.depth.readOnly=true;
    take(c->beginRenderPass(pass));take(c->setPipeline(write));take(c->bindResources(bindings(front,blue)));reject(c->draw(3),gfx::ErrorCode::InvalidState,"Read-only depth rejects writing pipeline");take(c->setPipeline(read));take(c->bindResources(bindings(front,blue)));draw(*c);take(c->endRenderPass());
    auto ba=stage(d,*c,a),bb=stage(d,*c,b),bz=stage(d,*c,z);finish(d,*c);verify("mrt-color",ba,{0,0,1,1});verify("mrt-bgra",bb,{1,0,0,1});verify("mrt-depth",bz,{},.25f);
}
void uniformImage(gfx::Device& d,std::string name,F format,gfx::Color color,bool blend=false){auto pd=pipeline(false,false,true,0,true,false,format);pd.alphaBlend=blend;auto p=take(d.createGraphicsPipeline(pd));auto t=take(d.createTexture({65,17,format,true}));auto c=take(d.createCommandList());take(c->begin());auto vs=constants(d,*c,{1,1,1,1}),fs=constants(d,*c,color);
    take(c->transition(t,S::RenderTarget));take(c->beginRenderPass(t,{0,0,1,1}));take(c->setPipeline(p));take(c->bindResources(bindings(vs,fs)));if(blend)take(c->setScissor(5,3,19,11));draw(*c);take(c->endRenderPass());auto back=stage(d,*c,t);finish(d,*c);verify(name,back,color,0,false,blend);
}
void depthOnly(gfx::Device& d){auto pd=pipeline();pd.fragment={};pd.renderTargets=gfx::RenderTargetLayout{{},F::D32Float};pd.depthState={true,true,gfx::CompareOp::Less};auto p=take(d.createGraphicsPipeline(pd));auto t=take(d.createTexture({65,17,F::D32Float,false,true}));auto c=take(d.createCommandList());take(c->begin());take(c->transition(t,S::DepthWrite));take(c->beginRenderPass({{},{t,gfx::LoadOp::Clear,1}}));take(c->setPipeline(p));draw(*c);take(c->endRenderPass());auto back=stage(d,*c,t);finish(d,*c);verify("depth-only",back,{},.5f);}
void contracts(gfx::Device& d){
    auto pd=pipeline();pd.vertex.stage=gfx::ShaderStage::Fragment;reject(d.createGraphicsPipeline(pd),gfx::ErrorCode::InvalidArgument,"Wrong MSL stage");pd=pipeline();pd.vertex.bytes={std::byte{0}};reject(d.createGraphicsPipeline(pd),gfx::ErrorCode::InvalidArgument,"Embedded NUL source");pd=pipeline();pd.vertex=shader("invalid msl",gfx::ShaderStage::Vertex);reject(d.createGraphicsPipeline(pd),gfx::ErrorCode::BackendFailure,"Actual MSL compiler diagnostics");
    pd=pipeline();pd.vertex=shader("#include <metal_stdlib>\nusing namespace metal; vertex float4 other(uint id[[vertex_id]]){return float4(id);}",gfx::ShaderStage::Vertex);reject(d.createGraphicsPipeline(pd),gfx::ErrorCode::InvalidArgument,"Missing fixed vertex entry");
    pd=pipeline(true);pd.vertexLayout.attributes[0].format=gfx::VertexFormat::Float3;pd.vertexLayout.buffers[0].stride=16;pd.vertexLayout.attributes[1].offset=12;reject(d.createGraphicsPipeline(pd),gfx::ErrorCode::InvalidArgument,"Native attribute type mismatch");
    pd=pipeline(false,false,true);pd.bindingLayout.entries[1].visibility=V::Vertex;reject(d.createGraphicsPipeline(pd),gfx::ErrorCode::InvalidArgument,"Native reflection visibility mismatch");pd=pipeline(false,false,true);pd.bindingLayout.entries[0].space=1;reject(d.createGraphicsPipeline(pd),gfx::ErrorCode::Unsupported,"Metal space-zero profile");
    auto p=take(d.createGraphicsPipeline(pipeline(false,false,true)));auto other=take(gfx::createDevice(desc()));auto foreign=take(other->createGraphicsPipeline(pipeline(false,false,true)));auto t=take(d.createTexture({65,17,F::RGBA8UNorm,true}));auto c=take(d.createCommandList());take(c->begin());auto vs=constants(d,*c,{1,1,1,1}),fs=constants(d,*c,{1,0,0,1});take(c->transition(t,S::RenderTarget));take(c->beginRenderPass(t,{}));take(c->setPipeline(p));
    reject(c->setPipeline(foreign),gfx::ErrorCode::WrongDevice,"Foreign pipeline");reject(c->draw(3),gfx::ErrorCode::InvalidState,"Missing complete resources");auto r=bindings(vs,fs);r.constants.pop_back();reject(c->bindResources(r),gfx::ErrorCode::InvalidArgument,"Missing binding element");r=bindings(vs,fs);r.constants[1].offset=1;reject(c->bindResources(r),gfx::ErrorCode::InvalidArgument,"Unaligned constant view");take(c->bindResources(bindings(vs,fs)));reject(c->setScissor(UINT32_MAX,0,1,1),gfx::ErrorCode::InvalidArgument,"Scissor overflow");reject(c->draw(0),gfx::ErrorCode::InvalidArgument,"Zero draw");reject(c->draw(3,UINT32_MAX,0,1),gfx::ErrorCode::InvalidArgument,"Instance overflow");reject(c->drawIndexed(3),gfx::ErrorCode::InvalidState,"Missing index view");
    bool wrong{};std::thread thread([&]{auto value=c->draw(3);wrong=!value&&value.error().code==gfx::ErrorCode::InvalidState;});thread.join();check(wrong,"Draw thread guard");draw(*c);take(c->endRenderPass());take(c->end());take(vs->write(768,{}));reject(d.queue().submit(*c),gfx::ErrorCode::InvalidState,"CPU update invalidates accepted draw bindings");
}
}
int main(int argc,char** argv)try{
    check(argc==2||argc==4,"Fixture, optional expected fingerprint/output");std::ifstream file(argv[1],std::ios::binary);std::ostringstream text;text<<file.rdbuf();check(file.good()||file.eof(),"Read fixed MSL fixture");fixture=text.str();check(!fixture.empty(),"Nonempty MSL fixture");
    if(argc==4){check(gfx::buildFingerprint()==argv[2],"Installed/source fingerprint");output=argv[3];check(std::filesystem::create_directory(output),"Fresh full draw payload directory");}
    auto d=take(gfx::createDevice(desc()));for(unsigned i=0;i<5;++i){basic(*d,i);empty(*d);}snapshots(*d);empty(*d);for(unsigned i=0;i<5;++i){sampling(*d,i);empty(*d);}depthMrt(*d);empty(*d);uniformImage(*d,"blend-scissor",F::RGBA8UNorm,{1,0,0,.5},true);empty(*d);depthOnly(*d);empty(*d);uniformImage(*d,"hdr",F::RGBA16Float,{-2,.5f,4,1});empty(*d);uniformImage(*d,"bgra",F::BGRA8UNorm,{1,0,0,1});empty(*d);contracts(*d);empty(*d);
    std::cout<<"{\"schema_version\":1,\"contract\":\"metal_graphics_v1\",\"SDK_source_fingerprint\":\""<<gfx::buildFingerprint()<<"\",\"images\":"<<images<<",\"recorded_draws\":"<<draws<<",\"verified_bytes\":"<<verifiedBytes<<",\"assertions\":"<<assertions<<",\"draw_executed\":true,\"native_validation_accepted\":false,\"physical_GPU_accepted\":false,\"performance_accepted\":false}\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
