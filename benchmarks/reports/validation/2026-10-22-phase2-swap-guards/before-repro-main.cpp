#include <graphics/Graphics.hpp>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <iostream>
#include <stdexcept>
template<class T>T take(gfx::Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(*r);}
void take(gfx::Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
int main()try{
    HWND window=CreateWindowExW(0,L"STATIC",L"GraphicsSDK stale backbuffer reproduction",WS_OVERLAPPEDWINDOW,0,0,128,128,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window)return 1;
    struct Close{HWND w;~Close(){DestroyWindow(w);}} close{window};
    auto d=take(gfx::createDevice({gfx::Backend::D3D12,false,true}));
    auto swap=take(d->createSwapChain({gfx::SurfaceKind::Win32,window},{64,64,2,false}));auto old=take(swap->acquire());
    auto c=take(d->createCommandList());take(c->begin());take(c->transition(old,gfx::ResourceState::RenderTarget));take(c->beginRenderPass(old,{1,0,0,1}));take(c->endRenderPass());take(c->transition(old,gfx::ResourceState::Present));take(c->end());
    auto f=take(d->queue().submit(*c));take(swap->present());take(d->queue().wait(f));auto current=take(swap->acquire());
    if(old==current)throw std::runtime_error("Present did not advance the backbuffer index");
    take(c->begin());const auto wrong=d->checkStatus()?c->transition(old,gfx::ResourceState::RenderTarget):gfx::Result<void>{std::unexpected(gfx::Error{gfx::ErrorCode::DeviceLost,"Unexpected loss"})};take(c->end());c.reset();
    swap.reset();c=take(d->createCommandList());take(c->begin());const auto destroyed=c->transition(current,gfx::ResourceState::RenderTarget);take(c->end());
    std::cout<<"{\"sdk\":\""<<gfx::buildFingerprint()<<"\",\"wrong_buffer_recording_accepted\":"<<bool(wrong)<<",\"destroyed_chain_recording_accepted\":"<<bool(destroyed)<<"}\n";
    return wrong||destroyed?2:0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
