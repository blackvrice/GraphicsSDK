#include "../BgraSupport.hpp"
#include "../linux_xcb_surface/Control.hpp"
#include "../linux_xcb_lifetime/Control.hpp"
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
resizeprobe::Control resizeControlFn;resizeprobe::Query resizeQuery;
unsigned resizes{};
auto resizeSnapshot(){resizeprobe::Snapshot s{};check(resizeQuery(&s,sizeof(s))==1,"Resize observer ABI");return s;}
void size(Window& w,unsigned width,unsigned height){uint32_t values[]{width,height};auto cookie=xcb_configure_window_checked(w.connection,w.id,XCB_CONFIG_WINDOW_WIDTH|XCB_CONFIG_WINDOW_HEIGHT,values);auto error=xcb_request_check(w.connection,cookie);bool ok=!error;std::free(error);check(ok,"Actual native window resize");xcb_flush(w.connection);auto reply=xcb_get_geometry_reply(w.connection,xcb_get_geometry(w.connection,w.id),nullptr);check(reply&&reply->width==width&&reply->height==height,"Native geometry agrees");std::free(reply);}
void resize(gfx::SwapChain& s,unsigned width,unsigned height){take(s.resize(width,height));++resizes;}
void frames(gfx::Device& d,gfx::SwapChain& s,unsigned width,unsigned height,unsigned count=3){
    for(unsigned draw=0;draw<count;++draw){auto t=take(s.acquire());check(t->description().width==width&&t->description().height==height&&t->description().format==F::BGRA8UNorm,"Actual recreated extent/format");frame(d,t,draw,&s);}
}
void caseResult(const char* name){++cases;std::cout<<"{\"event\":\"case\",\"name\":"<<std::quoted(name)<<",\"images\":"<<images<<",\"references\":"<<references<<",\"rejections\":"<<rejections<<",\"resizes\":"<<resizes<<"}"<<std::endl;}
void geometry(gfx::Device& d){
    Window w;auto s=chain(d,w);frames(d,*s,65,7,1);auto before=resizeSnapshot();
    reject(s->resize(83,11),gfx::ErrorCode::InvalidArgument);frames(d,*s,65,7,1);reject(s->resize(0,0),gfx::ErrorCode::InvalidArgument);frames(d,*s,65,7,1);
    check(resizeSnapshot().creates==before.creates,"Pre-call extent errors preserve native chain");
    auto t=take(s->acquire());auto held=take(d.createCommandList());take(held->begin());take(held->transition(t,S::Present));take(held->end());size(w,83,11);
    reject(s->resize(83,11),gfx::ErrorCode::InvalidState);t.reset();reject(s->resize(83,11),gfx::ErrorCode::InvalidState);check(resizeSnapshot().creates==before.creates,"Caller-owned references block native recreation");held.reset();resize(*s,83,11);frames(d,*s,83,11);
    for(auto [width,height]:std::array<std::pair<unsigned,unsigned>,4>{{{17,9},{1,1},{129,13},{65,7}}}){size(w,width,height);resize(*s,width,height);frames(d,*s,width,height,width==1?1:3);}
    caseResult("geometry-and-retained-prechecks");
}
void pendingResize(gfx::Device& d){
    Window w;auto s=chain(d,w);auto t=take(s->acquire());auto work=record(d,t);check(control(lifetimeprobe::Arm),"Arm actual pending resize event");ReleaseAtBoundary release(lifetimeprobe::IdleEntered);
    auto f=take(d.queue().submit(*work.commands));check(control(lifetimeprobe::Blocked),"Actual resize submission NOT_READY");work.commands.reset();t.reset();size(w,83,11);
    resize(*s,83,11);release.join();take(d.queue().wait(f));verify(work);check(control(lifetimeprobe::Cleanup),"Pending resize gate cleanup");work.read.reset();frames(d,*s,83,11);caseResult("pending-submission-resize");
}
void acquireStatus(gfx::Device& d,bool suboptimal){
    Window w;auto s=chain(d,w);
    if(!suboptimal){frames(d,*s,65,7,1);check(resizeControlFn(resizeprobe::AcquireOutOfDate),"Acquire out-of-date control");reject(s->acquire(),gfx::ErrorCode::InvalidState);auto before=native();reject(s->acquire(),gfx::ErrorCode::InvalidState);reject(s->present(),gfx::ErrorCode::InvalidState);check(native().acquires==before.acquires&&native().presents==before.presents,"Invalid chain blocks native work");}
    else {check(resizeControlFn(resizeprobe::AcquireSuboptimal),"Acquire suboptimal control");frames(d,*s,65,7,1);}
    size(w,83,11);resize(*s,83,11);frames(d,*s,83,11);caseResult(suboptimal?"acquire-suboptimal":"acquire-out-of-date");
}
void presentStatus(gfx::Device& d,bool suboptimal){
    Window w;auto s=chain(d,w);auto t=take(s->acquire());auto work=record(d,t);auto f=take(d.queue().submit(*work.commands));work.commands.reset();auto stale=take(d.createCommandList());take(stale->begin());take(stale->transition(t,S::Present));take(stale->end());
    check(resizeControlFn(suboptimal?resizeprobe::PresentSuboptimal:resizeprobe::PresentOutOfDate),"Present status control");auto before=native();
    if(suboptimal)take(s->present());else {reject(s->present(),gfx::ErrorCode::InvalidState);auto count=native();reject(s->acquire(),gfx::ErrorCode::InvalidState);reject(s->present(),gfx::ErrorCode::InvalidState);check(native().presents==count.presents,"Out-of-date retry blocks native present");}
    auto after=native();check(after.waitedPresents==before.waitedPresents+1,"Actual completion fence consumed despite delivered status");take(d.queue().wait(f));verify(work);reject(d.queue().submit(*stale),gfx::ErrorCode::InvalidState);t.reset();stale.reset();work.read.reset();
    size(w,83,11);resize(*s,83,11);frames(d,*s,83,11);caseResult(suboptimal?"present-suboptimal":"present-out-of-date");
}
void failedRecreations(gfx::Device& d){
    Window w;auto s=chain(d,w);frames(d,*s,65,7,1);unsigned n{};
    for(unsigned op:{resizeprobe::FailAllocation,resizeprobe::FailEnumeration,resizeprobe::FailSecondView}){
        const unsigned width=n==0?83:n==1?17:65,height=n==0?11:n==1?9:7;size(w,width,height);auto before=resizeSnapshot();check(resizeControlFn(op),"Native recreation failure control");reject(s->resize(width,height),gfx::ErrorCode::OutOfMemory);
        check(resizeSnapshot().oldAttempts==before.oldAttempts+1,"Failed native attempt carries live oldSwapchain");auto nativeBefore=native();reject(s->acquire(),gfx::ErrorCode::InvalidState);reject(s->present(),gfx::ErrorCode::InvalidState);reject(s->resize(0,0),gfx::ErrorCode::InvalidArgument);
        check(native().acquires==nativeBefore.acquires&&native().presents==nativeBefore.presents,"Retired old/new images never forwarded after failure");auto attempts=resizeSnapshot();resize(*s,width,height);check(resizeSnapshot().freshAttempts==attempts.freshAttempts+1,"Retry uses NULL oldSwapchain after failed recreation");frames(d,*s,width,height);++n;
    }
    caseResult("failed-recreation-retirement-and-retry");
}
}
int main(int argc,char** argv)try{
    bool validation=false;const char* proxy=nullptr;for(int i=1;i<argc;++i){if(std::string_view(argv[i])=="--validation")validation=true;else if(std::string_view(argv[i])=="--proxy"&&i+1<argc)proxy=argv[++i];else throw std::runtime_error("Unexpected resize argument");}
    check(proxy,"Exact observer path");auto module=dlopen(proxy,RTLD_NOW|RTLD_LOCAL);check(module,"Pin own observer");control=reinterpret_cast<lifetimeprobe::Control>(dlsym(module,"gfxLifetimeControl"));query=reinterpret_cast<lifetimeprobe::Query>(dlsym(module,"gfxLifetimeSnapshot"));nativeQuery=reinterpret_cast<surfaceprobe::Query>(dlsym(module,"gfxSurfaceSnapshot"));resizeControlFn=reinterpret_cast<resizeprobe::Control>(dlsym(module,"gfxResizeControl"));resizeQuery=reinterpret_cast<resizeprobe::Query>(dlsym(module,"gfxResizeSnapshot"));check(control&&query&&nativeQuery&&resizeControlFn&&resizeQuery,"Observer exports");
    auto adapters=take(gfx::enumerateAdapters(gfx::Backend::Vulkan));check(adapters.size()==1&&adapters[0].softwareAdapter,"One actual software ICD required; gate never uses a physical GPU");gfx::DeviceDesc desc;desc.backend=gfx::Backend::Vulkan;desc.adapter=adapters[0].id;desc.softwareAdapter=true;desc.validation=validation;auto d=take(gfx::createDevice(desc));check(d->caps().swapChain,"Actual XCB swapchain");
    geometry(*d);clean(*d);pendingResize(*d);clean(*d);acquireStatus(*d,false);clean(*d);acquireStatus(*d,true);clean(*d);presentStatus(*d,false);clean(*d);presentStatus(*d,true);clean(*d);failedRecreations(*d);clean(*d);d.reset();auto s=resizeSnapshot();auto life=snapshot();auto n=native();
    check(!s.orderErrors&&!life.orderErrors&&s.creates==23&&s.oldAttempts==13&&s.freshAttempts==10&&s.nativeFailures==1&&s.allocations>0&&s.enumFailures==1&&life.faults==1,"Actual failed/native recreation and retirement coverage");check(s.acquireOutOfDate==1&&s.acquireSuboptimal==1&&s.presentOutOfDate==1&&s.presentSuboptimal==1,"All delivered status controls");check(n.chains==n.destroyedChains&&n.surfaces==n.destroyedSurfaces&&n.devices==n.destroyedDevices&&n.instances==n.destroyedInstances,"Native parents closed");
    std::cout<<"{\"event\":\"resize_result\",\"source\":"<<std::quoted(gfx::buildFingerprint())<<",\"validation\":"<<std::boolalpha<<validation<<",\"cases\":"<<cases<<",\"resizes\":"<<resizes<<",\"rejections\":"<<rejections<<",\"images\":"<<images<<",\"references\":"<<references<<",\"pixel_bytes\":"<<pixelBytes+references*1820ull<<",\"work_readback_bytes\":"<<verifiedBytes<<",\"padding_rows\":"<<paddedRows<<",\"creates\":"<<s.creates<<",\"old_attempts\":"<<s.oldAttempts<<",\"fresh_attempts\":"<<s.freshAttempts<<",\"native_failures\":"<<s.nativeFailures<<",\"allocation_callbacks\":"<<s.allocations<<",\"enum_failures\":"<<s.enumFailures<<",\"view_failures\":"<<life.faults<<",\"acquire_out_of_date\":"<<s.acquireOutOfDate<<",\"acquire_suboptimal\":"<<s.acquireSuboptimal<<",\"present_out_of_date\":"<<s.presentOutOfDate<<",\"present_suboptimal\":"<<s.presentSuboptimal<<",\"gates\":"<<life.gates<<",\"blocked\":"<<life.blocked<<",\"released\":"<<life.released<<",\"cleaned\":"<<life.cleaned<<",\"order_errors\":"<<s.orderErrors+life.orderErrors<<",\"chains\":"<<n.chains<<",\"destroyed_chains\":"<<n.destroyedChains<<",\"presents\":"<<n.presents<<",\"fenced_presents\":"<<n.fencedPresents<<",\"waited_presents\":"<<n.waitedPresents<<",\"views\":"<<life.views<<",\"destroyed_views\":"<<life.destroyedViews<<",\"semaphores\":"<<life.semaphores<<",\"destroyed_semaphores\":"<<life.destroyedSemaphores<<",\"fences\":"<<life.fences<<",\"destroyed_fences\":"<<life.destroyedFences<<",\"instances\":"<<n.instances<<",\"destroyed_instances\":"<<n.destroyedInstances<<",\"devices\":"<<n.devices<<",\"destroyed_devices\":"<<n.destroyedDevices<<",\"surfaces\":"<<n.surfaces<<",\"destroyed_surfaces\":"<<n.destroyedSurfaces<<",\"validation_errors\":0,\"physical_gpu_accepted\":false,\"human_display_accepted\":false}"<<std::endl;dlclose(module);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
