#include <graphics/Graphics.hpp>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <array>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using S=gfx::ResourceState;
unsigned assertions{},images{},rejections{},ringCases{},recoveries{};
std::uint64_t pixels{};
void check(bool value,const char* message){++assertions;if(!value)throw std::runtime_error(message);}
template<class T>T take(gfx::Result<T> r){if(!r)throw std::runtime_error(r.error().message+" native="+std::to_string(r.error().nativeCode));return std::move(*r);}
void take(gfx::Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
template<class T>void reject(gfx::Result<T> r,gfx::ErrorCode code,const char* message){check(!r&&r.error().code==code,message);++rejections;}
struct Window {
    HWND handle=CreateWindowExW(0,L"STATIC",L"GraphicsSDK swapchain guard test",WS_OVERLAPPEDWINDOW,0,0,128,128,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Window(){check(handle!=nullptr,"Create hidden test window");}~Window(){DestroyWindow(handle);}
};
auto command(gfx::Device& d){auto c=take(d.createCommandList());take(c->begin());return c;}
auto recorded(gfx::Device& d,const gfx::TexturePtr& texture){auto c=command(d);take(c->transition(texture,S::Present));take(c->end());return c;}
void submitRejected(gfx::Device& d,gfx::CommandList& c){
    const auto timeline=take(d.queue().completedValue());
    reject(d.queue().submit(c),gfx::ErrorCode::InvalidState,"Stale backbuffer submission rejected before execution");
    check(take(d.queue().completedValue())==timeline,"Rejected submit leaves the SDK fence timeline unchanged");
}
void empty(gfx::Device& d){take(d.queue().waitIdle());const auto s=take(d.resourceStatistics());check(s.trackedObjects==0&&s.uniqueResources==0&&s.resourceAllocationBytes==0&&s.pendingSubmissions==0&&s.pendingObjectReferences==0,"Release all swapchain/command/texture/readback ownership");}
void verify(const gfx::BufferPtr& readback,gfx::TextureFootprint layout,unsigned width,unsigned height,gfx::Color color){
    const auto data=take(readback->read(0,layout.size));
    const std::array<unsigned,4> expected{unsigned(color.r*255),unsigned(color.g*255),unsigned(color.b*255),unsigned(color.a*255)};
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)for(unsigned k=0;k<4;++k)
        check(data[y*layout.rowPitch+x*4+k]==std::byte(expected[k]),"Full backbuffer clear/readback reference");
    ++images;pixels+=std::uint64_t(width)*height;
}
void frame(gfx::Device& d,gfx::SwapChain& swap,gfx::Color color,std::unique_ptr<gfx::CommandList> c={}){
    auto back=take(swap.acquire());const auto desc=back->description();const auto layout=back->footprint();
    auto readback=take(d.createBuffer({layout.size,gfx::Memory::Readback}));if(!c)c=command(d);
    take(c->transition(back,S::RenderTarget));take(c->beginRenderPass(back,color));take(c->endRenderPass());
    take(c->transition(back,S::CopySource));take(c->copyTextureToBuffer(back,readback));take(c->transition(back,S::Present));take(c->end());
    const auto f=take(d.queue().submit(*c));take(swap.present());take(d.queue().wait(f));verify(readback,layout,desc.width,desc.height,color);
}
void ring(gfx::Device& d,unsigned buffers){
    Window window;auto swap=take(d.createSwapChain({gfx::SurfaceKind::Win32,window.handle},{63,7,buffers,false}));
    auto held=take(swap->acquire());frame(d,*swap,{1,0,0,1});
    auto current=take(swap->acquire());check(held!=current,"Present advances to another backbuffer");
    reject(swap->resize(65,9),gfx::ErrorCode::InvalidState,"Held backbuffer still blocks resize");
    auto c=command(d);auto readback=take(d.createBuffer({held->footprint().size,gfx::Memory::Readback}));auto upload=take(d.createBuffer({held->footprint().size,gfx::Memory::Upload}));
    for(auto state:{S::Present,S::RenderTarget,S::CopySource,S::CopyDestination})reject(c->transition(held,state),gfx::ErrorCode::InvalidState,"Noncurrent backbuffer transition rejected");
    reject(c->beginRenderPass(held,{0,0,0,1}),gfx::ErrorCode::InvalidState,"Noncurrent color attachment rejected");
    take(c->transition(current,S::RenderTarget));
    reject(c->beginRenderPass({{{current},{held}}, {}}),gfx::ErrorCode::InvalidState,"Mixed current/noncurrent MRT rejected atomically");
    reject(c->copyTextureToBuffer(held,readback),gfx::ErrorCode::InvalidState,"Noncurrent backbuffer readback rejected");
    reject(c->copyBufferToTexture(upload,held),gfx::ErrorCode::InvalidState,"Noncurrent backbuffer upload rejected");
    // Rejections must leave this command list usable for a correct current frame.
    frame(d,*swap,{0,1,0,1},std::move(c));readback.reset();upload.reset();current.reset();
    auto target=take(swap->acquire());auto stale=recorded(d,target);
    take(swap->present());submitRejected(d,*stale);
    for(unsigned i=0;take(swap->acquire())!=target;++i){check(i<buffers,"Backbuffer ring returns to original target");take(swap->present());}
    submitRejected(d,*stale); // Same buffer is current again; present version must still reject it.
    stale.reset();frame(d,*swap,{0,0,1,1});
    held.reset();target.reset();take(swap->resize(65,9));frame(d,*swap,{1,0,1,1});
    auto dead=take(swap->acquire());stale=recorded(d,dead);swap.reset();
    check(dead->description().width==65&&dead->footprint().height==9,"Destroyed-chain wrapper keeps CPU metadata");
    c=command(d);reject(c->transition(dead,S::RenderTarget),gfx::ErrorCode::InvalidState,"Destroyed-chain transition rejected");take(c->end());c.reset();
    submitRejected(d,*stale);stale.reset();dead.reset();empty(d);++ringCases;
    std::cout<<"{\"event\":\"swap_guard_ring\",\"adapter_id\":"<<d.caps().adapterId.value<<",\"buffers\":"<<buffers<<",\"full_images\":4,\"passed\":true}\n";
}
void independent(gfx::Device& d){
    Window a,b;auto first=take(d.createSwapChain({gfx::SurfaceKind::Win32,a.handle},{53,11,2,false}));auto second=take(d.createSwapChain({gfx::SurfaceKind::Win32,b.handle},{53,11,3,false}));
    auto back=take(second->acquire());const auto desc=back->description();const auto layout=back->footprint();auto readback=take(d.createBuffer({layout.size,gfx::Memory::Readback}));auto c=command(d);
    take(c->transition(back,S::RenderTarget));take(c->beginRenderPass(back,{0,1,0,1}));take(c->endRenderPass());take(c->transition(back,S::CopySource));take(c->copyTextureToBuffer(back,readback));take(c->transition(back,S::Present));take(c->end());
    take(first->present());first.reset();const auto f=take(d.queue().submit(*c));take(second->present());take(d.queue().wait(f));verify(readback,layout,desc.width,desc.height,{0,1,0,1});
    c.reset();back.reset();readback.reset();frame(d,*second,{0,0,1,1});second.reset();empty(d);
}
void recovery(std::unique_ptr<gfx::Device>& d,bool validation){
    Window window;auto swap=take(d->createSwapChain({gfx::SurfaceKind::Win32,window.handle},{31,5,2,false}));auto old=take(swap->acquire());auto c=recorded(*d,old);
    bool wrongThread=false;std::thread other([&]{auto r=swap->acquire();wrongThread=!r&&r.error().code==gfx::ErrorCode::InvalidState;});other.join();check(wrongThread,"Acquire obeys thread affinity");
    if(validation)check(take(d->validationErrorCount())==0,"Healthy swapchain validation errors");
    take(d->removeForTesting());auto status=d->checkStatus();reject(status,gfx::ErrorCode::DeviceLost,"Observed controlled loss");const auto reason=status.error().nativeCode;
    auto fresh=take(d->recover());auto r=swap->acquire();reject(r,gfx::ErrorCode::DeviceLost,"Retired acquire preserves loss before liveness guard");check(r.error().nativeCode==reason,"Retired swapchain native reason preserved");
    reject(d->queue().submit(*c),gfx::ErrorCode::DeviceLost,"Retired command submit preserves loss");
    auto next=take(fresh->createSwapChain({gfx::SurfaceKind::Win32,window.handle},{31,5,2,false}));auto nextCommands=command(*fresh);
    reject(nextCommands->transition(old,S::RenderTarget),gfx::ErrorCode::WrongDevice,"Old generation buffer is foreign before liveness validation");take(nextCommands->end());nextCommands.reset();
    frame(*fresh,*next,{0,1,1,1});next.reset();c.reset();old.reset();swap.reset();empty(*fresh);
    if(validation)check(take(fresh->validationErrorCount())==0,"Recovered swapchain validation errors");d=std::move(fresh);++recoveries;
}
}
int main(int argc,char** argv)try{
    check(argc==2,"Expected --gpu or --validation");const std::string_view mode=argv[1];check(mode=="--gpu"||mode=="--validation","Unknown mode");const bool validation=mode=="--validation";
    const auto adapters=take(gfx::enumerateAdapters());check(!adapters.empty(),"Real adapters required");
    for(const auto& adapter:adapters){auto d=take(gfx::createDevice({gfx::Backend::D3D12,validation,adapter.softwareAdapter,true,adapter.id}));for(auto n:{2u,3u,4u})ring(*d,n);independent(*d);recovery(d,validation);}
    std::cout<<"{\"event\":\"swap_guard_result\",\"sdk_sha256\":\""<<gfx::buildFingerprint()<<"\",\"adapters\":"<<adapters.size()<<",\"validation\":"<<(validation?"true":"false")<<",\"ring_cases\":"<<ringCases<<",\"full_images\":"<<images<<",\"verified_pixels\":"<<pixels<<",\"rejections\":"<<rejections<<",\"recoveries\":"<<recoveries<<",\"passed\":true}\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
