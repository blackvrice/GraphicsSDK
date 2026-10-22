#include <graphics/Graphics.hpp>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <array>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
unsigned assertions=0,images=0;
void check(bool value,const char* message){++assertions;if(!value)throw std::runtime_error(message);}
template<class T>T take(gfx::Result<T> r){if(!r)throw std::runtime_error(r.error().message+" native="+std::to_string(r.error().nativeCode));return std::move(*r);}
void take(gfx::Result<void> r){if(!r)throw std::runtime_error(r.error().message+" native="+std::to_string(r.error().nativeCode));}
template<class T>void reject(gfx::Result<T> r,gfx::ErrorCode code,const char* message){check(!r&&r.error().code==code,message);}
template<class T>void lost(gfx::Result<T> r,std::int64_t reason){check(!r&&r.error().code==gfx::ErrorCode::DeviceLost&&r.error().nativeCode==reason,"Lost generation must consistently preserve its native reason");}
using S=gfx::ResourceState;using F=gfx::Format;
struct Window {
    HWND handle=CreateWindowExW(0,L"STATIC",L"GraphicsSDK recovery test",WS_OVERLAPPEDWINDOW,0,0,128,128,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Window(){check(handle!=nullptr,"Create hidden recovery window");}~Window(){if(handle)DestroyWindow(handle);}
};
gfx::GraphicsPipelineDesc description(){
    constexpr auto shader=R"(
    cbuffer Params:register(b0){float4 color;}
    float4 VS(uint id:SV_VertexID):SV_Position{float2 p=float2((id<<1)&2,id&2);return float4(p*2-1,.5,1);}
    struct Out{float4 a:SV_Target0;float4 b:SV_Target1;};Out PS(){Out o;o.a=color;o.b=float4(1-color.rgb,1);return o;})";
    gfx::GraphicsPipelineDesc p;p.vertex=take(gfx::compileHlsl(shader,"VS",gfx::ShaderStage::Vertex));p.fragment=take(gfx::compileHlsl(shader,"PS",gfx::ShaderStage::Fragment));
    p.bindingLayout={{{gfx::BindingType::ConstantBuffer,0}}, {}};p.renderTargets=gfx::RenderTargetLayout{{F::RGBA8UNorm,F::RGBA8UNorm},F::D32Float};p.depthState={true,true,gfx::CompareOp::Less};return p;
}
struct Assets {
    gfx::BufferPtr constants,readback;
    gfx::TexturePtr a,b,depth;
    gfx::PipelinePtr pipeline;
    gfx::TextureFootprint footprint;
};
Assets assets(gfx::Device& d,const gfx::GraphicsPipelineDesc& pd,unsigned iteration){
    Assets a;a.constants=take(d.createBuffer({256,gfx::Memory::Upload}));const std::array<float,4> color{iteration%2?0.f:1.f,iteration%2?1.f:0.f,0,1};take(a.constants->write(0,std::as_bytes(std::span{color})));
    a.a=take(d.createTexture({32,24,F::RGBA8UNorm,true}));a.b=take(d.createTexture({32,24,F::RGBA8UNorm,true}));a.depth=take(d.createTexture({32,24,F::D32Float,false,true}));a.pipeline=take(d.createGraphicsPipeline(pd));a.footprint=a.a->footprint();a.readback=take(d.createBuffer({a.footprint.size,gfx::Memory::Readback}));return a;
}
std::unique_ptr<gfx::CommandList> record(gfx::Device& d,const Assets& a){
    auto c=take(d.createCommandList());take(c->begin());take(c->transition(a.constants,S::ConstantBuffer));take(c->transition(a.a,S::RenderTarget));take(c->transition(a.b,S::RenderTarget));take(c->transition(a.depth,S::DepthWrite));
    take(c->beginRenderPass({{{a.a},{a.b}},{a.depth}}));take(c->setPipeline(a.pipeline));take(c->bindResources({{},{{0,0,a.constants,0,256}}}));take(c->draw(3));take(c->endRenderPass());
    take(c->transition(a.a,S::CopySource));take(c->copyTextureToBuffer(a.a,a.readback));take(c->end());return c;
}
void verify(const Assets& a,unsigned iteration){
    auto data=take(a.readback->read(0,a.footprint.size));
    for(unsigned y=0;y<24;++y)for(unsigned x=0;x<32;++x)for(unsigned channel=0;channel<4;++channel){unsigned expected=channel==3||channel==iteration%2?255:0;check(data[y*a.footprint.rowPitch+x*4+channel]==std::byte(expected),"Recovered full CPU image reference");}++images;
}
void present(gfx::Device& d,gfx::SwapChain& swap){
    auto back=take(swap.acquire());auto c=take(d.createCommandList());take(c->begin());take(c->transition(back,S::RenderTarget));take(c->beginRenderPass(back,{.25f,.5f,.75f,1}));take(c->endRenderPass());take(c->transition(back,S::Present));take(c->end());auto fence=take(d.queue().submit(*c));take(swap.present());take(d.queue().wait(fence));
}
void exercise(gfx::DeviceDesc deviceDesc){
    {auto ordinary=take(gfx::createDevice({deviceDesc.backend,deviceDesc.validation,deviceDesc.softwareAdapter}));take(ordinary->checkStatus());reject(ordinary->removeForTesting(),gfx::ErrorCode::Unsupported,"Native removal is opt-in");reject(ordinary->recover(),gfx::ErrorCode::InvalidState,"Healthy recovery must not discard the device");take(ordinary->checkStatus());}
    auto d=take(gfx::createDevice(deviceDesc));if(!d->caps().deviceLossInjection)throw std::runtime_error("This acceptance run requires native RemoveDevice support");check(d->caps().deviceRecovery,"Recovery capability");
    Window window;const auto pd=description();
    // Retain complete lost generations, including active/ended/submitted commands and swapchains.
    // Re-creation must not depend on the application dropping these wrappers first.
    std::vector<std::unique_ptr<gfx::Device>> oldDevices;
    std::vector<Assets> oldAssets;
    std::vector<std::unique_ptr<gfx::CommandList>> oldCommands;
    std::vector<std::unique_ptr<gfx::SwapChain>> oldSwaps;
    std::vector<gfx::TexturePtr> oldBackbuffers;
    for(unsigned cycle=0;cycle<4;++cycle){
        take(d->checkStatus());reject(d->recover(),gfx::ErrorCode::InvalidState,"Healthy generation rejects recovery");
        auto a=assets(*d,pd,cycle);auto complete=record(*d,a);auto completedFence=take(d->queue().submit(*complete));take(d->queue().wait(completedFence));verify(a,cycle);
        auto swap=take(d->createSwapChain({gfx::SurfaceKind::Win32,window.handle},{64,64,2,false}));present(*d,*swap);auto back=take(swap->acquire());
        auto pending=record(*d,a);auto pendingFence=take(d->queue().submit(*pending)); // No wait before removal.
        auto ended=record(*d,a);auto active=take(d->createCommandList());take(active->begin());take(active->transition(back,S::RenderTarget));take(active->beginRenderPass(back,{0,0,1,1}));
        bool wrongThread=false;std::thread worker([&]{auto r=d->removeForTesting();wrongThread=!r&&r.error().code==gfx::ErrorCode::InvalidState;});worker.join();check(wrongThread,"Injection obeys thread affinity");take(d->checkStatus());
        if(deviceDesc.validation)check(take(d->validationErrorCount())==0,"No unexpected validation error before removal");
        std::unique_ptr<gfx::Device> alias;
        if(cycle==1)alias=take(gfx::createDevice({deviceDesc.backend,false,deviceDesc.softwareAdapter})); // Native per-adapter singleton intentionally blocks the first re-create.
        take(d->removeForTesting());auto status=d->checkStatus();check(!status&&status.error().code==gfx::ErrorCode::DeviceLost&&status.error().nativeCode!=0,"Native runtime reports real device removal");const auto reason=status.error().nativeCode;
        lost(d->resourceStatistics(),reason);
        lost(d->checkStatus(),reason);lost(d->createBuffer({16}),reason);lost(d->createTexture({2,2}),reason);lost(d->createGraphicsPipeline(pd),reason);lost(d->createCommandList(),reason);lost(d->createSwapChain({gfx::SurfaceKind::Win32,window.handle},{64,64,2,false}),reason);lost(d->queue().submit(*ended),reason);lost(d->queue().wait(pendingFence),reason);lost(d->queue().completedValue(),reason);lost(d->queue().waitIdle(),reason);
        lost(a.constants->write(0,{}),reason);lost(a.readback->read(0,4),reason);lost(pending->begin(),reason);lost(ended->begin(),reason);lost(active->draw(3),reason);lost(active->endRenderPass(),reason);lost(active->end(),reason);lost(swap->acquire(),reason);lost(swap->present(),reason);lost(swap->resize(32,32),reason);lost(d->validationErrorCount(),reason);
        bool recoveryThread=false;std::thread another([&]{auto r=d->recover();recoveryThread=!r&&r.error().code==gfx::ErrorCode::InvalidState;});another.join();check(recoveryThread,"Recovery obeys thread affinity");
        if(alias){reject(d->recover(),gfx::ErrorCode::DeviceLost,"Recovery blocked by another owner must preserve retryable loss");lost(alias->checkStatus(),reason);alias.reset();}
        auto fresh=take(d->recover());take(fresh->checkStatus());check(fresh->caps().softwareAdapter==deviceDesc.softwareAdapter,"Recovery retains hardware/WARP selection policy");reject(d->recover(),gfx::ErrorCode::InvalidState,"A successfully recovered generation cannot recover twice");
        lost(d->resourceStatistics(),reason);auto empty=take(fresh->resourceStatistics());check(empty.trackedObjects==0&&empty.uniqueResources==0&&empty.resourceAllocationBytes==0&&empty.pendingSubmissions==0&&empty.pendingObjectReferences==0,"Recovered generation starts without retained resources");
        lost(d->checkStatus(),reason);lost(a.constants->write(0,{}),reason);lost(a.readback->read(0,4),reason);lost(active->endRenderPass(),reason);lost(ended->begin(),reason);lost(swap->present(),reason);
        check(a.a->description().width==32&&a.constants->description().size==256,"Lost wrappers retain CPU descriptions");
        auto next=assets(*fresh,pd,cycle+1);auto c=take(fresh->createCommandList());take(c->begin());reject(c->transition(a.a,S::RenderTarget),gfx::ErrorCode::WrongDevice,"Old texture is not a new-generation texture");reject(c->setPipeline(a.pipeline),gfx::ErrorCode::WrongDevice,"Old pipeline is not a new-generation pipeline");reject(c->bindVertexBuffer(0,a.constants,16),gfx::ErrorCode::WrongDevice,"Old buffer is not a new-generation buffer");take(c->setPipeline(next.pipeline));reject(c->bindResources({{},{{0,0,a.constants,0,256}}}),gfx::ErrorCode::WrongDevice,"Old CBV is not a new-generation binding");take(c->end());
        reject(fresh->queue().submit(*ended),gfx::ErrorCode::WrongDevice,"Old commands cannot be submitted after recovery");reject(fresh->queue().wait(completedFence),gfx::ErrorCode::WrongDevice,"Old fences cannot satisfy new-generation waits");
        c=record(*fresh,next);auto fence=take(fresh->queue().submit(*c));take(fresh->queue().wait(fence));verify(next,cycle+1);
        auto newSwap=take(fresh->createSwapChain({gfx::SurfaceKind::Win32,window.handle},{64,64,2,false}));present(*fresh,*newSwap);take(newSwap->resize(96,96));present(*fresh,*newSwap);newSwap.reset();
        for(unsigned i=0;i<600;++i){auto churn=take(fresh->createBuffer({256,gfx::Memory::Upload}));} // Exercise expired weak-registration pruning.
        take(fresh->queue().waitIdle());if(deviceDesc.validation)check(take(fresh->validationErrorCount())==0,"Fresh generation has no native/GBV errors");
        oldAssets.push_back(std::move(a));oldCommands.push_back(std::move(pending));oldCommands.push_back(std::move(ended));oldCommands.push_back(std::move(active));oldSwaps.push_back(std::move(swap));oldBackbuffers.push_back(std::move(back));oldDevices.push_back(std::move(d));d=std::move(fresh);
        std::cout<<"recovery_cycle="<<cycle<<" native_reason="<<reason<<" full_images_before_after hidden_present_resize old_handles_retained passed\n";
    }
    take(d->checkStatus());take(d->queue().waitIdle());
    oldCommands.clear();oldSwaps.clear();oldBackbuffers.clear();oldAssets.clear();oldDevices.clear();take(d->checkStatus());
    if(deviceDesc.validation)check(take(d->validationErrorCount())==0,"Lost-wrapper destruction does not damage recovered generation");
}
}
int main(int argc,char** argv)try{
    std::string_view mode=argc>1?argv[1]:"--gpu";gfx::DeviceDesc desc{gfx::Backend::D3D12,mode=="--validation",mode=="--warp",true};
    {auto probe=gfx::createDevice(desc);if(!probe&&probe.error().code==gfx::ErrorCode::Unsupported){std::cout<<"SKIP: "<<probe.error().message<<'\n';return 77;}
     auto device=take(std::move(probe));if(!device->caps().deviceLossInjection){std::cout<<"SKIP: native RemoveDevice is unavailable\n";return 77;}std::cout<<"Adapter: "<<device->caps().adapterName<<" software="<<device->caps().softwareAdapter<<'\n';}
    exercise(desc);
    std::cout<<"PASS: "<<assertions<<" assertions, "<<images<<" full images, 4 native removal/recovery cycles\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
