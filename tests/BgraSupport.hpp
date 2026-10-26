#pragma once
#include <graphics/Graphics.hpp>
#include "fixtures/SpirvFixtures.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
namespace bgraTest {
using F=gfx::Format;using S=gfx::ResourceState;
inline unsigned assertions{},images{},transfers{},paddedRows{};
inline std::uint64_t pixelBytes{},transferBytes{};
inline void check(bool ok,const char* text){++assertions;if(!ok)throw std::runtime_error(text);}
template<class T>T take(gfx::Result<T> r){if(!r)throw std::runtime_error(r.error().message+" native="+std::to_string(r.error().nativeCode));return std::move(*r);}
inline void take(gfx::Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
inline gfx::ShaderBinary spirv(std::span<const std::uint32_t> words,gfx::ShaderStage stage){auto b=std::as_bytes(words);return {stage,{b.begin(),b.end()},gfx::ShaderFormat::SPIRV};}
inline gfx::PipelinePtr pipeline(gfx::Device& d,F format,bool blend){
    gfx::GraphicsPipelineDesc p;p.colorFormat=format;p.alphaBlend=blend;
    if(d.caps().backend==gfx::Backend::Vulkan){p.vertex=spirv(fixtures::fullscreen,gfx::ShaderStage::Vertex);p.fragment=spirv(blend?std::span<const std::uint32_t>(fixtures::half_red):fixtures::red,gfx::ShaderStage::Fragment);}
    else{
        p.vertex=take(gfx::compileHlsl("float4 VS(uint id:SV_VertexID):SV_Position{float2 p=float2((id<<1)&2,id&2);return float4(p*2-1,0,1);}","VS",gfx::ShaderStage::Vertex));
        p.fragment=take(gfx::compileHlsl(blend?"float4 PS():SV_Target{return float4(1,0,0,0.5);}":"float4 PS():SV_Target{return float4(1,0,0,1);}","PS",gfx::ShaderStage::Fragment));
    }
    return take(d.createGraphicsPipeline(p));
}
inline void frame(gfx::Device& d,const gfx::TexturePtr& t,unsigned draw,gfx::SwapChain* swap=nullptr){
    const auto desc=t->description();const auto f=t->footprint();check((desc.format==F::BGRA8UNorm||desc.format==F::RGBA8UNorm)&&f.rowBytes==desc.width*4&&f.height==desc.height&&f.size>=std::uint64_t(f.rowPitch)*(f.height-1)+f.rowBytes&&f.size<=std::uint64_t(f.rowPitch)*f.height,"Truthful 8-bit format and footprint");
    auto read=take(d.createBuffer({f.size,gfx::Memory::Readback}));auto pad=take(d.createBuffer({f.size,gfx::Memory::Upload}));take(pad->write(0,std::vector<std::byte>(f.size,std::byte{0xb7})));
    auto c=take(d.createCommandList());take(c->begin());take(c->copyBuffer(pad,read,f.size));take(c->transition(t,S::RenderTarget));take(c->beginRenderPass(t,{.25f,.5f,.75f,1}));
    if(draw){auto p=pipeline(d,desc.format,draw==2);take(c->setPipeline(p));take(c->setScissor(1,1,desc.width-2,desc.height-2));take(c->draw(3));}
    take(c->endRenderPass());take(c->transition(t,S::CopySource));take(c->copyTextureToBuffer(t,read));if(swap)take(c->transition(t,S::Present));take(c->end());auto fence=take(d.queue().submit(*c));
    if(swap)take(swap->present());take(d.queue().wait(fence));auto bytes=take(read->read(0,f.size));bool pixels=true,padding=true;
    for(unsigned y=0;y<desc.height;++y){
        for(unsigned x=0;x<desc.width;++x){std::array<float,4> color{.25f,.5f,.75f,1};if(draw&&x>0&&x+1<desc.width&&y>0&&y+1<desc.height)color=draw==2?std::array<float,4>{.625f,.25f,.375f,.5f}:std::array<float,4>{1,0,0,1};
            for(unsigned k=0;k<4;++k){auto channel=desc.format==F::BGRA8UNorm&&k<3?2-k:k;pixels &= std::abs(std::to_integer<unsigned>(bytes[y*f.rowPitch+x*4+k])-color[channel]*255.f)<=1.1f;}
        }
        for(unsigned k=f.rowBytes;k<f.rowPitch&&std::uint64_t(y)*f.rowPitch+k<f.size;++k)padding &= bytes[y*f.rowPitch+k]==std::byte{0xb7};if(f.size>std::uint64_t(y)*f.rowPitch+f.rowBytes)++paddedRows;
    }
    check(pixels&&padding,"Full clear/draw/blend image and every padding byte");++images;pixelBytes+=std::uint64_t(f.rowBytes)*f.height;
}
inline void transfer(gfx::Device& d,F format){
    auto t=take(d.createTexture({65,7,format}));auto f=t->footprint();check(f.rowBytes==260,"No 16-bit fallback for BGRA");auto up=take(d.createBuffer({f.size,gfx::Memory::Upload}));auto read=take(d.createBuffer({f.size,gfx::Memory::Readback}));
    std::vector<std::byte> expected(f.size,std::byte{0x9d});for(unsigned y=0;y<f.height;++y)for(unsigned x=0;x<f.rowBytes;++x)expected[y*f.rowPitch+x]=std::byte((x*31+y*17)%251);take(up->write(0,expected));
    auto c=take(d.createCommandList());take(c->begin());take(c->copyBuffer(up,read,f.size));take(c->transition(t,S::CopyDestination));take(c->copyBufferToTexture(up,t));take(c->transition(t,S::CopySource));take(c->copyTextureToBuffer(t,read));take(c->end());take(d.queue().wait(take(d.queue().submit(*c))));check(take(read->read(0,f.size))==expected,"Upload/download preserves exact native byte order and padding");++transfers;transferBytes+=std::uint64_t(f.rowBytes)*f.height;
}
inline void offscreen(gfx::Device& d){for(auto f:{F::RGBA8UNorm,F::BGRA8UNorm}){transfer(d,f);auto t=take(d.createTexture({65,7,f,true}));for(unsigned draw=0;draw<3;++draw)frame(d,t,draw);}}
inline void empty(gfx::Device& d){take(d.queue().waitIdle());auto s=take(d.resourceStatistics());check(!s.trackedObjects&&!s.uniqueResources&&!s.pendingSubmissions&&!s.pendingObjectReferences,"No retained BGRA or presentation resources");check(take(d.validationErrorCount())==0,"No delivered validation errors");}
}
