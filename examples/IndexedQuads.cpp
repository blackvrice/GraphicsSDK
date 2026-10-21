#include <graphics/Graphics.hpp>
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
template<class T>T take(gfx::Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(*r);}
void take(gfx::Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
template<class T>gfx::BufferPtr upload(gfx::Device& d,const T& data){auto b=take(d.createBuffer({sizeof(data),gfx::Memory::Upload}));take(b->write(0,std::as_bytes(std::span{data})));return b;}
}
int main(int argc,char** argv)try{
    auto device=take(gfx::createDevice());
    constexpr auto source=R"(
struct V{float4 p:SV_Position;float4 color:COLOR;};
V VS(float2 p:POSITION,float4 transform:TEXCOORD0,float4 color:COLOR0){V v;v.p=float4(p*transform.zw+transform.xy,.5,1);v.color=color;return v;}
float4 PS(V v):SV_Target0{return v.color;}
)";
    const std::array<std::array<float,2>,4> positions{{{-1,1},{1,1},{-1,-1},{1,-1}}};
    const std::array<std::uint16_t,6> indices{0,1,2,2,1,3};
    const std::array<std::array<float,8>,2> instances{{{-.5f,0,.4f,.8f,1,.2f,.05f,1},{.5f,0,.4f,.8f,.05f,.8f,.2f,1}}};
    auto vertices=upload(*device,positions),index=upload(*device,indices),instance=upload(*device,instances);
    gfx::GraphicsPipelineDesc desc;desc.vertex=take(gfx::compileHlsl(source,"VS",gfx::ShaderStage::Vertex));desc.fragment=take(gfx::compileHlsl(source,"PS",gfx::ShaderStage::Fragment));
    desc.vertexLayout={{{0,8},{1,32,gfx::VertexInputRate::Instance}},{{"POSITION",0,gfx::VertexFormat::Float2,0,0},{"TEXCOORD",0,gfx::VertexFormat::Float4,1,0},{"COLOR",0,gfx::VertexFormat::Float4,1,16}}};
    auto pipeline=take(device->createGraphicsPipeline(desc));auto target=take(device->createTexture({256,256,gfx::Format::RGBA8UNorm,true}));auto footprint=target->footprint();auto readback=take(device->createBuffer({footprint.size,gfx::Memory::Readback}));auto cmd=take(device->createCommandList());
    take(cmd->begin());take(cmd->transition(vertices,gfx::ResourceState::VertexBuffer));take(cmd->transition(instance,gfx::ResourceState::VertexBuffer));take(cmd->transition(index,gfx::ResourceState::IndexBuffer));take(cmd->transition(target,gfx::ResourceState::RenderTarget));
    take(cmd->beginRenderPass(target,{.025f,.04f,.09f,1}));take(cmd->setPipeline(pipeline));take(cmd->bindVertexBuffer(0,vertices,8));take(cmd->bindVertexBuffer(1,instance,32));take(cmd->bindIndexBuffer(index,gfx::IndexFormat::UInt16));take(cmd->drawIndexed(6,2));take(cmd->endRenderPass());
    take(cmd->transition(target,gfx::ResourceState::CopySource));take(cmd->copyTextureToBuffer(target,readback));take(cmd->end());take(device->queue().wait(take(device->queue().submit(*cmd))));auto rgba=take(readback->read(0,footprint.size));
    std::string output=argc>1?argv[1]:"indexed-quads.ppm";std::ofstream file(output,std::ios::binary);if(!file)throw std::runtime_error("Cannot create output image");file<<"P6\n256 256\n255\n";
    for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x)file.write(reinterpret_cast<const char*>(rgba.data()+y*footprint.rowPitch+x*4),3);
    if(!file)throw std::runtime_error("Image write failed");std::cout<<device->caps().adapterName<<": indexed two instances -> "<<output<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
