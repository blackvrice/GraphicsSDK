#include <graphics/Graphics.hpp>
#include <iostream>
#include <stdexcept>

namespace {
template<class T>T take(gfx::Result<T> result){if(!result)throw std::runtime_error(result.error().message);return std::move(*result);}
void take(gfx::Result<void> result){if(!result)throw std::runtime_error(result.error().message);}
void verify(gfx::Device& device,const gfx::GraphicsPipelineDesc& desc){
    auto pipeline=take(device.createGraphicsPipeline(desc));auto texture=take(device.createTexture({32,32,gfx::Format::RGBA8UNorm,true}));auto f=texture->footprint();auto output=take(device.createBuffer({f.size,gfx::Memory::Readback}));
    auto commands=take(device.createCommandList());take(commands->begin());take(commands->transition(texture,gfx::ResourceState::RenderTarget));take(commands->beginRenderPass(texture,{1,0,0,1}));take(commands->setPipeline(pipeline));take(commands->draw(3));take(commands->endRenderPass());take(commands->transition(texture,gfx::ResourceState::CopySource));take(commands->copyTextureToBuffer(texture,output));take(commands->end());take(device.queue().wait(take(device.queue().submit(*commands))));auto pixels=take(output->read(0,f.size));
    for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x)for(unsigned channel=0;channel<4;++channel)
        if(pixels[y*f.rowPitch+x*4+channel]!=std::byte(channel==1||channel==3?255:0))throw std::runtime_error("Installed recovery consumer full image mismatch");
}
}
int main(int argc,char** argv)try{
    const bool warp=argc>1&&std::string_view(argv[1])=="--warp";
    auto device=take(gfx::createDevice({gfx::Backend::D3D12,false,warp,true}));take(device->checkStatus());
    gfx::GraphicsPipelineDesc desc;desc.vertex=take(gfx::compileHlsl("float4 VS(uint id:SV_VertexID):SV_Position{float2 p=float2((id<<1)&2,id&2);return float4(p*2-1,.5,1);}","VS",gfx::ShaderStage::Vertex));desc.fragment=take(gfx::compileHlsl("float4 PS():SV_Target{return float4(0,1,0,1);}","PS",gfx::ShaderStage::Fragment));
    verify(*device,desc);auto old=take(device->createBuffer({256,gfx::Memory::Upload}));take(device->removeForTesting());auto status=device->checkStatus();if(status||status.error().code!=gfx::ErrorCode::DeviceLost)return 1;
    auto recovered=take(device->recover());auto invalid=old->write(0,{});if(invalid||invalid.error().code!=gfx::ErrorCode::DeviceLost)return 1;take(recovered->checkStatus());verify(*recovered,desc);
    std::cout<<"Installed recovery consumer: 2048 pixels verified before/after native removal, warp="<<warp<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
