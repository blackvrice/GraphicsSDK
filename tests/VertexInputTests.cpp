#include <graphics/Graphics.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
unsigned assertions=0;
void check(bool value,const char* message){++assertions;if(!value)throw std::runtime_error(message);}
template<class T>T take(gfx::Result<T> value){if(!value)throw std::runtime_error(value.error().message);return std::move(*value);}
void take(gfx::Result<void> value){if(!value)throw std::runtime_error(value.error().message);}
template<class T>void reject(gfx::Result<T> value,gfx::ErrorCode code,const char* message){check(!value&&value.error().code==code,message);}
template<class T>std::span<const std::byte> bytes(const T& data){return std::as_bytes(std::span{data});}
gfx::VertexLayout layout(){return {{{3,16,gfx::VertexInputRate::Vertex},{7,32,gfx::VertexInputRate::Instance}},
    {{"POSITION",0,gfx::VertexFormat::Float3,3,0},{"COLOR",0,gfx::VertexFormat::UNorm8x4,3,12},
     {"TEXCOORD",0,gfx::VertexFormat::Float4,7,0},{"TEXCOORD",1,gfx::VertexFormat::Float4,7,16}}};}
void cpu(){
    take(gfx::validateVertexLayout({}));check(true,"Empty vertex-ID layout");take(gfx::validateVertexLayout(layout()));
    for(unsigned kind=0;kind<14;++kind){auto bad=layout();switch(kind){
    case 0:bad.buffers[0].slot=32;break;
    case 1:bad.buffers.push_back(bad.buffers[0]);break;
    case 2:bad.buffers[0].stride=0;break;
    case 3:bad.buffers[0].stride=2049;break;
    case 4:bad.buffers[0].rate=static_cast<gfx::VertexInputRate>(99);break;
    case 5:bad.attributes[0].semantic="";break;
    case 6:bad.attributes[0].semantic=std::string("POS\0ITION",9);break;
    case 7:bad.attributes[0].semantic="SV_Position";break;
    case 8:bad.attributes.push_back({"position",0,gfx::VertexFormat::Float3,3,0});break;
    case 9:bad.attributes[0].slot=9;break;
    case 10:bad.attributes[0].offset=UINT32_MAX;break;
    case 11:bad.attributes[0].offset=1;break;
    case 12:bad.attributes[0].format=static_cast<gfx::VertexFormat>(99);break;
    case 13:bad.buffers.push_back({9,4});break;
    }reject(gfx::validateVertexLayout(bad),gfx::ErrorCode::InvalidArgument,"Malformed layout must fail portably");}
    auto valid=layout();valid.attributes[0].semantic="position";take(gfx::validateVertexLayout(valid));
    for(auto format:{gfx::VertexFormat::Float,gfx::VertexFormat::Float2,gfx::VertexFormat::Float3,gfx::VertexFormat::Float4,gfx::VertexFormat::UNorm8x4}){
        gfx::VertexLayout one{{{31,16}},{{"ATTR",123,format,31,0}}};take(gfx::validateVertexLayout(one));check(true,"Every public vertex format validates");}
    gfx::VertexLayout tooMany{{{0,4}},std::vector<gfx::VertexAttribute>(33,{"ATTR",0,gfx::VertexFormat::Float,0,0})};
    reject(gfx::validateVertexLayout(tooMany),gfx::ErrorCode::InvalidArgument,"Reject oversized input layout");
}
constexpr auto source=R"(
struct V {float4 p:SV_Position;float4 color:COLOR;};
V VS(float3 p:POSITION,float4 color:COLOR,float4 transform:TEXCOORD0,float4 tint:TEXCOORD1){
 V v;v.p=float4(p.xy*transform.zw+transform.xy,p.z,1);v.color=color*tint;return v;
}
V Split(float x:POSITION0,float y:POSITION1,float4 color:COLOR0,float2 offset:TEXCOORD0,float2 scale:TEXCOORD1,float4 tint:COLOR1){
 V v;v.p=float4(float2(x,y)*scale+offset,.5,1);v.color=color*tint;return v;
}
float4 PS(V v):SV_Target0{return v.color;}
)";
struct Vertex{float x,y,z;std::array<unsigned char,4> color{255,255,255,255};};
struct Instance{std::array<float,4> transform,tint;};
static_assert(sizeof(Vertex)==16 && sizeof(Instance)==32);
const std::array<Vertex,4> corners{{{-1,1,.5f},{1,1,.5f},{-1,-1,.5f},{1,-1,.5f}}};
const std::array<Instance,3> instances{{{{8,0,.5f,1},{0,0,1,1}},{{-.5f,0,.5f,1},{1,0,0,1}},{{.5f,0,.5f,1},{0,1,0,1}}}};
gfx::BufferPtr upload(gfx::Device& d,std::span<const std::byte> data){auto b=take(d.createBuffer({data.size(),gfx::Memory::Upload}));take(b->write(0,data));return b;}
gfx::BufferPtr buffer(gfx::Device& d,std::span<const std::byte> data,gfx::Memory memory){
    auto staging=upload(d,data);if(memory==gfx::Memory::Upload)return staging;
    auto b=take(d.createBuffer({data.size()}));auto c=take(d.createCommandList());take(c->begin());take(c->transition(b,gfx::ResourceState::CopyDestination));take(c->copyBuffer(staging,b,data.size()));take(c->end());take(d.queue().wait(take(d.queue().submit(*c))));return b;
}
gfx::PipelinePtr pipeline(gfx::Device& d,bool split=false,unsigned strideOverride=0){
    gfx::GraphicsPipelineDesc desc;desc.vertex=take(gfx::compileHlsl(source,split?"Split":"VS",gfx::ShaderStage::Vertex));desc.fragment=take(gfx::compileHlsl(source,"PS",gfx::ShaderStage::Fragment));desc.vertexLayout=layout();
    if(split)desc.vertexLayout={{{3,12},{7,32,gfx::VertexInputRate::Instance}},
        {{"POSITION",0,gfx::VertexFormat::Float,3,0},{"POSITION",1,gfx::VertexFormat::Float,3,4},{"COLOR",0,gfx::VertexFormat::UNorm8x4,3,8},
         {"TEXCOORD",0,gfx::VertexFormat::Float2,7,0},{"TEXCOORD",1,gfx::VertexFormat::Float2,7,8},{"COLOR",1,gfx::VertexFormat::Float4,7,16}}};
    if(strideOverride)desc.vertexLayout.buffers[0].stride=strideOverride;
    return take(d.createGraphicsPipeline(desc));
}
void verifyImage(gfx::Buffer& image,gfx::TextureFootprint footprint){
    auto data=take(image.read(0,footprint.size));
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){auto at=y*footprint.rowPitch+x*4;
        check(data[at]==std::byte(x<32?255:0)&&data[at+1]==std::byte(x<32?0:255)&&data[at+2]==std::byte{0}&&data[at+3]==std::byte{255},"Full indexed/instanced CPU image reference");}
}
void render(gfx::Device& d,gfx::Memory memory,bool indexed,gfx::IndexFormat format,int base,bool split=false,bool combined=false){
    std::vector<Vertex> vertices(2);vertices.insert(vertices.end(),corners.begin(),corners.end());
    if(!indexed){vertices.resize(2);for(auto i:{0,1,2,2,1,3})vertices.push_back(corners[i]);}
    std::vector<std::byte> vertexBytes;
    if(split){for(const auto& v:vertices){auto at=vertexBytes.size();vertexBytes.resize(at+12);std::memcpy(vertexBytes.data()+at,&v,8);std::memcpy(vertexBytes.data()+at+8,v.color.data(),4);}}
    else{auto raw=bytes(vertices);vertexBytes.assign(raw.begin(),raw.end());}
    auto vb=buffer(d,vertexBytes,memory),inst=buffer(d,bytes(instances),memory);gfx::BufferPtr ib;
    std::array<std::uint32_t,8> indices{777,777,0,1,2,2,1,3};if(base<0)for(unsigned i=2;i<8;++i)++indices[i];
    const auto element=format==gfx::IndexFormat::UInt16?2u:4u;std::vector<std::byte> indexBytes;
    if(format==gfx::IndexFormat::UInt16){std::array<std::uint16_t,8> small{};std::copy(indices.begin(),indices.end(),small.begin());auto raw=bytes(small);indexBytes.assign(raw.begin(),raw.end());}
    else{auto raw=bytes(indices);indexBytes.assign(raw.begin(),raw.end());}
    if(indexed)ib=buffer(d,indexBytes,memory);
    const auto stride=split?12u:16u;
    if(combined){std::vector<std::byte> data(512);std::memcpy(data.data(),vertexBytes.data(),vertexBytes.size());std::memcpy(data.data()+256,indexBytes.data(),indexBytes.size());vb=buffer(d,data,memory);ib=vb;}
    auto p=pipeline(d,split);auto target=take(d.createTexture({64,64,gfx::Format::RGBA8UNorm,true}));const auto footprint=target->footprint();auto image=take(d.createBuffer({footprint.size,gfx::Memory::Readback}));
    auto c=take(d.createCommandList());reject(c->bindVertexBuffer(3,vb,stride),gfx::ErrorCode::InvalidState,"Reject bind before begin");take(c->begin());
    take(c->transition(vb,combined?gfx::ResourceState::VertexAndIndexBuffer:gfx::ResourceState::VertexBuffer));take(c->transition(inst,gfx::ResourceState::VertexBuffer));
    if(indexed&&!combined)take(c->transition(ib,gfx::ResourceState::IndexBuffer));
    take(c->transition(target,gfx::ResourceState::RenderTarget));take(c->beginRenderPass(target,{0,0,1,1}));take(c->setPipeline(p));
    reject(c->draw(3),gfx::ErrorCode::InvalidState,"Missing vertex bindings");
    const std::uint64_t vertexOffset=base<0?stride*2:stride;const auto viewBytes=vertexBytes.size()-vertexOffset;
    take(c->bindVertexBuffer(3,vb,stride,vertexOffset,viewBytes));
    take(c->bindVertexBuffer(3,vb,stride,1,viewBytes));reject(c->draw(3),gfx::ErrorCode::InvalidArgument,"Misaligned float vertex input");
    take(c->bindVertexBuffer(3,vb,stride,vertexOffset,1));reject(c->draw(3),gfx::ErrorCode::InvalidArgument,"Vertex attribute extent exceeds view");
    take(c->bindVertexBuffer(3,vb,stride,vertexOffset,viewBytes));
    auto otherPipeline=pipeline(d,split,32);take(c->setPipeline(otherPipeline));reject(c->draw(3),gfx::ErrorCode::InvalidState,"Pipeline switch rechecks input layout");take(c->setPipeline(p));otherPipeline.reset();
    reject(c->draw(3),gfx::ErrorCode::InvalidState,"Missing instance input");take(c->bindVertexBuffer(7,inst,32));
    reject(c->bindVertexBuffer(32,vb,stride),gfx::ErrorCode::InvalidArgument,"Invalid vertex slot");
    reject(c->bindVertexBuffer(3,vb,0),gfx::ErrorCode::InvalidArgument,"Zero stride");
    reject(c->bindVertexBuffer(3,vb,2049),gfx::ErrorCode::InvalidArgument,"Oversized stride");
    reject(c->bindVertexBuffer(3,vb,stride,UINT64_MAX),gfx::ErrorCode::InvalidArgument,"Vertex offset overflow");
    reject(c->bindVertexBuffer(3,vb,stride,0,UINT64_MAX),gfx::ErrorCode::InvalidArgument,"Vertex size overflow");
    reject(c->bindVertexBuffer(3,image,stride),gfx::ErrorCode::InvalidArgument,"Readback cannot be vertex input");
    take(c->bindVertexBuffer(3,vb,stride+4,vertexOffset,viewBytes));reject(c->draw(3),gfx::ErrorCode::InvalidState,"Stride must match pipeline");
    take(c->bindVertexBuffer(3,vb,stride,vertexOffset,viewBytes));
    reject(c->draw(3,2,0,2),gfx::ErrorCode::InvalidArgument,"Instance view bounds");
    reject(c->draw(UINT32_MAX,2,2,1),gfx::ErrorCode::InvalidArgument,"Vertex draw range overflow");
    reject(c->draw(3,UINT32_MAX,0,2),gfx::ErrorCode::InvalidArgument,"Instance draw range overflow");
    if(indexed){
        reject(c->drawIndexed(6,2),gfx::ErrorCode::InvalidState,"Missing index input");
        const auto indexOffset=(combined?256u:0u)+element;take(c->bindIndexBuffer(ib,format,indexOffset,indexBytes.size()-element));
        reject(c->bindIndexBuffer(ib,static_cast<gfx::IndexFormat>(99)),gfx::ErrorCode::InvalidArgument,"Unknown index format");
        reject(c->bindIndexBuffer(ib,format,1),gfx::ErrorCode::InvalidArgument,"Misaligned index view");
        reject(c->bindIndexBuffer(ib,format,0,3),gfx::ErrorCode::InvalidArgument,"Partial index element");
        reject(c->bindIndexBuffer(ib,format,UINT64_MAX),gfx::ErrorCode::InvalidArgument,"Index offset overflow");
        reject(c->drawIndexed(8),gfx::ErrorCode::InvalidArgument,"Index window exceeds view");
        reject(c->drawIndexed(6,2,UINT32_MAX),gfx::ErrorCode::InvalidArgument,"Index start overflow");
        reject(c->drawIndexed(6,2,1,INT32_MAX,1),gfx::ErrorCode::InvalidArgument,"Impossible positive base vertex");
        take(c->drawIndexed(6,2,1,base,1));
    }else take(c->draw(6,2,1,1));
    take(c->endRenderPass());take(c->transition(target,gfx::ResourceState::CopySource));take(c->copyTextureToBuffer(target,image));take(c->end());
    auto fence=take(d.queue().submit(*c));c.reset();vb.reset();ib.reset();inst.reset();p.reset();target.reset();take(d.queue().wait(fence));verifyImage(*image,footprint);
    std::cout<<"full_image memory="<<int(memory)<<" indexed="<<indexed<<" index="<<int(format)<<" base="<<base<<" split="<<split<<" combined="<<combined<<" passed\n";
}
void stateContracts(gfx::Device& d){
    gfx::GraphicsPipelineDesc malformed;malformed.vertex=take(gfx::compileHlsl(source,"VS",gfx::ShaderStage::Vertex));malformed.fragment=take(gfx::compileHlsl(source,"PS",gfx::ShaderStage::Fragment));malformed.vertexLayout=layout();malformed.vertexLayout.buffers[0].stride=0;
    reject(d.createGraphicsPipeline(malformed),gfx::ErrorCode::InvalidArgument,"Pipeline creation validates layout before driver calls");
    auto vb=upload(d,bytes(corners));auto ib=upload(d,bytes(std::array<std::uint16_t,6>{0,1,2,2,1,3}));auto inst=upload(d,bytes(instances));
    auto p=pipeline(d);auto target=take(d.createTexture({64,64,gfx::Format::RGBA8UNorm,true}));
    auto c=take(d.createCommandList());take(c->begin());
    reject(c->bindVertexBuffer(3,vb,16),gfx::ErrorCode::InvalidState,"Vertex state required");reject(c->bindIndexBuffer(ib,gfx::IndexFormat::UInt16),gfx::ErrorCode::InvalidState,"Index state required");
    reject(c->transition(target,gfx::ResourceState::VertexBuffer),gfx::ErrorCode::InvalidArgument,"Texture cannot become vertex buffer");
    take(c->transition(vb,gfx::ResourceState::VertexBuffer));take(c->transition(ib,gfx::ResourceState::IndexBuffer));take(c->transition(inst,gfx::ResourceState::VertexBuffer));
    take(c->bindVertexBuffer(3,vb,16));take(c->bindVertexBuffer(7,inst,32));take(c->bindIndexBuffer(ib,gfx::IndexFormat::UInt16));
    take(c->transition(vb,gfx::ResourceState::CopySource));take(c->transition(target,gfx::ResourceState::RenderTarget));take(c->beginRenderPass(target,{0,0,1,1}));take(c->setPipeline(p));
    reject(c->drawIndexed(6,2,0,0,1),gfx::ErrorCode::InvalidState,"Recheck bound vertex state at draw");take(c->endRenderPass());
    take(c->transition(vb,gfx::ResourceState::VertexBuffer));take(c->transition(ib,gfx::ResourceState::CopySource));take(c->beginRenderPass(target,{0,0,1,1}));
    reject(c->drawIndexed(6,2,0,0,1),gfx::ErrorCode::InvalidState,"Recheck bound index state at draw");take(c->endRenderPass());
    take(c->transition(ib,gfx::ResourceState::IndexBuffer));take(c->beginRenderPass(target,{0,0,1,1}));take(c->drawIndexed(6,2,0,0,1));take(c->endRenderPass());take(c->end());
    // Both input resources participate in submission version checks, even without a copy command.
    take(vb->write(0,bytes(corners)));reject(d.queue().submit(*c),gfx::ErrorCode::InvalidState,"Vertex write after recording invalidates submission");
    take(c->begin());take(c->transition(ib,gfx::ResourceState::IndexBuffer));take(c->bindIndexBuffer(ib,gfx::IndexFormat::UInt16));take(c->end());
    const std::array<std::uint16_t,6> data{0,1,2,2,1,3};take(ib->write(0,bytes(data)));reject(d.queue().submit(*c),gfx::ErrorCode::InvalidState,"Index write after recording invalidates submission");
    take(c->begin());take(c->transition(vb,gfx::ResourceState::VertexBuffer));take(c->transition(inst,gfx::ResourceState::VertexBuffer));take(c->transition(target,gfx::ResourceState::RenderTarget));take(c->beginRenderPass(target,{0,0,1,1}));take(c->setPipeline(p));
    reject(c->draw(3),gfx::ErrorCode::InvalidState,"Begin clears vertex bindings");take(c->bindVertexBuffer(3,vb,16));take(c->bindVertexBuffer(7,inst,32));reject(c->drawIndexed(6),gfx::ErrorCode::InvalidState,"Begin clears index binding");take(c->endRenderPass());take(c->end());
    auto other=take(gfx::createDevice({gfx::Backend::D3D12,false,d.caps().softwareAdapter}));auto foreign=take(other->createBuffer({64,gfx::Memory::Upload}));
    take(c->begin());reject(c->bindVertexBuffer(3,foreign,16),gfx::ErrorCode::WrongDevice,"Reject foreign vertex resource");reject(c->bindIndexBuffer(foreign,gfx::IndexFormat::UInt16),gfx::ErrorCode::WrongDevice,"Reject foreign index resource");
    bool rejected=false;std::thread worker([&]{auto r=c->bindVertexBuffer(3,vb,16);rejected=!r&&r.error().code==gfx::ErrorCode::InvalidState;});worker.join();check(rejected,"Reject input binding on foreign thread");take(c->end());
}
}
int main(int argc,char** argv)try{
    const std::string_view mode=argc>1?argv[1]:"--cpu";
    if(mode=="--cpu")cpu();
    else{
        auto result=gfx::createDevice({gfx::Backend::D3D12,mode=="--validation",mode=="--warp"});
        if(!result&&result.error().code==gfx::ErrorCode::Unsupported){std::cout<<"SKIP: "<<result.error().message<<'\n';return 77;}
        auto d=take(std::move(result));check(d->caps().vertexInput&&d->caps().indexedDrawing,"Capabilities advertise implemented input support");
        for(auto memory:{gfx::Memory::Upload,gfx::Memory::Device})render(*d,memory,false,gfx::IndexFormat::UInt16,0);
        render(*d,gfx::Memory::Upload,true,gfx::IndexFormat::UInt16,1);render(*d,gfx::Memory::Device,true,gfx::IndexFormat::UInt32,1);
        render(*d,gfx::Memory::Upload,true,gfx::IndexFormat::UInt32,-1);render(*d,gfx::Memory::Device,true,gfx::IndexFormat::UInt16,-1);
        render(*d,gfx::Memory::Device,true,gfx::IndexFormat::UInt16,1,false,true);
        render(*d,gfx::Memory::Upload,false,gfx::IndexFormat::UInt16,0,true);stateContracts(*d);take(d->queue().waitIdle());
        if(mode=="--validation")check(take(d->validationErrorCount())==0,"Native/GBV must report no errors");
    }
    std::cout<<"PASS: "<<assertions<<" assertions\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
