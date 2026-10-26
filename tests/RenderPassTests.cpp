#include <graphics/Graphics.hpp>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
unsigned assertions=0,images=0;
void check(bool value,const char* message){++assertions;if(!value)throw std::runtime_error(message);}
template<class T>T take(gfx::Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(*r);}
void take(gfx::Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
template<class T>void reject(gfx::Result<T> r,gfx::ErrorCode code,const char* message){check(!r&&r.error().code==code,message);}
using F=gfx::Format;using S=gfx::ResourceState;using L=gfx::LoadOp;using C=gfx::CompareOp;using Pixel=std::array<float,4>;
struct Stub final:gfx::Texture {gfx::TextureDesc d;explicit Stub(gfx::TextureDesc value):d(value){}gfx::TextureDesc description()const override{return d;}gfx::TextureFootprint footprint()const override{return {};}};
void cpu(){
    take(gfx::validateTextureDescription({3,7,F::BGRA8UNorm,true}));take(gfx::validateRenderTargets({{F::BGRA8UNorm,F::RGBA8UNorm},{}},{}));
    take(gfx::validateTextureDescription({3,7,F::D32Float,false,true}));take(gfx::validateRenderTargets({{},F::D32Float},{true,true,C::Less}));
    for(auto d:{gfx::TextureDesc{0,1},gfx::TextureDesc{1,16385},gfx::TextureDesc{1,1,F::D32Float},gfx::TextureDesc{1,1,F::D32Float,true,true},gfx::TextureDesc{1,1,F::RGBA8UNorm,false,true},gfx::TextureDesc{1,1,static_cast<F>(99)}})
        reject(gfx::validateTextureDescription(d),gfx::ErrorCode::InvalidArgument,"Malformed texture descriptor");
    reject(gfx::validateRenderTargets({{}, {}}),gfx::ErrorCode::InvalidArgument,"Empty output layout");
    reject(gfx::validateRenderTargets({std::vector<F>(9,F::RGBA8UNorm),{}}),gfx::ErrorCode::InvalidArgument,"MRT count limit");
    reject(gfx::validateRenderTargets({{F::D32Float},{}}),gfx::ErrorCode::InvalidArgument,"Depth format is not color");
    reject(gfx::validateRenderTargets({{F::RGBA8UNorm},F::RGBA8UNorm}),gfx::ErrorCode::InvalidArgument,"Invalid depth format");
    for(auto state:{gfx::DepthState{true,false},gfx::DepthState{false,true},gfx::DepthState{false,false,static_cast<C>(99)}})
        reject(gfx::validateRenderTargets({},state),gfx::ErrorCode::InvalidArgument,"Invalid/missing depth state");
    auto color=std::make_shared<Stub>(gfx::TextureDesc{16,16,F::RGBA8UNorm,true});auto depth=std::make_shared<Stub>(gfx::TextureDesc{16,16,F::D32Float,false,true});
    gfx::RenderPassDesc good{{{color}}, {depth}};take(gfx::validateRenderPass(good));
    for(int i=0;i<11;++i){auto p=good;switch(i){
    case 0:p={};break;case 1:p.colors[0].texture.reset();break;case 2:p.colors.push_back(p.colors[0]);break;
    case 3:p.colors[0].texture=depth;break;case 4:p.depth.texture=color;break;
    case 5:p.colors[0].load=static_cast<L>(99);break;case 6:p.depth.load=static_cast<L>(99);break;
    case 7:p.depth.clear=std::numeric_limits<float>::quiet_NaN();break;case 8:p.depth.clear=1.01f;break;
    case 9:p.depth.readOnly=true;break;case 10:p.colors[0].clear.r=std::numeric_limits<float>::infinity();break;
    }reject(gfx::validateRenderPass(p),gfx::ErrorCode::InvalidArgument,"Malformed render pass");}
    good.colors[0].texture=std::make_shared<Stub>(gfx::TextureDesc{17,16,F::RGBA8UNorm,true});reject(gfx::validateRenderPass(good),gfx::ErrorCode::InvalidArgument,"Attachment dimensions must match");
    good.colors.clear();good.depth.readOnly=true;good.depth.load=L::Load;take(gfx::validateRenderPass(good));
}
constexpr auto vertex=R"(
cbuffer Draw:register(b0){float4 color;float4 geometry;}
float4 VS(uint id:SV_VertexID):SV_Position{float2 p=float2((id<<1)&2,id&2);return float4(p*2-1,geometry.x,1);})";
constexpr auto one=R"(cbuffer Draw:register(b0){float4 color;float4 geometry;}float4 PS():SV_Target0{return color;})";
constexpr auto two=R"(cbuffer Draw:register(b0){float4 color;float4 geometry;}struct Out{float4 a:SV_Target0;float4 b:SV_Target1;};Out PS(){Out o;o.a=color;o.b=float4(1-color.rgb,color.a);return o;})";
struct Params {Pixel color;Pixel geometry;};
gfx::BufferPtr constants(gfx::Device& d){auto b=take(d.createBuffer({2048,gfx::Memory::Upload}));return b;}
void write(gfx::BufferPtr b,unsigned slot,Pixel color,float depth){Params p{color,{depth,0,0,0}};take(b->write(slot*256,std::as_bytes(std::span{&p,1})));}
gfx::GraphicsPipelineDesc description(gfx::RenderTargetLayout targets,gfx::DepthState depth={},std::string_view ps=one){
    gfx::GraphicsPipelineDesc p;p.vertex=take(gfx::compileHlsl(vertex,"VS",gfx::ShaderStage::Vertex));
    if(!ps.empty())p.fragment=take(gfx::compileHlsl(ps,"PS",gfx::ShaderStage::Fragment));
    p.renderTargets=targets;p.depthState=depth;p.bindingLayout={{{gfx::BindingType::ConstantBuffer,0,1,0,gfx::ShaderVisibility::All}}, {}};return p;
}
gfx::TexturePtr color(gfx::Device& d,F format=F::RGBA8UNorm,unsigned width=48,unsigned height=16){return take(d.createTexture({width,height,format,true}));}
gfx::TexturePtr depth(gfx::Device& d,unsigned width=48,unsigned height=16){return take(d.createTexture({width,height,F::D32Float,false,true}));}
void draw(gfx::CommandList& c,gfx::BufferPtr b,unsigned slot){take(c.bindResources({{},{{0,0,b,slot*256ull,256}}}));take(c.draw(3));}
struct Readback {gfx::BufferPtr buffer;gfx::TextureFootprint f;F format;};
Readback copy(gfx::Device& d,gfx::CommandList& c,gfx::TexturePtr t){auto f=t->footprint();auto b=take(d.createBuffer({f.size,gfx::Memory::Readback}));take(c.transition(t,S::CopySource));take(c.copyTextureToBuffer(t,b));return {b,f,t->description().format};}
float half(std::uint16_t bits){const auto exponent=(bits>>10)&31;const auto fraction=bits&1023;return std::ldexp(exponent?1.f+fraction/1024.f:fraction/1024.f,exponent?int(exponent)-15:-14)*(bits&0x8000?-1.f:1.f);}
void verify(const Readback& r,std::function<Pixel(unsigned,unsigned)> expected){
    auto data=take(r.buffer->read(0,r.f.size));const auto pixelBytes=r.format==F::RGBA16Float?8u:4u;
    for(unsigned y=0;y<r.f.height;++y)for(unsigned x=0;x<r.f.rowBytes/pixelBytes;++x){auto e=expected(x,y);
        for(unsigned i=0;i<4;++i){float value;
            if(r.format==F::RGBA8UNorm)value=std::to_integer<unsigned char>(data[y*r.f.rowPitch+x*4+i])/255.f;
            else{std::uint16_t bits{};std::memcpy(&bits,data.data()+y*r.f.rowPitch+x*8+i*2,2);value=half(bits);}
            check(std::isfinite(value)&&std::abs(value-e[i])<=(r.format==F::RGBA8UNorm?1.01f/255:0.001f),"Full render color CPU reference");
        }
    }++images;
}
void verifyDepth(const Readback& r,std::function<float(unsigned,unsigned)> expected){
    auto data=take(r.buffer->read(0,r.f.size));for(unsigned y=0;y<r.f.height;++y)for(unsigned x=0;x<r.f.rowBytes/4;++x){float value;std::memcpy(&value,data.data()+y*r.f.rowPitch+x*4,4);check(std::isfinite(value)&&std::abs(value-expected(x,y))<0.000001f,"Full D32 CPU reference");}++images;
}
void submit(gfx::Device& d,std::unique_ptr<gfx::CommandList>& c){take(c->end());auto fence=take(d.queue().submit(*c));c.reset();take(d.queue().wait(fence));}
bool compare(C op,float a,float b){switch(op){case C::Never:return false;case C::Less:return a<b;case C::Equal:return a==b;case C::LessEqual:return a<=b;case C::Greater:return a>b;case C::NotEqual:return a!=b;case C::GreaterEqual:return a>=b;case C::Always:return true;}return false;}
void comparisons(gfx::Device& d){
    auto b=constants(d);for(unsigned i=0;i<3;++i)write(b,i,{1,0,0,1},.25f*(i+1));
    for(unsigned op=0;op<8;++op)for(bool writes:{false,true}){
        auto p=take(d.createGraphicsPipeline(description({{F::RGBA8UNorm},F::D32Float},{true,writes,static_cast<C>(op)})));auto t=color(d),z=depth(d);auto c=take(d.createCommandList());take(c->begin());take(c->transition(b,S::ConstantBuffer));take(c->transition(t,S::RenderTarget));take(c->transition(z,S::DepthWrite));
        take(c->beginRenderPass({{{t,L::Clear,{0,0,1,1}}},{z,L::Clear,.5f}}));take(c->setPipeline(p));for(unsigned i=0;i<3;++i){take(c->setScissor(i*16,0,16,16));draw(*c,b,i);}take(c->endRenderPass());auto image=copy(d,*c,t),zImage=copy(d,*c,z);submit(d,c);
        verify(image,[=](unsigned x,unsigned){return compare(static_cast<C>(op),.25f*(x/16+1),.5f)?Pixel{1,0,0,1}:Pixel{0,0,1,1};});
        verifyDepth(zImage,[=](unsigned x,unsigned){float value=.25f*(x/16+1);return writes&&compare(static_cast<C>(op),value,.5f)?value:.5f;});
    }std::cout<<"full_image depth_compare 8_functions write_mask_on_off passed\n";
}
void mrtAndLoad(gfx::Device& d,bool reverse){
    auto b=constants(d);write(b,0,{1,0,0,1},.75f);write(b,1,{0,1,0,1},.25f);write(b,2,{0,0,1,1},.5f);write(b,3,{1,0,0,1},.25f);
    const gfx::RenderTargetLayout layout{{F::RGBA8UNorm,F::RGBA16Float},F::D32Float};
    auto p=take(d.createGraphicsPipeline(description(layout,{true,true,C::Less},two)));auto readOnly=take(d.createGraphicsPipeline(description(layout,{true,false,C::LessEqual},two)));
    auto a=color(d),bTarget=color(d,F::RGBA16Float),z=depth(d);auto c=take(d.createCommandList());take(c->begin());take(c->transition(b,S::ConstantBuffer));take(c->transition(a,S::RenderTarget));take(c->transition(bTarget,S::RenderTarget));take(c->transition(z,S::DepthWrite));
    gfx::RenderPassDesc pass{{{a,L::Clear,{0,0,0,1}},{bTarget,L::Clear,{0,0,0,1}}},{z}};take(c->beginRenderPass(pass));take(c->setPipeline(p));
    if(!reverse)draw(*c,b,0);take(c->setScissor(0,0,24,16));draw(*c,b,1);take(c->setScissor(24,0,24,16));draw(*c,b,2);if(reverse){take(c->setScissor(0,0,48,16));draw(*c,b,0);}take(c->endRenderPass());
    auto first=copy(d,*c,a),second=copy(d,*c,bTarget),zFirst=copy(d,*c,z);
    take(c->transition(a,S::RenderTarget));take(c->transition(bTarget,S::RenderTarget));take(c->transition(z,S::DepthRead));
    pass.colors[0].load=pass.colors[1].load=L::Load;pass.depth={z,L::Load,1,true};take(c->beginRenderPass(pass));
    take(c->setPipeline(p));take(c->bindResources({{},{{0,0,b,0,256}}}));reject(c->draw(3),gfx::ErrorCode::InvalidState,"Read-only depth rejects writing pipeline");take(c->setPipeline(readOnly));take(c->setScissor(0,0,16,16));draw(*c,b,3);take(c->endRenderPass());
    auto loaded=copy(d,*c,a),loadedSecond=copy(d,*c,bTarget),zLoaded=copy(d,*c,z);
    // Both MRT outputs become inputs to an ordinary graphics postprocess.
    constexpr auto vs="float4 VS(uint id:SV_VertexID):SV_Position{float2 p=float2((id<<1)&2,id&2);return float4(p*2-1,.5,1);}";
    constexpr auto ps="Texture2D<float4> a:register(t0);Texture2D<float4> b:register(t1);float4 PS(float4 p:SV_Position):SV_Target{return float4(a.Load(int3(p.xy,0)).r,b.Load(int3(p.xy,0)).b,0,1);}";
    gfx::GraphicsPipelineDesc post;post.vertex=take(gfx::compileHlsl(vs,"VS",gfx::ShaderStage::Vertex));post.fragment=take(gfx::compileHlsl(ps,"PS",gfx::ShaderStage::Fragment));post.bindingLayout={{{gfx::BindingType::Texture2D,0,2}}, {}};auto postP=take(d.createGraphicsPipeline(post));auto result=color(d);
    take(c->transition(a,S::ShaderRead));take(c->transition(bTarget,S::ShaderRead));take(c->transition(result,S::RenderTarget));take(c->beginRenderPass(result,{0,0,0,1}));take(c->setPipeline(postP));take(c->bindResources({{{0,0,a},{0,1,bTarget}}, {}}));take(c->draw(3));take(c->endRenderPass());auto output=copy(d,*c,result);
    // Drop every resource/pipeline wrapper before waiting; native batches retain them.
    take(c->end());auto fence=take(d.queue().submit(*c));c.reset();p.reset();readOnly.reset();postP.reset();pass={};a.reset();bTarget.reset();z.reset();result.reset();b.reset();take(d.queue().wait(fence));
    verify(first,[](unsigned x,unsigned){return x<24?Pixel{0,1,0,1}:Pixel{0,0,1,1};});verify(second,[](unsigned x,unsigned){return x<24?Pixel{1,0,1,1}:Pixel{1,1,0,1};});verifyDepth(zFirst,[](unsigned x,unsigned){return x<24?.25f:.5f;});
    verify(loaded,[](unsigned x,unsigned){return x<16?Pixel{1,0,0,1}:x<24?Pixel{0,1,0,1}:Pixel{0,0,1,1};});verify(loadedSecond,[](unsigned x,unsigned){return x<16?Pixel{0,1,1,1}:x<24?Pixel{1,0,1,1}:Pixel{1,1,0,1};});verifyDepth(zLoaded,[](unsigned x,unsigned){return x<24?.25f:.5f;});verify(output,[](unsigned x,unsigned){return Pixel{x<16?1.f:0.f,x<24?1.f:0.f,0,1};});
    std::cout<<"full_image MRT_depth_load_readonly_postprocess reverse="<<reverse<<" passed\n";
}
void depthOnly(gfx::Device& d){
    auto cb=constants(d);write(cb,0,{0,1,0,1},.25f);write(cb,1,{0,1,0,1},.5f);auto z=depth(d),t=color(d);
    auto p=take(d.createGraphicsPipeline(description({{},F::D32Float},{true,true,C::Always},{})));auto pColor=take(d.createGraphicsPipeline(description({{F::RGBA8UNorm},F::D32Float},{true,false,C::Less})));
    auto c=take(d.createCommandList());take(c->begin());take(c->transition(cb,S::ConstantBuffer));take(c->transition(z,S::DepthWrite));take(c->beginRenderPass({{}, {z,L::Clear,.75f}}));take(c->setPipeline(p));take(c->setScissor(0,0,24,16));draw(*c,cb,0);take(c->endRenderPass());take(c->transition(z,S::DepthRead));take(c->transition(t,S::RenderTarget));take(c->beginRenderPass({{{t,L::Clear,{0,0,1,1}}},{z,L::Load,1,true}}));take(c->setPipeline(pColor));draw(*c,cb,1);take(c->endRenderPass());auto image=copy(d,*c,t),zImage=copy(d,*c,z);submit(d,c);
    verify(image,[](unsigned x,unsigned){return x<24?Pixel{0,0,1,1}:Pixel{0,1,0,1};});verifyDepth(zImage,[](unsigned x,unsigned){return x<24?.25f:.75f;});std::cout<<"full_image depth_only omitted_fragment passed\n";
}
void eightAndBlend(gfx::Device& d){
    auto cb=constants(d);write(cb,0,{1,0,0,.5f},.5f);std::string shader="struct Out{";for(unsigned i=0;i<8;++i)shader+="float4 c"+std::to_string(i)+":SV_Target"+std::to_string(i)+";";shader+="};Out PS(){Out o;";
    for(unsigned i=0;i<8;++i)shader+="o.c"+std::to_string(i)+"=float4("+std::to_string(i&1)+","+std::to_string((i>>1)&1)+","+std::to_string((i>>2)&1)+",1);";shader+="return o;}";
    gfx::RenderTargetLayout layout;layout.colors.clear();gfx::RenderPassDesc pass;std::vector<gfx::TexturePtr> targets;
    for(unsigned i=0;i<8;++i){auto format=i%2?F::RGBA16Float:F::RGBA8UNorm;layout.colors.push_back(format);targets.push_back(color(d,format,17,9));pass.colors.push_back({targets.back(),L::Clear,{1,1,1,1}});}
    auto p=take(d.createGraphicsPipeline(description(layout,{},shader)));auto c=take(d.createCommandList());take(c->begin());take(c->transition(cb,S::ConstantBuffer));for(auto t:targets)take(c->transition(t,S::RenderTarget));take(c->beginRenderPass(pass));take(c->setPipeline(p));draw(*c,cb,0);take(c->endRenderPass());std::vector<Readback> reads;for(auto t:targets)reads.push_back(copy(d,*c,t));submit(d,c);
    for(unsigned i=0;i<8;++i)verify(reads[i],[=](unsigned,unsigned){return Pixel{float(i&1),float((i>>1)&1),float((i>>2)&1),1};});
    auto a=color(d),b=color(d,F::RGBA16Float);auto pd=description({{F::RGBA8UNorm,F::RGBA16Float},{}},{},two);pd.alphaBlend=true;p=take(d.createGraphicsPipeline(pd));c=take(d.createCommandList());take(c->begin());take(c->transition(a,S::RenderTarget));take(c->transition(b,S::RenderTarget));take(c->beginRenderPass({{{a,L::Clear,{0,0,1,1}},{b,L::Clear,{0,0,1,1}}},{}}));take(c->setPipeline(p));draw(*c,cb,0);take(c->endRenderPass());auto aRead=copy(d,*c,a),bRead=copy(d,*c,b);submit(d,c);verify(aRead,[](unsigned,unsigned){return Pixel{.5f,0,.5f,.5f};});verify(bRead,[](unsigned,unsigned){return Pixel{0,.5f,1,.5f};});std::cout<<"full_image eight_MRT_mixed_formats_and_alpha_blend passed\n";
}
void contracts(gfx::Device& d){
    auto a=color(d),b=color(d),z=depth(d),small=color(d,F::RGBA8UNorm,17);auto c=take(d.createCommandList());
    reject(c->beginRenderPass(gfx::RenderPassDesc{{{a}}, {}}),gfx::ErrorCode::InvalidState,"Pass before recording");take(c->begin());
    reject(c->transition(a,S::DepthWrite),gfx::ErrorCode::InvalidArgument,"Color cannot become depth");reject(c->transition(z,S::ShaderRead),gfx::ErrorCode::InvalidArgument,"D32 sampling unsupported");reject(c->transition(z,S::RenderTarget),gfx::ErrorCode::InvalidArgument,"Depth cannot become color");
    reject(c->beginRenderPass(gfx::RenderPassDesc{{{a}}, {z}}),gfx::ErrorCode::InvalidState,"Attachment states required");take(c->transition(a,S::RenderTarget));take(c->transition(b,S::RenderTarget));take(c->transition(z,S::DepthWrite));
    take(c->beginRenderPass({{{a,L::Clear,{1,0,0,1}},{b,L::Clear,{0,1,0,1}}},{z}}));reject(c->beginRenderPass(a,{}),gfx::ErrorCode::InvalidState,"No nested pass");reject(c->transition(z,S::DepthRead),gfx::ErrorCode::InvalidState,"No transitions inside pass");reject(c->end(),gfx::ErrorCode::InvalidState,"Cannot end command inside pass");take(c->endRenderPass());
    take(c->transition(b,S::CopySource));reject(c->beginRenderPass({{{a,L::Clear,{0,0,1,1}},{b,L::Clear,{0,0,1,1}}},{}}),gfx::ErrorCode::InvalidState,"Failed begin must not partially clear first target");take(c->transition(b,S::RenderTarget));
    reject(c->beginRenderPass({{{a},{a}}, {}}),gfx::ErrorCode::InvalidArgument,"Duplicate target");reject(c->beginRenderPass({{{a},{small}}, {}}),gfx::ErrorCode::InvalidArgument,"Mismatched size");reject(c->beginRenderPass({{}, {z,L::Clear,1,true}}),gfx::ErrorCode::InvalidArgument,"Cannot clear read-only depth");reject(c->beginRenderPass({{}, {z,L::Load,1,true}}),gfx::ErrorCode::InvalidState,"Read-only pass requires DepthRead");
    auto other=take(gfx::createDevice({gfx::Backend::D3D12,false,d.caps().softwareAdapter}));auto foreign=color(*other);reject(c->beginRenderPass(gfx::RenderPassDesc{{{foreign}}, {}}),gfx::ErrorCode::WrongDevice,"Foreign attachment");
    auto foreignDepth=depth(*other);reject(c->beginRenderPass({{}, {foreignDepth}}),gfx::ErrorCode::WrongDevice,"Foreign depth attachment");
    bool denied=false;std::thread worker([&]{auto r=c->beginRenderPass(gfx::RenderPassDesc{{{a}}, {}});denied=!r&&r.error().code==gfx::ErrorCode::InvalidState;});worker.join();check(denied,"Pass thread affinity");
    auto pd=description({{F::RGBA8UNorm}, {}});auto oneP=take(d.createGraphicsPipeline(pd));auto wrong=description({{F::RGBA16Float}, {}});auto wrongP=take(d.createGraphicsPipeline(wrong));auto cb=constants(d);write(cb,0,{1,1,1,1},.5f);take(c->transition(cb,S::ConstantBuffer));
    take(c->beginRenderPass({{{a,L::Load,{}}},{z,L::Load}}));take(c->setPipeline(oneP));reject(c->draw(3),gfx::ErrorCode::InvalidState,"Missing binding set");take(c->bindResources({{},{{0,0,cb,0,256}}}));reject(c->draw(3),gfx::ErrorCode::InvalidState,"Pipeline missing depth attachment");take(c->endRenderPass());
    take(c->beginRenderPass(gfx::RenderPassDesc{{{a,L::Load,{}}}, {}}));take(c->setPipeline(wrongP));take(c->bindResources({{},{{0,0,cb,0,256}}}));reject(c->draw(3),gfx::ErrorCode::InvalidState,"Pipeline color format mismatch");
    auto twoP=take(d.createGraphicsPipeline(description({{F::RGBA8UNorm,F::RGBA8UNorm},{}},{},two)));take(c->setPipeline(twoP));take(c->bindResources({{},{{0,0,cb,0,256}}}));reject(c->draw(3),gfx::ErrorCode::InvalidState,"Pipeline MRT count mismatch");
    take(c->endRenderPass());reject(c->endRenderPass(),gfx::ErrorCode::InvalidState,"No pass to end");auto ar=copy(d,*c,a),br=copy(d,*c,b);submit(d,c);verify(ar,[](unsigned,unsigned){return Pixel{1,0,0,1};});verify(br,[](unsigned,unsigned){return Pixel{0,1,0,1};});
    pd.renderTargets=gfx::RenderTargetLayout{{F::RGBA8UNorm,F::RGBA8UNorm},{}};reject(d.createGraphicsPipeline(pd),gfx::ErrorCode::InvalidArgument,"Shader must write every MRT");pd.renderTargets=gfx::RenderTargetLayout{{F::RGBA8UNorm},{}};pd.fragment=take(gfx::compileHlsl(two,"PS",gfx::ShaderStage::Fragment));reject(d.createGraphicsPipeline(pd),gfx::ErrorCode::InvalidArgument,"Shader output exceeds attachments");
    pd=description({{F::RGBA8UNorm},{}},{true,true});reject(d.createGraphicsPipeline(pd),gfx::ErrorCode::InvalidArgument,"Depth state needs depth format");
    pd=description({{},F::D32Float},{true,true},"float PS():SV_Depth{return .25;}");reject(d.createGraphicsPipeline(pd),gfx::ErrorCode::Unsupported,"Fragment depth export is explicitly unsupported");
    // Attachment-only commands take part in optimistic resource version validation.
    auto first=take(d.createCommandList()),second=take(d.createCommandList());take(first->begin());take(first->transition(z,S::DepthWrite));take(first->beginRenderPass({{}, {z}}));take(first->endRenderPass());take(first->end());
    take(second->begin());take(second->transition(z,S::DepthWrite));take(second->beginRenderPass({{}, {z}}));take(second->endRenderPass());take(second->end());take(d.queue().wait(take(d.queue().submit(*second))));reject(d.queue().submit(*first),gfx::ErrorCode::InvalidState,"Stale depth attachment recording");
    // D32 padded upload/copy/readback preserves every valid float byte.
    z=depth(d,17,3);auto f=z->footprint();auto upload=take(d.createBuffer({f.size,gfx::Memory::Upload}));std::vector<std::byte> data(static_cast<size_t>(f.size));for(unsigned y=0;y<3;++y)for(unsigned x=0;x<17;++x){const float value=float((x+y)%5)/4;std::memcpy(data.data()+y*f.rowPitch+x*4,&value,4);}take(upload->write(0,data));c=take(d.createCommandList());take(c->begin());take(c->transition(z,S::CopyDestination));take(c->copyBufferToTexture(upload,z));auto zr=copy(d,*c,z);submit(d,c);
    auto copied=take(zr.buffer->read(0,f.size));for(unsigned y=0;y<3;++y)check(std::memcmp(copied.data()+y*f.rowPitch,data.data()+y*f.rowPitch,f.rowBytes)==0,"D32 valid row bytes roundtrip exactly");verifyDepth(zr,[](unsigned x,unsigned y){return float((x+y)%5)/4;});
    std::cout<<"full_image pass_atomicity_and_depth_copy passed\n";
}
}
int main(int argc,char** argv)try{
    std::string_view mode=argc>1?argv[1]:"--cpu";if(mode=="--cpu")cpu();else{
        auto result=gfx::createDevice({gfx::Backend::D3D12,mode=="--validation",mode=="--warp"});if(!result&&result.error().code==gfx::ErrorCode::Unsupported){std::cout<<"SKIP: "<<result.error().message<<'\n';return 77;}
        auto d=take(std::move(result));check(d->caps().depthAttachment&&d->caps().multipleRenderTargets,"Depth/MRT capabilities");comparisons(*d);mrtAndLoad(*d,false);mrtAndLoad(*d,true);depthOnly(*d);eightAndBlend(*d);contracts(*d);take(d->queue().waitIdle());if(mode=="--validation")check(take(d->validationErrorCount())==0,"Native/GBV errors must be zero");
    }std::cout<<"PASS: "<<assertions<<" assertions, "<<images<<" full images\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
