#include "../BgraSupport.hpp"
#include "../linux_xcb_surface/Control.hpp"
#include "../linux_xcb_lifetime/Control.hpp"
#include "Control.hpp"
#include "../../examples/xcb_presentation/XcbLoop.hpp"
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
struct Window : xcbPresentation::Window {Window():xcbPresentation::Window(129,73){}};
template<class T>void reject(gfx::Result<T> r,gfx::ErrorCode e){check(!r&&r.error().code==e,"Exact lifetime rejection");++rejections;}
auto snapshot(){lifetimeprobe::Snapshot s{};check(query(&s,sizeof(s))==1,"Lifetime snapshot ABI");return s;}
auto native(){surfaceprobe::Snapshot s{};check(nativeQuery(&s,sizeof(s))==1,"Native snapshot ABI");return s;}
auto chain(gfx::Device& d,Window& w){auto [width,height]=w.geometry();return take(d.createSwapChain(w.surface(),{width,height,2,true}));}
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
    for(unsigned y=0;y<f.height;++y)for(unsigned x=0;x<t->description().width;++x){auto offset=y*f.rowPitch+x*4;w.expected[offset]=std::byte{};w.expected[offset+1]=std::byte{};w.expected[offset+2]=std::byte{255};w.expected[offset+3]=std::byte{255};}
    w.read=take(d.createBuffer({f.size,gfx::Memory::Readback}));auto pad=take(d.createBuffer({f.size,gfx::Memory::Upload}));take(pad->write(0,std::vector<std::byte>(f.size,std::byte{0xb7})));
    w.commands=take(d.createCommandList());auto& c=*w.commands;take(c.begin());take(c.copyBuffer(pad,w.read,f.size));take(c.transition(t,S::RenderTarget));take(c.beginRenderPass(t,{1,0,0,1}));take(c.endRenderPass());take(c.transition(t,S::CopySource));take(c.copyTextureToBuffer(t,w.read));take(c.transition(t,S::Present));take(c.end());return w;
}
void verify(Work& w){check(take(w.read->read(0,w.expected.size()))==w.expected,"Every BGRA texel and padding byte after lifetime boundary");++references;verifiedBytes+=w.expected.size();}
void clean(gfx::Device& d){empty(d);auto s=snapshot();check(s.views==s.destroyedViews&&s.semaphores==s.destroyedSemaphores&&s.fences==s.destroyedFences&&!s.orderErrors,"All native views/semaphores/fences reclaimed in valid order");auto n=native();check(n.chains==n.destroyedChains&&n.surfaces==n.destroyedSurfaces,"Native chains and surfaces reclaimed");}
eventprobe::Control eventControlFn;eventprobe::Query eventQuery;
unsigned hidden{},iconic{},restored{},idlePolls{},recovered{},boundedFailures{};
auto eventSnapshot(){eventprobe::Snapshot s{};check(eventQuery(&s,sizeof(s))==1,"Event observer ABI");return s;}
void sync(Window& w){auto r=xcb_get_input_focus_reply(w.connection,xcb_get_input_focus(w.connection),nullptr);std::free(r);}
template<class P>void until(xcbPresentation::Session& app,P predicate){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(8);while(std::chrono::steady_clock::now()<end){app.pump();if(predicate())return;app.wait(50);}throw std::runtime_error("Native XCB/WM event deadline");}
void map(xcbPresentation::Session& app){app.window.map();until(app,[&]{return app.policy.active()&&app.window.managerState()==1;});check(app.window.managed(),"Actual window manager owns display");}
void imageRow(unsigned width,unsigned height,const char* kind){std::cout<<"{\"event\":\"image\",\"kind\":"<<std::quoted(kind)<<",\"width\":"<<width<<",\"height\":"<<height<<"}"<<std::endl;}
void reference(xcbPresentation::Session& app){
    check(app.lastReadback!=nullptr,"Captured app frame");auto desc=app.lastDescription;auto f=app.lastFootprint;check(desc.format==F::BGRA8UNorm,"Actual BGRA8 app frame");auto data=take(app.lastReadback->read(0,f.size));bool ok=true;
    for(unsigned y=0;y<desc.height;++y){for(unsigned x=0;x<desc.width;++x)for(unsigned k=0;k<4;++k){std::array<float,4> color{.75f,.5f,.25f,1};ok&=std::abs(std::to_integer<unsigned>(data[y*f.rowPitch+x*4+k])-color[k]*255.f)<=1.1f;}for(unsigned b=f.rowBytes;b<f.rowPitch;++b)ok&=data[y*f.rowPitch+b]==std::byte{0xb7};}
    check(ok,"Every app texel and padding byte after event transition");++references;pixelBytes+=std::uint64_t(f.rowBytes)*f.height;paddedRows+=f.height;imageRow(desc.width,desc.height,"clear");
}
void frame(xcbPresentation::Session& app){check(app.frame(true),"Healthy app frame");reference(app);}
void result(const char* name){++cases;std::cout<<"{\"event\":\"case\",\"name\":"<<std::quoted(name)<<",\"references\":"<<references<<",\"rejections\":"<<rejections<<"}"<<std::endl;}
void unchanged(const surfaceprobe::Snapshot& a,const lifetimeprobe::Snapshot& b){check(native().acquires==a.acquires&&native().presents==a.presents&&snapshot().submits==b.submits,"No submission/acquire/present while inactive");}
void hideRestore(gfx::Device& d,bool minimize){
    Window w;xcbPresentation::Session app(d,w);map(app);frame(app);if(minimize)w.minimize();else w.hide();until(app,[&]{return !app.policy.mapped&&(!minimize||(app.policy.minimized&&w.managerState()==3));});
    if(minimize){check(app.policy.minimized,"Actual WM_STATE IconicState");++iconic;}else ++hidden;
    auto n=native();auto life=snapshot();for(unsigned i=0;i<8;++i)check(!app.frame(),"Inactive app frame suppressed");unchanged(n,life);check(app.policy.timeout(presentation::FramePacer::Clock::now())==-1,"Inactive loop requests blocking event wait");
    std::jthread wake([id=w.id]{auto c=xcb_connect(nullptr,nullptr);std::this_thread::sleep_for(std::chrono::milliseconds(30));xcb_map_window(c,id);xcb_flush(c);auto r=xcb_get_input_focus_reply(c,xcb_get_input_focus(c),nullptr);std::free(r);xcb_disconnect(c);});
    app.wait(-1);++idlePolls;until(app,[&]{return app.policy.active()&&w.managerState()==1;});wake.join();++restored;frame(app);result(minimize?"wm-minimize-restore":"hide-restore");
}
void closeProtocol(gfx::Device& d){Window w;xcbPresentation::Session app(d,w);map(app);frame(app);auto n=native();auto life=snapshot();check(eventControlFn(eventprobe::ClosedBoundary),"Close boundary");w.requestClose();sync(w);until(app,[&]{return app.policy.closing;});for(unsigned i=0;i<8;++i)check(!app.frame(),"Close suppresses new work");unchanged(n,life);check(w.exists(),"WM_DELETE processed before window destruction");check(native().chains==native().destroyedChains,"Chain drained before caller destroys window");check(eventControlFn(eventprobe::ClearBoundary),"Clear close boundary");result("wm-delete-protocol");}
template<class T>void lost(gfx::Result<T> r){check(!r&&r.error().code==gfx::ErrorCode::InvalidState&&r.error().nativeCode==-1000000000,"Stable native surface-lost error");++rejections;}
void deadWindow(gfx::Device& d){
    Window w;xcbPresentation::Session app(d,w);map(app);frame(app);auto t=take(app.chain->acquire());auto c=take(d.createCommandList());take(c->begin());take(c->transition(t,S::Present));take(c->end());w.destroy();sync(w);check(!w.exists(),"Actual server-side destruction");check(eventControlFn(eventprobe::ClosedBoundary),"Destroyed-window boundary");
    auto n=native();auto l=snapshot();reject(d.queue().submit(*c),gfx::ErrorCode::InvalidState);take(c->begin());reject(c->transition(t,S::RenderTarget),gfx::ErrorCode::InvalidState);reject(app.chain->acquire(),gfx::ErrorCode::InvalidState);reject(app.chain->present(),gfx::ErrorCode::InvalidState);reject(app.chain->resize(129,73),gfx::ErrorCode::InvalidState);unchanged(n,l);check(t->description().width==129,"Expired CPU metadata survives");
    // Reusing a numeric XID must not revive a surface already observed expired.
    auto screen=xcb_setup_roots_iterator(xcb_get_setup(w.connection));auto cookie=xcb_create_window_checked(w.connection,XCB_COPY_FROM_PARENT,w.id,w.root,0,0,129,73,0,XCB_WINDOW_CLASS_INPUT_OUTPUT,screen.data->root_visual,0,nullptr);auto error=xcb_request_check(w.connection,cookie);check(!error,"Recreate actual window with the same numeric XID");std::free(error);w.live=true;check(w.exists(),"Reused XID is a different live window");reject(app.chain->acquire(),gfx::ErrorCode::InvalidState);unchanged(n,l);c.reset();t.reset();app.stop();check(eventControlFn(eventprobe::ClearBoundary),"Clear expired boundary");
    w.map();{auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);while(w.managerState()!=1&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(5));check(w.managerState()==1,"Reused window mapped by actual WM");}auto fresh=chain(d,w);auto image=take(fresh->acquire());bgraTest::frame(d,image,0,fresh.get());++references;imageRow(129,73,"clear");result("destroyed-window-and-xid-reuse");
}
void connectionLoss(gfx::Device& d){
    Window w;xcbPresentation::Session app(d,w);map(app);frame(app);auto t=take(app.chain->acquire());auto c=take(d.createCommandList());take(c->begin());take(c->transition(t,S::Present));take(c->end());auto other=xcb_connect(nullptr,nullptr);xcb_kill_client(other,w.id);xcb_flush(other);auto reply=xcb_get_input_focus_reply(other,xcb_get_input_focus(other),nullptr);std::free(reply);xcb_disconnect(other);sync(w);check(xcb_connection_has_error(w.connection)!=0,"Actual server-disconnected client, allocation remains alive");w.live=false;check(eventControlFn(eventprobe::ClosedBoundary),"Connection-loss boundary");
    auto n=native();auto l=snapshot();reject(d.queue().submit(*c),gfx::ErrorCode::InvalidState);take(c->begin());reject(c->transition(t,S::Present),gfx::ErrorCode::InvalidState);reject(app.chain->acquire(),gfx::ErrorCode::InvalidState);reject(app.chain->present(),gfx::ErrorCode::InvalidState);reject(app.chain->resize(129,73),gfx::ErrorCode::InvalidState);app.pump();check(app.policy.closing&&!app.frame(),"Connection error exits event loop");unchanged(n,l);c.reset();t.reset();check(eventControlFn(eventprobe::ClearBoundary),"Clear connection boundary");result("connection-disconnect");
}
void recover(gfx::Device& d,unsigned op){
    Window w;xcbPresentation::Session app(d,w);map(app);frame(app);auto before=native();
    if(op==eventprobe::QueryLost){w.resize(147,79);until(app,[&]{return app.policy.width==147&&app.policy.height==79;});}
    check(eventControlFn(op),"Surface-loss status control");check(!app.frame(true)&&!app.chain&&!app.policy.closing,"Lost surface drops old chain for bounded recreation");frame(app);check(app.recoveries==1&&native().surfaces==before.surfaces+1,"Fresh native surface created before recovery frame");++recovered;result(op==eventprobe::AcquireLost?"acquire-surface-loss":op==eventprobe::PresentLost?"present-surface-loss":"query-surface-loss");
}
void stickyLoss(gfx::Device& d){
    Window w;xcbPresentation::Session app(d,w);map(app);frame(app);auto t=take(app.chain->acquire());auto held=take(d.createCommandList());take(held->begin());take(held->transition(t,S::Present));take(held->end());take(app.chain->present());check(eventControlFn(eventprobe::AcquireLost),"Sticky surface loss");lost(app.chain->acquire());auto n=native();auto l=snapshot();auto q=eventSnapshot().queries;lost(app.chain->acquire());lost(app.chain->present());lost(app.chain->resize(129,73));reject(d.queue().submit(*held),gfx::ErrorCode::InvalidState);take(held->begin());reject(held->transition(t,S::Present),gfx::ErrorCode::InvalidState);unchanged(n,l);check(eventSnapshot().queries==q,"Lost surface is not queried/reused by resize");check(t->footprint().height==73,"Lost-surface metadata survives");held.reset();t.reset();app.stop();app.policy.dirty=true;frame(app);result("sticky-loss-and-new-surface");
}
void boundedLoss(gfx::Device& d){
    Window w;xcbPresentation::Session app(d,w);map(app);frame(app);check(eventControlFn(eventprobe::AcquireLost),"First bounded loss");check(!app.frame(),"First loss requests replacement");check(eventControlFn(eventprobe::AcquireLost),"Replacement surface loss");bool rejected=false;try{(void)app.frame();}catch(const std::runtime_error&){rejected=true;}check(rejected&&app.recoveries==1,"Repeated replacement loss exits instead of retry spin");app.stop();++boundedFailures;result("bounded-replacement-failure");
}
void pendingClose(gfx::Device& d){
    Window w;xcbPresentation::Session app(d,w);map(app);frame(app);auto t=take(app.chain->acquire());auto desc=t->description();auto fp=t->footprint();auto work=record(d,t);check(control(lifetimeprobe::Arm),"Arm actual pending close");ReleaseAtBoundary release(lifetimeprobe::IdleEntered);auto f=take(d.queue().submit(*work.commands));check(control(lifetimeprobe::Blocked),"Actual close submission NOT_READY");work.commands.reset();t.reset();w.requestClose();sync(w);until(app,[&]{return app.policy.closing;});release.join();take(d.queue().wait(f));verify(work);pixelBytes+=std::uint64_t(fp.rowBytes)*fp.height;paddedRows+=fp.height;imageRow(desc.width,desc.height,"red-pending");work.read.reset();check(control(lifetimeprobe::Cleanup),"Pending-close test gate reclaimed");check(native().chains==native().destroyedChains&&w.exists(),"GPU drain before native window teardown");result("close-with-pending-submission");
}
}
int main(int argc,char** argv)try{
    bool validation=false;const char* proxy=nullptr;for(int i=1;i<argc;++i){if(std::string_view(argv[i])=="--validation")validation=true;else if(std::string_view(argv[i])=="--proxy"&&i+1<argc)proxy=argv[++i];else throw std::runtime_error("Unexpected lifecycle argument");}
    check(proxy,"Exact observer path");auto module=dlopen(proxy,RTLD_NOW|RTLD_LOCAL);check(module,"Pin observer");control=reinterpret_cast<lifetimeprobe::Control>(dlsym(module,"gfxLifetimeControl"));query=reinterpret_cast<lifetimeprobe::Query>(dlsym(module,"gfxLifetimeSnapshot"));nativeQuery=reinterpret_cast<surfaceprobe::Query>(dlsym(module,"gfxSurfaceSnapshot"));eventControlFn=reinterpret_cast<eventprobe::Control>(dlsym(module,"gfxEventControl"));eventQuery=reinterpret_cast<eventprobe::Query>(dlsym(module,"gfxEventSnapshot"));check(control&&query&&nativeQuery&&eventControlFn&&eventQuery,"Observer exports");
    {Window w;auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);while(!w.managed()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(5));check(w.managed(),"Actual Openbox-compatible window manager required");}
    auto adapters=take(gfx::enumerateAdapters(gfx::Backend::Vulkan));check(adapters.size()==1&&adapters[0].softwareAdapter,"Actual single software ICD only");gfx::DeviceDesc desc;desc.backend=gfx::Backend::Vulkan;desc.adapter=adapters[0].id;desc.softwareAdapter=true;desc.validation=validation;auto d=take(gfx::createDevice(desc));check(d->caps().swapChain,"Actual XCB swapchain");
    hideRestore(*d,false);clean(*d);hideRestore(*d,true);clean(*d);closeProtocol(*d);clean(*d);deadWindow(*d);clean(*d);connectionLoss(*d);clean(*d);recover(*d,eventprobe::AcquireLost);clean(*d);recover(*d,eventprobe::PresentLost);clean(*d);recover(*d,eventprobe::QueryLost);clean(*d);stickyLoss(*d);clean(*d);boundedLoss(*d);clean(*d);pendingClose(*d);clean(*d);d.reset();auto e=eventSnapshot();auto life=snapshot();auto n=native();check(!e.afterClose&&!life.orderErrors&&n.instances==n.destroyedInstances&&n.devices==n.destroyedDevices,"All native state closed without post-close GPU work");
    std::cout<<"{\"event\":\"events_result\",\"source\":"<<std::quoted(gfx::buildFingerprint())<<",\"validation\":"<<std::boolalpha<<validation<<",\"cases\":"<<cases<<",\"references\":"<<references<<",\"pixel_bytes\":"<<pixelBytes<<",\"padding_rows\":"<<paddedRows<<",\"work_readback_bytes\":"<<verifiedBytes<<",\"rejections\":"<<rejections<<",\"hidden\":"<<hidden<<",\"iconic\":"<<iconic<<",\"restored\":"<<restored<<",\"idle_polls\":"<<idlePolls<<",\"recovered\":"<<recovered<<",\"bounded_failures\":"<<boundedFailures<<",\"acquire_lost\":"<<e.acquireLost<<",\"present_lost\":"<<e.presentLost<<",\"query_lost\":"<<e.queryLost<<",\"after_close\":"<<e.afterClose<<",\"gates\":"<<life.gates<<",\"blocked\":"<<life.blocked<<",\"released\":"<<life.released<<",\"cleaned\":"<<life.cleaned<<",\"order_errors\":"<<life.orderErrors<<",\"chains\":"<<n.chains<<",\"destroyed_chains\":"<<n.destroyedChains<<",\"surfaces\":"<<n.surfaces<<",\"destroyed_surfaces\":"<<n.destroyedSurfaces<<",\"instances\":"<<n.instances<<",\"destroyed_instances\":"<<n.destroyedInstances<<",\"devices\":"<<n.devices<<",\"destroyed_devices\":"<<n.destroyedDevices<<",\"presents\":"<<n.presents<<",\"fenced_presents\":"<<n.fencedPresents<<",\"waited_presents\":"<<n.waitedPresents<<",\"views\":"<<life.views<<",\"destroyed_views\":"<<life.destroyedViews<<",\"semaphores\":"<<life.semaphores<<",\"destroyed_semaphores\":"<<life.destroyedSemaphores<<",\"fences\":"<<life.fences<<",\"destroyed_fences\":"<<life.destroyedFences<<",\"validation_errors\":0,\"physical_gpu_accepted\":false,\"human_display_accepted\":false}"<<std::endl;dlclose(module);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
