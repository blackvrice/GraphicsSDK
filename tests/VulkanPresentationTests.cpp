#include "VulkanDepthSupport.hpp"
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using S = gfx::ResourceState;
unsigned assertions{}, frames{}, images{}, draws{}, resizes{}, shown{};
unsigned supportedAdapters{}, unsupportedAdapters{};
std::uint64_t bytesVerified{};
void check(bool b, const char* m) { ++assertions; if (!b) throw std::runtime_error(m); }
template<class T> T take(gfx::Result<T> r) { if (!r) throw std::runtime_error(r.error().message + " native=" + std::to_string(r.error().nativeCode)); return std::move(*r); }
void take(gfx::Result<void> r) { if (!r) throw std::runtime_error(r.error().message + " native=" + std::to_string(r.error().nativeCode)); }
template<class T> void reject(gfx::Result<T> r, gfx::ErrorCode code, const char* m) { check(!r && r.error().code == code, m); }
struct Window {
    HWND handle{};
    Window(unsigned w, unsigned h) {
        RECT r{0, 0, LONG(w), LONG(h)}; check(AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0) != 0, "Adjust client extent");
        handle = CreateWindowExW(0, L"STATIC", L"GraphicsSDK Vulkan validation", WS_OVERLAPPEDWINDOW, 20, 20, r.right-r.left, r.bottom-r.top, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        check(handle != nullptr, "Create real native window");
    }
    void size(unsigned w, unsigned h) {
        RECT r{0, 0, LONG(w), LONG(h)}; check(AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0) != 0, "Adjust resized client extent");
        check(SetWindowPos(handle, nullptr, 0, 0, r.right-r.left, r.bottom-r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != 0, "Resize native window first"); pump();
    }
    void pump() { MSG msg; while (PeekMessageW(&msg, handle, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
    void show() { ShowWindow(handle, SW_SHOWNOACTIVATE); pump(); ++shown; }
    void destroy() { if (handle) DestroyWindow(handle); handle = {}; }
    ~Window() { destroy(); }
};
gfx::DeviceDesc description(const gfx::AdapterInfo& a, bool validation) { gfx::DeviceDesc d; d.backend = gfx::Backend::Vulkan; d.adapter = a.id; d.softwareAdapter = a.softwareAdapter; d.validation = validation; return d; }
auto chain(gfx::Device& d, Window& w, unsigned width, unsigned height, unsigned count=2, bool vsync=false) { return take(d.createSwapChain({gfx::SurfaceKind::Win32, w.handle}, {width, height, count, vsync})); }
void reclaimed(gfx::Device& d) {
    take(d.queue().waitIdle()); auto r = take(d.resourceStatistics());
    check(!r.uniqueResources && !r.resourceAllocationBytes && !r.trackedObjects && !r.pendingSubmissions && !r.pendingObjectReferences, "All native images/views/semaphores/recordings reclaimed");
}
void frame(gfx::Device& d, gfx::SwapChain& s, unsigned seed, bool draw, bool drop) {
    auto t = take(s.acquire()); check(take(s.acquire()) == t, "Repeated acquire is stable until present");
    const auto td = t->description(); const auto fp = t->footprint();
    check(td.format == gfx::Format::RGBA8UNorm && td.renderTarget && !td.depthStencil && fp.rowBytes == td.width * 4, "Backbuffer has truthful public format/footprint");
    std::array<float, 4> color{float(seed % 3) * .25f, .5f, .75f, 1.f};
    auto c = take(d.createCommandList()); take(c->begin()); take(c->transition(t, S::RenderTarget));
    gfx::PipelinePtr p; gfx::BufferPtr constant; gfx::TexturePtr z;
    if (draw) {
        p = take(d.createGraphicsPipeline(depthTest::pipeline({{gfx::Format::RGBA8UNorm}, gfx::Format::D32Float}, {true, true, gfx::CompareOp::Less})));
        z = take(d.createTexture({td.width, td.height, gfx::Format::D32Float, false, true}));
        std::array<float, 64> data{}; std::copy(color.begin(), color.end(), data.begin()); data[4] = .25f;
        constant = take(d.createBuffer({256, gfx::Memory::Upload})); take(constant->write(0, std::as_bytes(std::span(data))));
        take(c->transition(constant, S::ConstantBuffer)); take(c->transition(z, S::DepthWrite));
        take(c->beginRenderPass({{{t}}, {z}})); take(c->setPipeline(p)); take(c->bindResources({{}, {{0, 0, constant, 0, 256}}})); take(c->draw(3)); ++draws;
    } else take(c->beginRenderPass(t, {color[0], color[1], color[2], color[3]}));
    take(c->endRenderPass());
    auto r = take(d.createBuffer({fp.size, gfx::Memory::Readback})); auto sentinel = take(d.createBuffer({fp.size, gfx::Memory::Upload}));
    take(sentinel->write(0, std::vector<std::byte>(static_cast<size_t>(fp.size), std::byte{0xab})));
    take(c->copyBuffer(sentinel, r, fp.size)); take(c->transition(t, S::CopySource)); take(c->copyTextureToBuffer(t, r)); take(c->transition(t, S::Present)); take(c->end());
    const auto fence = take(d.queue().submit(*c)); if (drop) c.reset();
    // Present synchronizes through a native semaphore, without a user fence wait.
    take(s.present()); ++frames; take(d.queue().wait(fence));
    auto data = take(r->read(0, fp.size)); bool pixels=true, padding=true;
    for (unsigned y=0; y<td.height; ++y) {
        const auto base = std::size_t(y)*fp.rowPitch;
        for (unsigned x=0; x<td.width; ++x) for (unsigned k=0; k<4; ++k)
            pixels &= std::abs(std::to_integer<unsigned>(data[base+x*4+k])/255.f-color[k]) <= 1.1f/255;
        padding &= std::all_of(data.begin()+base+fp.rowBytes, data.begin()+base+fp.rowPitch, [](auto b) { return b == std::byte{0xab}; });
    }
    check(pixels, "Every acquired image pixel matches independent clear/draw reference"); check(padding, "Every readback padding byte preserved");
    ++images; bytesVerified += std::uint64_t(td.width)*td.height*4;
    // Old wrapper is unusable before another native acquisition, even if the next index repeats.
    auto invalid = take(d.createCommandList()); take(invalid->begin()); reject(invalid->transition(t, S::RenderTarget), gfx::ErrorCode::InvalidState, "Presented image no longer current");
}
void uploadFrame(gfx::Device& d, gfx::SwapChain& s, unsigned seed) {
    auto t=take(s.acquire()); const auto fp=t->footprint();
    std::vector<std::byte> expected(static_cast<size_t>(fp.size),std::byte{0x5c});
    for(unsigned y=0;y<fp.height;++y) for(unsigned x=0;x<fp.rowBytes;++x)
        expected[std::size_t(y)*fp.rowPitch+x]=std::byte((x*37+y*13+seed*7)&255);
    auto u=take(d.createBuffer({fp.size,gfx::Memory::Upload})), r=take(d.createBuffer({fp.size,gfx::Memory::Readback})); take(u->write(0,expected));
    auto c=take(d.createCommandList()); take(c->begin()); take(c->copyBuffer(u,r,fp.size));
    take(c->transition(t,S::CopyDestination)); take(c->copyBufferToTexture(u,t)); take(c->transition(t,S::CopySource)); take(c->copyTextureToBuffer(t,r)); take(c->transition(t,S::Present)); take(c->end());
    auto f=take(d.queue().submit(*c)); u.reset(); c.reset(); take(s.present()); ++frames; take(d.queue().wait(f));
    check(take(r->read(0,fp.size))==expected,"All uploaded backbuffer pixels and independent staging padding agree"); ++images; bytesVerified+=std::uint64_t(fp.rowBytes)*fp.height;
}
void contracts(gfx::Device& d) {
    Window w(321,197); check(d.caps().swapChain, "Actual Win32 extension capability advertised");
    reject(d.createSwapChain({}, {321,197}), gfx::ErrorCode::InvalidArgument, "Null window");
    reject(d.createSwapChain({gfx::SurfaceKind::Win32,w.handle}, {0,197}), gfx::ErrorCode::InvalidArgument, "Zero extent");
    reject(d.createSwapChain({gfx::SurfaceKind::Win32,w.handle}, {322,197}), gfx::ErrorCode::InvalidArgument, "No silent native window mutation");
    for (unsigned n : {0u,1u,5u,UINT32_MAX}) reject(d.createSwapChain({gfx::SurfaceKind::Win32,w.handle}, {321,197,n}), gfx::ErrorCode::InvalidArgument, "Invalid requested buffer count");
    auto s=chain(d,w,321,197); auto t=take(s->acquire());
    { auto stats=take(d.resourceStatistics()); check(stats.uniqueResources>=2 && !stats.resourceAllocationBytes,"External images counted without fabricating native allocation bytes"); }
    auto c=take(d.createCommandList()); take(c->begin());
    auto offscreen=take(d.createTexture({321,197,gfx::Format::RGBA8UNorm,true}));
    reject(c->transition(offscreen,S::Present),gfx::ErrorCode::InvalidArgument,"Offscreen Present rejected");
    reject(c->transition(t,S::ShaderRead),gfx::ErrorCode::Unsupported,"Unsupported backbuffer sampling");
    take(c->transition(t,S::RenderTarget)); take(c->end()); auto f=take(d.queue().submit(*c)); take(d.queue().wait(f));
    reject(s->present(),gfx::ErrorCode::InvalidState,"Present requires submitted Present state");
    take(c->begin()); take(c->transition(t,S::Present)); take(c->end()); take(d.queue().wait(take(d.queue().submit(*c)))); c.reset();
    auto stale=take(d.createCommandList()); take(stale->begin()); take(stale->transition(t,S::Present)); take(stale->end());
    take(s->present()); ++frames; reject(d.queue().submit(*stale),gfx::ErrorCode::InvalidState,"Present invalidates recorded commands before reacquire");
    for(unsigned i=0;i<12;++i) { (void)take(s->acquire()); take(s->present()); ++frames; }
    (void)take(s->acquire()); reject(d.queue().submit(*stale),gfx::ErrorCode::InvalidState,"Stale generation rejected after arbitrary image cycling");
    reject(s->resize(0,197),gfx::ErrorCode::InvalidArgument,"Invalid resize preserves old chain");
    reject(s->resize(321,197),gfx::ErrorCode::InvalidState,"Retained wrapper/recording blocks resize");
    t.reset(); reject(s->resize(321,197),gfx::ErrorCode::InvalidState,"Recorded allocation alone blocks resize"); stale.reset();
    take(s->resize(321,197)); ++resizes;
    bool wrongThread=false; std::thread other([&] { auto a=s->acquire(); auto b=s->present(); auto r=s->resize(321,197); wrongThread=!a && !b && !r && a.error().code==gfx::ErrorCode::InvalidState && b.error().code==gfx::ErrorCode::InvalidState && r.error().code==gfx::ErrorCode::InvalidState; }); other.join(); check(wrongThread,"WSI operations require creating thread");
    auto dead=take(s->acquire()); auto recorded=take(d.createCommandList()); take(recorded->begin()); take(recorded->transition(dead,S::RenderTarget)); take(recorded->end()); s.reset();
    reject(d.queue().submit(*recorded),gfx::ErrorCode::InvalidState,"Destroyed chain blocks previously recorded submit");
    take(recorded->begin()); reject(recorded->transition(dead,S::RenderTarget),gfx::ErrorCode::InvalidState,"Retained wrapper after destruction is inert");
    check(dead->description().width==321,"Retained CPU description survives");
    // Destruction waits an actual user submission whose public recording was dropped.
    Window queuedWindow(321,197); auto queued=chain(d,queuedWindow,321,197); auto qt=take(queued->acquire()); auto q=take(d.createCommandList());
    take(q->begin()); take(q->transition(qt,S::RenderTarget)); take(q->beginRenderPass(qt,{1,0,0,1})); take(q->endRenderPass()); take(q->transition(qt,S::Present)); take(q->end());
    auto pending=take(d.queue().submit(*q)); q.reset(); queued.reset(); take(d.queue().wait(pending));
    auto invalid=take(d.createCommandList()); take(invalid->begin()); reject(invalid->transition(qt,S::Present),gfx::ErrorCode::InvalidState,"Dropped in-flight chain leaves inert wrapper");
}
void windows(gfx::Device& d, unsigned count, bool vsync) {
    Window a(321,197), b(353,211); if(count==2 && !vsync) a.show();
    auto sa=chain(d,a,321,197,count,vsync), sb=chain(d,b,353,211,count,vsync);
    for(unsigned i=0;i<8;++i) { frame(d,*sa,i,i%2==1,i%3==0); frame(d,*sb,i+1,i%2==0,i%3==1); a.pump(); b.pump(); }
    uploadFrame(d,*sa,count); uploadFrame(d,*sb,count+1);
    reject(sa->resize(323,199),gfx::ErrorCode::InvalidArgument,"Resize requires matching client extent");
    a.size(323,199); take(sa->resize(323,199)); ++resizes;
    for(unsigned i=0;i<4;++i) frame(d,*sa,i+20,i%2==0,true);
    // Destroying a caller-owned HWND blocks native image use, including stored commands.
    auto t=take(sb->acquire()); auto c=take(d.createCommandList()); take(c->begin()); take(c->transition(t,S::Present)); take(c->end()); b.destroy();
    reject(sb->acquire(),gfx::ErrorCode::InvalidState,"Destroyed window acquire"); reject(sb->present(),gfx::ErrorCode::InvalidState,"Destroyed window present");
    reject(d.queue().submit(*c),gfx::ErrorCode::InvalidState,"Destroyed window submit");
}
void foreign(const gfx::AdapterInfo& a, bool validation) {
    auto d=take(gfx::createDevice(description(a,validation))), other=take(gfx::createDevice(description(a,validation)));
    Window w(321,197); auto s=chain(*d,w,321,197); auto t=take(s->acquire()); auto c=take(other->createCommandList()); take(c->begin());
    reject(c->transition(t,S::RenderTarget),gfx::ErrorCode::WrongDevice,"Foreign backbuffer");
    auto held=take(d->createCommandList()); take(held->begin()); take(held->transition(t,S::Present)); take(held->end());
    d.reset(); reject(s->acquire(),gfx::ErrorCode::DeviceLost,"Retired acquire"); reject(s->present(),gfx::ErrorCode::DeviceLost,"Retired present"); reject(s->resize(321,197),gfx::ErrorCode::DeviceLost,"Retired resize");
    reject(held->begin(),gfx::ErrorCode::DeviceLost,"Retired WSI command inert"); check(t->footprint().height==197,"Retired CPU footprint survives");
}
}
int main(int argc,char** argv) try {
    if(argc!=1 && !(argc==2 && std::string_view(argv[1])=="--validation")) throw std::runtime_error("Usage: gfx_vulkan_presentation_tests [--validation]");
    const bool validation=argc==2; std::cout<<"source="<<gfx::buildFingerprint()<<" validation="<<validation<<std::endl;
    auto adapters=take(gfx::enumerateAdapters(gfx::Backend::Vulkan)); check(!adapters.empty(),"Real adapter required");
    for(const auto& a:adapters) {
        std::cout<<"adapter="<<a.name<<" id="<<a.id.value<<std::endl; auto d=take(gfx::createDevice(description(a,validation)));
        if(!d->caps().swapChain) {
            Window w(321,197); auto r=d->createSwapChain({gfx::SurfaceKind::Win32,w.handle},{321,197});
            check(!r && r.error().code==gfx::ErrorCode::Unsupported && r.error().message.find("swapchainMaintenance1")!=std::string::npos,"Missing presentation completion capability fails explicitly");
            check(take(d->validationErrorCount())==0,"Unsupported presentation creates no native errors"); reclaimed(*d); ++unsupportedAdapters;
            std::cout<<"presentation=unsupported (completion fence feature unavailable)"<<std::endl; continue;
        }
        ++supportedAdapters;
        contracts(*d); reclaimed(*d);
        for(unsigned count:{2u,3u,4u}) for(bool vsync:{false,true}) { windows(*d,count,vsync); reclaimed(*d); }
        foreign(a,validation); check(take(d->validationErrorCount())==0,"Presentation core/synchronization errors zero");
    }
    check(supportedAdapters>0,"At least one real presentation adapter required; no all-unsupported pass");
    std::cout<<"PASS presentation_assertions="<<assertions<<" presents="<<frames<<" images="<<images<<" draws="<<draws<<" resizes="<<resizes<<" shown_windows="<<shown<<" verified_texel_bytes="<<bytesVerified<<" supported_adapters="<<supportedAdapters<<" unsupported_adapters="<<unsupportedAdapters<<" adapters="<<adapters.size()<<std::endl; return 0;
} catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<std::endl; return 1; }
