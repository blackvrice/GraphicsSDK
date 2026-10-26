#include "../BgraSupport.hpp"
#include "../linux_xcb_surface/Control.hpp"
#include "Control.hpp"
#include <xcb/xcb.h>
#include <dlfcn.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <iomanip>
#include <cstdlib>
namespace {
using namespace bgraTest;
lifetimeprobe::Control control; lifetimeprobe::Query query; surfaceprobe::Query nativeQuery;
unsigned cases{},rejections{},references{};
std::uint64_t verifiedBytes{};
struct Window {
    xcb_connection_t* connection{};xcb_window_t id{};
    Window(){connection=xcb_connect(nullptr,nullptr);check(connection&&!xcb_connection_has_error(connection),"Actual XCB display");auto s=xcb_setup_roots_iterator(xcb_get_setup(connection));check(s.rem,"Actual screen");id=xcb_generate_id(connection);auto c=xcb_create_window_checked(connection,XCB_COPY_FROM_PARENT,id,s.data->root,0,0,65,7,0,XCB_WINDOW_CLASS_INPUT_OUTPUT,s.data->root_visual,0,nullptr);auto e=xcb_request_check(connection,c);bool ok=!e;std::free(e);check(ok,"Actual XCB window");xcb_map_window(connection,id);xcb_flush(connection);}
    ~Window(){xcb_destroy_window(connection,id);xcb_disconnect(connection);}
    gfx::Surface surface(){return {gfx::SurfaceKind::Xcb,nullptr,connection,id};}
};
template<class T>void reject(gfx::Result<T> r,gfx::ErrorCode e){check(!r&&r.error().code==e,"Exact lifetime rejection");++rejections;}
auto snapshot(){lifetimeprobe::Snapshot s{};check(query(&s,sizeof(s))==1,"Lifetime snapshot ABI");return s;}
auto native(){surfaceprobe::Snapshot s{};check(nativeQuery(&s,sizeof(s))==1,"Native snapshot ABI");return s;}
auto chain(gfx::Device& d,Window& w){return take(d.createSwapChain(w.surface(),{65,7,2,true}));}
void result(const char* name){++cases;std::cout<<"{\"event\":\"case\",\"name\":"<<std::quoted(name)<<",\"rejections\":"<<rejections<<",\"references\":"<<references<<"}"<<std::endl;}
struct ReleaseAtBoundary {
    std::atomic<bool> observed{false};std::jthread thread;
    explicit ReleaseAtBoundary(unsigned boundary):thread([this,boundary]{
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(!control(boundary)&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        observed=control(boundary)==1;
        // Even a failed assertion/early exit must release the real GPU event.
        (void)control(lifetimeprobe::Release);
    }){}
    void join(){thread.join();check(observed,"Actual blocking API boundary observed before gate release");}
};
struct Work {std::unique_ptr<gfx::CommandList> commands;gfx::BufferPtr read;std::vector<std::byte> expected;};
Work record(gfx::Device& d,const gfx::TexturePtr& t){
    auto f=t->footprint();Work w;w.expected.assign(f.size,std::byte{0xb7});
    for(unsigned y=0;y<f.height;++y)for(unsigned x=0;x<65;++x){auto offset=y*f.rowPitch+x*4;w.expected[offset]=std::byte{};w.expected[offset+1]=std::byte{};w.expected[offset+2]=std::byte{255};w.expected[offset+3]=std::byte{255};}
    w.read=take(d.createBuffer({f.size,gfx::Memory::Readback}));auto pad=take(d.createBuffer({f.size,gfx::Memory::Upload}));take(pad->write(0,std::vector<std::byte>(f.size,std::byte{0xb7})));
    w.commands=take(d.createCommandList());auto& c=*w.commands;take(c.begin());take(c.copyBuffer(pad,w.read,f.size));take(c.transition(t,S::RenderTarget));take(c.beginRenderPass(t,{1,0,0,1}));take(c.endRenderPass());take(c.transition(t,S::CopySource));take(c.copyTextureToBuffer(t,w.read));take(c.transition(t,S::Present));take(c.end());return w;
}
void verify(Work& w){check(take(w.read->read(0,w.expected.size()))==w.expected,"Every BGRA texel and padding byte after lifetime boundary");++references;verifiedBytes+=w.expected.size();}
void clean(gfx::Device& d){empty(d);auto s=snapshot();check(s.views==s.destroyedViews&&s.semaphores==s.destroyedSemaphores&&s.fences==s.destroyedFences&&!s.orderErrors,"All native views/semaphores/fences reclaimed in valid order");auto n=native();check(n.chains==n.destroyedChains&&n.surfaces==n.destroyedSurfaces,"Native chains and surfaces reclaimed");}
void retained(gfx::Device& d){
    Window w;auto s=chain(d,w);auto t=take(s->acquire());check(take(s->acquire())==t,"Stable repeated acquire");auto c=take(d.createCommandList());take(c->begin());take(c->transition(t,S::Present));take(c->end());
    auto before=native();reject(s->resize(65,7),gfx::ErrorCode::InvalidState);check(native().chains==before.chains&&native().destroyedChains==before.destroyedChains,"Retained-wrapper resize rejected before native recreation");
    t.reset();reject(s->resize(65,7),gfx::ErrorCode::InvalidState);check(native().chains==before.chains,"Recorded allocation alone prevents native recreation");c.reset();take(s->resize(65,7));
    auto image=take(s->acquire());auto stale=take(d.createCommandList());take(stale->begin());take(stale->transition(image,S::Present));take(stale->end());take(s->present());
    reject(d.queue().submit(*stale),gfx::ErrorCode::InvalidState);auto invalid=take(d.createCommandList());take(invalid->begin());reject(invalid->transition(image,S::RenderTarget),gfx::ErrorCode::InvalidState);take(invalid->end());
    // Cycling can return the same native index; pre-present recordings remain stale.
    for(unsigned n=0;n<8;++n){(void)take(s->acquire());take(s->present());}reject(d.queue().submit(*stale),gfx::ErrorCode::InvalidState);
    s.reset();auto held=native();check(held.chains>held.destroyedChains,"Retained image/recording keeps native chain alive after wrapper destruction");
    reject(d.queue().submit(*stale),gfx::ErrorCode::InvalidState);take(invalid->begin());reject(invalid->transition(image,S::Present),gfx::ErrorCode::InvalidState);check(image->description().width==65&&image->footprint().height==7,"Inert CPU descriptions survive");
    image.reset();stale.reset();invalid.reset();result("retained-and-stale");
}
void pendingDestruction(gfx::Device& d){
    Window w;auto s=chain(d,w);auto t=take(s->acquire());auto work=record(d,t);check(control(lifetimeprobe::Arm),"Arm real event");ReleaseAtBoundary release(lifetimeprobe::IdleEntered);
    auto f=take(d.queue().submit(*work.commands));check(control(lifetimeprobe::Blocked),"Actual native submission fence is NOT_READY");work.commands.reset();t.reset();
    auto stats=take(d.resourceStatistics());check(stats.pendingSubmissions==1&&stats.pendingObjectReferences>0,"Dropped public recording retained by actual pending submission");
    s.reset();release.join();take(d.queue().wait(f));verify(work);check(control(lifetimeprobe::Cleanup),"Destroy completed test gate");work.read.reset();result("pending-chain-destruction");
}
void presentCompletion(gfx::Device& d){
    Window w;auto s=chain(d,w);auto t=take(s->acquire());auto work=record(d,t);auto f=take(d.queue().submit(*work.commands));work.commands.reset();
    check(control(lifetimeprobe::Arm),"Arm ordered present signal");ReleaseAtBoundary release(lifetimeprobe::PresentEntered);auto before=native();take(s->present());release.join();auto after=native();
    check(after.fencedPresents==before.fencedPresents+1&&after.waitedPresents==before.waitedPresents+1,"Actual present has a successfully waited resource-completion fence");take(d.queue().wait(f));verify(work);
    check(control(lifetimeprobe::Cleanup),"Present gate cleanup");s.reset();t.reset();work.read.reset();result("present-completion");
}
void failures(gfx::Device& d){
    Window w;auto before=native();check(control(lifetimeprobe::FailSecondView),"Arm partial view construction failure");reject(d.createSwapChain(w.surface(),{65,7,2,true}),gfx::ErrorCode::OutOfMemory);
    check(native().chains==before.chains+1&&native().destroyedChains==before.destroyedChains+1,"Partial native construction releases chain");auto a=snapshot();check(a.views==a.destroyedViews&&a.semaphores==a.destroyedSemaphores,"Partial image/view/semaphore construction reclaimed");
    auto s=chain(d,w);auto t=take(s->acquire());for(unsigned op:{lifetimeprobe::FailFence,lifetimeprobe::FailSubmit}){auto n=native();check(control(op),"Arm safe pre-call failure");reject(s->present(),gfx::ErrorCode::OutOfMemory);check(native().presents==n.presents,"Rejected attempt never reaches native present");check(take(s->acquire())==t,"Failed pre-present preserves acquisition");}
    auto work=record(d,t);auto f=take(d.queue().submit(*work.commands));work.commands.reset();take(s->present());take(d.queue().wait(f));verify(work);work.read.reset();t.reset();s.reset();result("partial-and-pre-present-failures");
}
void foreign(gfx::Device& d,const gfx::DeviceDesc& desc){
    auto other=take(gfx::createDevice(desc));Window w;auto s=chain(d,w);auto t=take(s->acquire());auto c=take(other->createCommandList());take(c->begin());reject(c->transition(t,S::Present),gfx::ErrorCode::WrongDevice);c.reset();other.reset();t.reset();s.reset();result("foreign-device");
}
void retirement(const gfx::DeviceDesc& desc){
    auto d=take(gfx::createDevice(desc));Window w;auto s=chain(*d,w);auto t=take(s->acquire());auto work=record(*d,t);check(control(lifetimeprobe::Arm),"Arm retirement gate");ReleaseAtBoundary release(lifetimeprobe::IdleEntered);
    (void)take(d->queue().submit(*work.commands));check(control(lifetimeprobe::Blocked),"Retirement starts with actual pending native fence");d.reset();release.join();
    reject(s->acquire(),gfx::ErrorCode::DeviceLost);reject(s->present(),gfx::ErrorCode::DeviceLost);reject(s->resize(65,7),gfx::ErrorCode::DeviceLost);reject(work.commands->begin(),gfx::ErrorCode::DeviceLost);reject(work.read->read(0,4),gfx::ErrorCode::DeviceLost);
    check(t->description().format==F::BGRA8UNorm&&t->footprint().height==7,"Retired CPU metadata remains accessible");auto n=native();check(n.chains==n.destroyedChains,"Device retirement releases native chains while wrappers survive");result("device-retirement");
}
}
int main(int argc,char** argv)try{
    bool validation=false;const char* proxy=nullptr;for(int i=1;i<argc;++i){if(std::string_view(argv[i])=="--validation")validation=true;else if(std::string_view(argv[i])=="--proxy"&&i+1<argc)proxy=argv[++i];else throw std::runtime_error("Unexpected lifetime argument");}
    check(proxy,"Exact proxy path");auto module=dlopen(proxy,RTLD_NOW|RTLD_LOCAL);check(module,"Pin own observer");control=reinterpret_cast<lifetimeprobe::Control>(dlsym(module,"gfxLifetimeControl"));query=reinterpret_cast<lifetimeprobe::Query>(dlsym(module,"gfxLifetimeSnapshot"));nativeQuery=reinterpret_cast<surfaceprobe::Query>(dlsym(module,"gfxSurfaceSnapshot"));check(control&&query&&nativeQuery,"Own observer exports");
    auto adapters=take(gfx::enumerateAdapters(gfx::Backend::Vulkan));check(adapters.size()==1&&adapters[0].softwareAdapter,"One actual software ICD required; no physical GPU gate");gfx::DeviceDesc desc;desc.backend=gfx::Backend::Vulkan;desc.adapter=adapters[0].id;desc.softwareAdapter=true;desc.validation=validation;
    auto d=take(gfx::createDevice(desc));check(d->caps().swapChain,"Actual XCB support");retained(*d);clean(*d);pendingDestruction(*d);clean(*d);presentCompletion(*d);clean(*d);failures(*d);clean(*d);foreign(*d,desc);clean(*d);d.reset();retirement(desc);
    auto s=snapshot();auto n=native();check(!s.orderErrors&&s.views==s.destroyedViews&&s.semaphores==s.destroyedSemaphores&&s.fences==s.destroyedFences&&s.gates==3&&s.blocked==2&&s.released==3&&s.cleaned==3&&s.faults==3,"Complete native lifetime coverage and closure");check(n.chains==n.destroyedChains&&n.surfaces==n.destroyedSurfaces&&n.devices==n.destroyedDevices&&n.instances==n.destroyedInstances,"Every native parent closed");
    std::cout<<"{\"event\":\"lifetime_result\",\"source\":"<<std::quoted(gfx::buildFingerprint())<<",\"validation\":"<<std::boolalpha<<validation<<",\"cases\":"<<cases<<",\"rejections\":"<<rejections<<",\"references\":"<<references<<",\"verified_bytes\":"<<verifiedBytes<<",\"gates\":"<<s.gates<<",\"blocked\":"<<s.blocked<<",\"released\":"<<s.released<<",\"cleaned\":"<<s.cleaned<<",\"faults\":"<<s.faults<<",\"order_errors\":"<<s.orderErrors<<",\"views\":"<<s.views<<",\"destroyed_views\":"<<s.destroyedViews<<",\"semaphores\":"<<s.semaphores<<",\"destroyed_semaphores\":"<<s.destroyedSemaphores<<",\"fences\":"<<s.fences<<",\"destroyed_fences\":"<<s.destroyedFences<<",\"chains\":"<<n.chains<<",\"destroyed_chains\":"<<n.destroyedChains<<",\"presents\":"<<n.presents<<",\"fenced_presents\":"<<n.fencedPresents<<",\"waited_presents\":"<<n.waitedPresents<<",\"instances\":"<<n.instances<<",\"destroyed_instances\":"<<n.destroyedInstances<<",\"devices\":"<<n.devices<<",\"destroyed_devices\":"<<n.destroyedDevices<<",\"surfaces\":"<<n.surfaces<<",\"destroyed_surfaces\":"<<n.destroyedSurfaces<<",\"validation_errors\":0,\"physical_gpu_accepted\":false,\"human_display_accepted\":false}"<<std::endl;dlclose(module);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
