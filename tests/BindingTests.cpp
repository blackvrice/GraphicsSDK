#include <graphics/Graphics.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
unsigned assertions=0;
void check(bool value,const char* message){++assertions;if(!value)throw std::runtime_error(message);}
template<class T>T take(gfx::Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(*r);}
void take(gfx::Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
template<class T>void reject(gfx::Result<T> r,gfx::ErrorCode code,const char* message){check(!r&&r.error().code==code,message);}
template<class T>auto bytes(const T& v){return std::as_bytes(std::span{&v,1});}
using Type=gfx::BindingType;using Vis=gfx::ShaderVisibility;
gfx::BindingLayout layout(){return {
    {{Type::Texture2D,3,2,1,Vis::Fragment},{Type::Texture2D,3,1,1,Vis::Vertex},
     {Type::Texture2D,3,1,2,Vis::Fragment},{Type::ConstantBuffer,3,1,1,Vis::Vertex},{Type::ConstantBuffer,3,1,1,Vis::Fragment}},
    {{2,1,Vis::Fragment,gfx::SamplerFilter::Point},{3,1,Vis::Fragment,gfx::SamplerFilter::Linear,gfx::SamplerAddress::Repeat}}};}
void cpu(){
    take(gfx::validateBindingLayout({}));take(gfx::validateBindingLayout(layout()));
    for(int i=0;i<15;++i){auto l=layout();switch(i){
    case 0:l.entries[0].count=0;break;
    case 1:l.entries[0].count=UINT32_MAX;break;
    case 2:l.entries[0].slot=UINT32_MAX;break;
    case 3:l.entries[0].space=0xfffffff0u;break;
    case 4:l.entries[0].type=static_cast<Type>(99);break;
    case 5:l.entries[0].visibility=static_cast<Vis>(99);break;
    case 6:l.entries[1].visibility=Vis::All;break;
    case 7:l.entries[3].count=15;break;
    case 8:l.entries[3].visibility=Vis::All;break;
    case 9:l.samplers[1].slot=2;break;
    case 10:l.samplers[0].slot=16;break;
    case 11:l.samplers[0].space=UINT32_MAX;break;
    case 12:l.samplers[0].visibility=static_cast<Vis>(99);break;
    case 13:l.samplers[0].filter=static_cast<gfx::SamplerFilter>(99);break;
    case 14:l.samplers[0].address=static_cast<gfx::SamplerAddress>(99);break;
    }reject(gfx::validateBindingLayout(l),gfx::ErrorCode::InvalidArgument,"Malformed binding layout");}
    auto l=layout();l.entries.resize(33);reject(gfx::validateBindingLayout(l),gfx::ErrorCode::InvalidArgument,"Root entry limit");
    l=layout();l.samplers.resize(17);reject(gfx::validateBindingLayout(l),gfx::ErrorCode::InvalidArgument,"Static sampler limit");
    l={{{Type::Texture2D,0,128,0,Vis::Vertex},{Type::Texture2D,0,128,0,Vis::Fragment}}, {}};take(gfx::validateBindingLayout(l));
    l.entries.push_back({Type::ConstantBuffer,0,1,0,Vis::All});reject(gfx::validateBindingLayout(l),gfx::ErrorCode::InvalidArgument,"Total descriptor limit");
    l={{{Type::Texture2D,0,128,0,Vis::Vertex},{Type::Texture2D,0,1,1,Vis::Vertex}}, {}};reject(gfx::validateBindingLayout(l),gfx::ErrorCode::InvalidArgument,"Per-stage resource limit across spaces");
    l={{{Type::ConstantBuffer,0,14,0,Vis::All}},{{15,0,Vis::Vertex},{15,0,Vis::Fragment}}};take(gfx::validateBindingLayout(l));
}
constexpr auto vertex=R"(
Texture2D<float4> vertexImage:register(t3,space1);
cbuffer Transform:register(b3,space1){float4 transform;}
float4 VS(uint id:SV_VertexID):SV_Position {
 float2 p=float2((id<<1)&2,id&2);return float4((p*2-1)*transform.xy*vertexImage.Load(int3(0,0,0)).xy+transform.zw,.5,1);
})";
constexpr auto fragment=R"(
Texture2D<float4> images[2]:register(t3,space1);
Texture2D<float4> other:register(t3,space2);
SamplerState pointSample:register(s2,space1);SamplerState linearSample:register(s3,space1);
cbuffer Tint:register(b3,space1){float4 tint;}
float4 PS():SV_Target0 {
 float4 a=images[0].SampleLevel(pointSample,float2(.5,.5),0);
 float4 b=images[1].SampleLevel(linearSample,float2(.5,.5),0);
 return float4(a.r,b.g,other.Load(int3(0,0,0)).b,1)*tint;
})";
constexpr auto plainVertex=R"(float4 VS(uint id:SV_VertexID):SV_Position{float2 p=float2((id<<1)&2,id&2);return float4(p*2-1,.5,1);})";
gfx::GraphicsPipelineDesc desc(){gfx::GraphicsPipelineDesc p;p.vertex=take(gfx::compileHlsl(vertex,"VS",gfx::ShaderStage::Vertex));p.fragment=take(gfx::compileHlsl(fragment,"PS",gfx::ShaderStage::Fragment));p.bindingLayout=layout();return p;}
gfx::BufferPtr constants(gfx::Device& d,gfx::Memory memory){
    auto staging=take(d.createBuffer({1024,gfx::Memory::Upload}));
    const std::array<float,4> transform{1,1,0,0},red{1,0,0,1},white{1,1,1,1};
    take(staging->write(0,bytes(transform)));take(staging->write(256,bytes(red)));take(staging->write(512,bytes(white)));
    if(memory==gfx::Memory::Upload)return staging;
    auto b=take(d.createBuffer({1024}));auto c=take(d.createCommandList());take(c->begin());take(c->transition(b,gfx::ResourceState::CopyDestination));take(c->copyBuffer(staging,b,1024));take(c->end());take(d.queue().wait(take(d.queue().submit(*c))));return b;
}
gfx::TexturePtr texture(gfx::Device& d,std::array<unsigned char,4> color,gfx::Format format=gfx::Format::RGBA8UNorm){
    auto t=take(d.createTexture({2,1,format}));auto f=t->footprint();auto upload=take(d.createBuffer({f.size,gfx::Memory::Upload}));
    std::vector<std::byte> data(static_cast<size_t>(f.size));
    for(unsigned x=0;x<2;++x)if(format==gfx::Format::RGBA8UNorm)std::memcpy(data.data()+x*4,color.data(),4);
    else for(unsigned channel=0;channel<4;++channel){const std::uint16_t half=color[channel]?0x3c00:0;std::memcpy(data.data()+x*8+channel*2,&half,2);}
    take(upload->write(0,data));
    auto c=take(d.createCommandList());take(c->begin());take(c->transition(t,gfx::ResourceState::CopyDestination));take(c->copyBufferToTexture(upload,t));take(c->transition(t,gfx::ResourceState::ShaderRead));take(c->end());take(d.queue().wait(take(d.queue().submit(*c))));return t;
}
void verify(gfx::Buffer& image,gfx::TextureFootprint f,std::array<unsigned char,4> left,std::array<unsigned char,4> right,unsigned tolerance=0){
    auto data=take(image.read(0,f.size));for(unsigned y=0;y<f.height;++y)for(unsigned x=0;x<f.rowBytes/4;++x)for(unsigned channel=0;channel<4;++channel){
        auto expected=(x<f.rowBytes/8?left:right)[channel];auto actual=std::to_integer<unsigned char>(data[y*f.rowPitch+x*4+channel]);
        check(static_cast<unsigned>(std::abs(int(actual)-int(expected)))<=tolerance,"Full CPU image reference");}
}
void pipelineContracts(gfx::Device& d){
    const auto p=desc();auto r=take(gfx::reflectShader(p.fragment));check(r.size()==5,"Reflection resource count");
    for(const auto& e:r){check(e.type!=gfx::ShaderResourceType::Unsupported,"Reflection has typed resources");if(e.type==gfx::ShaderResourceType::ConstantBuffer)check(e.minimumBytes==16,"Reflected cbuffer size");}
    auto stage=p.vertex;stage.stage=gfx::ShaderStage::Fragment;reject(gfx::reflectShader(stage),gfx::ErrorCode::InvalidArgument,"Reject bytecode stage spoof");
    for(int i=0;i<9;++i){auto bad=p;switch(i){
    case 0:bad.bindingLayout.entries.clear();break;
    case 1:bad.bindingLayout.entries[0].count=1;break;
    case 2:bad.bindingLayout.entries[0].space=4;break;
    case 3:bad.bindingLayout.entries[0].visibility=Vis::Vertex;break;
    case 4:bad.bindingLayout.entries[0].type=Type::ConstantBuffer;break;
    case 5:bad.bindingLayout.samplers.pop_back();break;
    case 6:bad.sampledTexture=true;break;
    case 7:bad.bindingLayout.samplers[0].visibility=Vis::Vertex;break;
    case 8:bad.fragment=p.vertex;bad.fragment.stage=gfx::ShaderStage::Fragment;break;
    }reject(d.createGraphicsPipeline(bad),gfx::ErrorCode::InvalidArgument,"Shader and layout mismatch must fail before driver validation");}
    for(auto declaration:{"Texture3D<float4> t:register(t0);float4 PS():SV_Target{return t.Load(int4(0,0,0,0));}",
                          "Texture2D<uint4> t:register(t0);float4 PS():SV_Target{return t.Load(int3(0,0,0));}",
                          "StructuredBuffer<float4> t:register(t0);float4 PS():SV_Target{return t[0];}"}){
        auto bad=p;bad.vertex=take(gfx::compileHlsl(plainVertex,"VS",gfx::ShaderStage::Vertex));bad.fragment=take(gfx::compileHlsl(declaration,"PS",gfx::ShaderStage::Fragment));
        reject(d.createGraphicsPipeline(bad),gfx::ErrorCode::Unsupported,"Unsupported shader resource kind");}
    for(auto declaration:{
        "Texture2D<float4> t:register(t0);SamplerState s[2]:register(s0);float4 PS():SV_Target{return t.SampleLevel(s[0],float2(0,0),0)+t.SampleLevel(s[1],float2(0,0),0);}",
        "Texture2D<float4> t:register(t0);SamplerComparisonState s:register(s0);float4 PS():SV_Target{return t.SampleCmpLevelZero(s,float2(0,0),.5);}"}){
        auto bad=p;bad.vertex=take(gfx::compileHlsl(plainVertex,"VS",gfx::ShaderStage::Vertex));bad.fragment=take(gfx::compileHlsl(declaration,"PS",gfx::ShaderStage::Fragment));
        bad.bindingLayout={{{Type::Texture2D,0}},{{0},{1}}};
        reject(d.createGraphicsPipeline(bad),gfx::ErrorCode::Unsupported,"Sampler arrays/comparison samplers are rejected before native PSO creation");
    }
}
void render(gfx::Device& d,gfx::Memory memory,gfx::Format format){
    auto p=take(d.createGraphicsPipeline(desc()));auto red=texture(d,{255,0,0,255},format),green=texture(d,{0,255,0,255},format),blue=texture(d,{0,0,255,255},format),white=texture(d,{255,255,255,255},format);auto cb=constants(d,memory);
    auto t=take(d.createTexture({64,32,gfx::Format::RGBA8UNorm,true}));auto f=t->footprint();auto image=take(d.createBuffer({f.size,gfx::Memory::Readback}));
    gfx::ResourceBindings set{{{0,0,red},{0,1,green},{1,0,white},{2,0,blue}},{{3,0,cb,0,256},{4,0,cb,256,256}}};
    auto c=take(d.createCommandList());reject(c->bindResources(set),gfx::ErrorCode::InvalidState,"Bind before begin");take(c->begin());reject(c->bindResources(set),gfx::ErrorCode::InvalidState,"Bind before pipeline");take(c->setPipeline(p));
    reject(c->bindResources(set),gfx::ErrorCode::InvalidState,"Constant buffer state required");take(c->transition(cb,gfx::ResourceState::ConstantBuffer));
    reject(c->transition(t,gfx::ResourceState::ConstantBuffer),gfx::ErrorCode::InvalidArgument,"Texture cannot use constant state");
    take(c->transition(t,gfx::ResourceState::RenderTarget));take(c->beginRenderPass(t,{0,0,1,1}));reject(c->draw(3),gfx::ErrorCode::InvalidState,"Missing full binding set");
    take(c->bindResources(set));take(c->setScissor(0,0,32,32));take(c->draw(3));
    for(int i=0;i<12;++i){auto bad=set;switch(i){
    case 0:bad.textures.pop_back();break;
    case 1:bad.textures[1].element=0;break;
    case 2:bad.textures[0].binding=99;break;
    case 3:bad.textures[0].binding=3;break;
    case 4:bad.textures[0].element=2;break;
    case 5:bad.constants[0].offset=1;break;
    case 6:bad.constants[0].size=0;break;
    case 7:bad.constants[0].size=65792;break;
    case 8:bad.constants[0].size=128;break;
    case 9:bad.constants[0].offset=UINT64_MAX-255;break;
    case 10:bad.constants[0].size=1280;break;
    case 11:bad.constants[0].buffer=image;break;
    }reject(c->bindResources(bad),gfx::ErrorCode::InvalidArgument,"Malformed binding snapshot");}
    auto bad=set;bad.textures[0].texture.reset();reject(c->bindResources(bad),gfx::ErrorCode::WrongDevice,"Null texture");
    bad=set;bad.constants[0].buffer.reset();reject(c->bindResources(bad),gfx::ErrorCode::WrongDevice,"Null constant buffer");
    reject(c->bindTexture(red),gfx::ErrorCode::InvalidState,"Legacy helper is explicit opt-in");
    take(c->draw(3)); // Failed sets must preserve the first snapshot.
    take(c->endRenderPass());take(c->transition(cb,gfx::ResourceState::CopySource));take(c->beginRenderPass(t,{0,0,1,1}));
    reject(c->draw(3),gfx::ErrorCode::InvalidState,"Draw rechecks bound constant buffer state");take(c->endRenderPass());
    take(c->transition(cb,gfx::ResourceState::ConstantBuffer));take(c->transition(red,gfx::ResourceState::CopySource));take(c->beginRenderPass(t,{0,0,1,1}));
    reject(c->draw(3),gfx::ErrorCode::InvalidState,"Draw rechecks bound texture state");take(c->endRenderPass());take(c->transition(red,gfx::ResourceState::ShaderRead));
    take(c->beginRenderPass(t,{0,0,1,1}));take(c->setScissor(0,0,32,32));take(c->draw(3)); // Last clear means redraw left.
    set.textures[0].texture=blue;set.textures[3].texture=red;set.constants[1].offset=512;
    std::reverse(set.textures.begin(),set.textures.end());std::reverse(set.constants.begin(),set.constants.end());
    take(c->bindResources(set));take(c->setScissor(32,0,32,32));take(c->draw(3));
    // Six descriptors per snapshot; 42 sets fit, four slots remain. Rejection is atomic.
    for(unsigned i=2;i<42;++i)take(c->bindResources(set));reject(c->bindResources(set),gfx::ErrorCode::InvalidState,"Descriptor capacity is bounded");take(c->draw(3));
    take(c->setPipeline(p));reject(c->draw(3),gfx::ErrorCode::InvalidState,"Pipeline switch clears binding set");take(c->endRenderPass());
    take(c->transition(t,gfx::ResourceState::CopySource));take(c->copyTextureToBuffer(t,image));take(c->end());
    auto fence=take(d.queue().submit(*c));c.reset();p.reset();set={};t.reset();cb.reset();red.reset();green.reset();blue.reset();white.reset();take(d.queue().wait(fence));
    verify(*image,f,{255,0,0,255},{0,255,0,255});std::cout<<"full_image binding_snapshot memory="<<int(memory)<<" texture_format="<<int(format)<<" passed\n";
}
void samplerAndArray(gfx::Device& d){
    constexpr auto shader=R"(
    struct Params{float4 value;};ConstantBuffer<Params> params[2]:register(b1,space2);
    Texture2D<float4> image:register(t7,space2);SamplerState samp:register(s5,space2);
    float4 PS():SV_Target{return image.SampleLevel(samp,float2(params[0].value.x,.5),0)*params[1].value;})";
    auto cb=constants(d,gfx::Memory::Upload);const std::array<float,4> uv{1.25f,0,0,0};take(cb->write(0,bytes(uv)));
    auto t=take(d.createTexture({2,1}));auto f=t->footprint();auto up=take(d.createBuffer({f.size,gfx::Memory::Upload}));std::vector<std::byte> data(static_cast<size_t>(f.size));
    const std::array<unsigned char,8> colors{255,0,0,255,0,255,0,255};std::memcpy(data.data(),colors.data(),8);take(up->write(0,data));
    auto target=take(d.createTexture({16,16,gfx::Format::RGBA8UNorm,true}));auto out=target->footprint();auto read=take(d.createBuffer({out.size,gfx::Memory::Readback}));
    gfx::GraphicsPipelineDesc pd;pd.vertex=take(gfx::compileHlsl(plainVertex,"VS",gfx::ShaderStage::Vertex));pd.fragment=take(gfx::compileHlsl(shader,"PS",gfx::ShaderStage::Fragment));
    pd.bindingLayout={{{Type::ConstantBuffer,1,2,2,Vis::All},{Type::Texture2D,7,1,2,Vis::All}},{{5,2,Vis::All,gfx::SamplerFilter::Point}}};
    gfx::ResourceBindings set{{{1,0,t}},{{0,0,cb,0,256},{0,1,cb,512,256}}};
    for(int mode=0;mode<4;++mode){
        pd.bindingLayout.samplers[0].address=(mode==1||mode==3)?gfx::SamplerAddress::Repeat:gfx::SamplerAddress::Clamp;
        pd.bindingLayout.samplers[0].filter=mode>=2?gfx::SamplerFilter::Linear:gfx::SamplerFilter::Point;
        auto p=take(d.createGraphicsPipeline(pd));auto c=take(d.createCommandList());take(c->begin());
        if(mode==0){take(c->transition(t,gfx::ResourceState::CopyDestination));take(c->copyBufferToTexture(up,t));}
        if(mode>=2){const std::array<float,4> position{mode==2?.5f:1.f,0,0,0};take(cb->write(0,bytes(position)));}
        take(c->transition(t,gfx::ResourceState::ShaderRead));take(c->transition(cb,gfx::ResourceState::ConstantBuffer));take(c->transition(target,gfx::ResourceState::RenderTarget));
        take(c->beginRenderPass(target,{0,0,1,1}));take(c->setPipeline(p));take(c->bindResources(set));take(c->draw(3));take(c->endRenderPass());take(c->transition(target,gfx::ResourceState::CopySource));take(c->copyTextureToBuffer(target,read));take(c->end());take(d.queue().wait(take(d.queue().submit(*c))));
        const std::array<unsigned char,4> expected=mode==0?std::array<unsigned char,4>{0,255,0,255}:mode==1?std::array<unsigned char,4>{255,0,0,255}:std::array<unsigned char,4>{128,128,0,255};
        verify(*read,out,expected,expected,mode>=2?1:0);std::cout<<"full_image static_sampler mode="<<mode<<" constant_array passed\n";
    }
}
void lifetimeContracts(gfx::Device& d){
    auto pd=desc();auto p=take(d.createGraphicsPipeline(pd));auto t=texture(d,{255,255,255,255});auto cb=constants(d,gfx::Memory::Upload);
    gfx::ResourceBindings set{{{0,0,t},{0,1,t},{1,0,t},{2,0,t}},{{3,0,cb,0,256},{4,0,cb,512,256}}};
    auto other=take(gfx::createDevice({gfx::Backend::D3D12,false,d.caps().softwareAdapter}));auto foreignTexture=take(other->createTexture({2,1}));auto foreignBuffer=take(other->createBuffer({1024,gfx::Memory::Upload}));
    auto c=take(d.createCommandList());take(c->begin());take(c->setPipeline(p));take(c->transition(cb,gfx::ResourceState::ConstantBuffer));
    auto bad=set;bad.textures[0].texture=foreignTexture;reject(c->bindResources(bad),gfx::ErrorCode::WrongDevice,"Foreign texture");bad=set;bad.constants[0].buffer=foreignBuffer;reject(c->bindResources(bad),gfx::ErrorCode::WrongDevice,"Foreign buffer");
    bool rejected=false;std::thread thread([&]{auto r=c->bindResources(set);rejected=!r&&r.error().code==gfx::ErrorCode::InvalidState;});thread.join();check(rejected,"Binding thread affinity");
    take(c->bindResources(set));take(c->end());const std::array<float,4> value{1,1,0,0};take(cb->write(0,bytes(value)));reject(d.queue().submit(*c),gfx::ErrorCode::InvalidState,"Constant buffer write invalidates recorded version");
    take(c->begin());reject(c->bindResources(set),gfx::ErrorCode::InvalidState,"Begin clears pipeline and bindings");take(c->end());
    // Reflected block size is enforced in addition to CBV alignment.
    pd.vertex=take(gfx::compileHlsl(plainVertex,"VS",gfx::ShaderStage::Vertex));pd.fragment=take(gfx::compileHlsl("cbuffer Big:register(b0){float4 values[17];}float4 PS():SV_Target{return values[16];}","PS",gfx::ShaderStage::Fragment));
    pd.bindingLayout={{{Type::ConstantBuffer,0}}, {}};p=take(d.createGraphicsPipeline(pd));take(c->begin());take(c->setPipeline(p));take(c->transition(cb,gfx::ResourceState::ConstantBuffer));
    reject(c->bindResources({{},{{0,0,cb,0,256}}}),gfx::ErrorCode::InvalidArgument,"CBV must cover reflected shader size");take(c->bindResources({{},{{0,0,cb,0,512}}}));take(c->end());take(d.queue().wait(take(d.queue().submit(*c))));
    auto large=take(d.createBuffer({65536,gfx::Memory::Upload}));take(c->begin());take(c->setPipeline(p));take(c->transition(large,gfx::ResourceState::ConstantBuffer));take(c->bindResources({{},{{0,0,large,0,65536}}}));take(c->end());take(d.queue().wait(take(d.queue().submit(*c))));
}
}
int main(int argc,char** argv)try{
    const std::string_view mode=argc>1?argv[1]:"--cpu";
    if(mode=="--cpu")cpu();else{
        auto result=gfx::createDevice({gfx::Backend::D3D12,mode=="--validation",mode=="--warp"});
        if(!result&&result.error().code==gfx::ErrorCode::Unsupported){std::cout<<"SKIP: "<<result.error().message<<'\n';return 77;}
        auto d=take(std::move(result));check(d->caps().bindingLayouts,"Binding layouts capability");pipelineContracts(*d);
        for(auto memory:{gfx::Memory::Upload,gfx::Memory::Device})for(auto format:{gfx::Format::RGBA8UNorm,gfx::Format::RGBA16Float})render(*d,memory,format);
        samplerAndArray(*d);lifetimeContracts(*d);take(d->queue().waitIdle());
        if(mode=="--validation")check(take(d->validationErrorCount())==0,"Native/GBV error count must be zero");
    }
    std::cout<<"PASS: "<<assertions<<" assertions\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
