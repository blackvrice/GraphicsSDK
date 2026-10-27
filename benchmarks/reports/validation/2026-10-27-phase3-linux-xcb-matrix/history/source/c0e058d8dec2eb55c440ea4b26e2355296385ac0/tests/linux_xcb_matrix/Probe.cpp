#include "../BgraSupport.hpp"
#include "../linux_xcb_surface/Control.hpp"
#include "../linux_xcb_lifetime/Control.hpp"
#include "../../examples/xcb_presentation/XcbLoop.hpp"
#include <dlfcn.h>
#include <iomanip>
#include <cstring>
#include <thread>
namespace {
using namespace bgraTest;
surfaceprobe::Query query; lifetimeprobe::Query lifetime;
unsigned pairs{},rejections{};
auto native(){surfaceprobe::Snapshot s{};check(query(&s,sizeof(s))==1,"Native matrix observer");return s;}
auto resources(){lifetimeprobe::Snapshot s{};check(lifetime(&s,sizeof(s))==1,"Resource matrix observer");return s;}
template<class T>void reject(gfx::Result<T> r){check(!r&&r.error().code==gfx::ErrorCode::InvalidState,"Exact cross-window lifetime rejection");++rejections;}
void image(gfx::Device& d,gfx::SwapChain& c,unsigned window,unsigned draw){
    auto t=take(c.acquire());auto desc=t->description();frame(d,t,draw,&c);
    std::cout<<"{\"event\":\"image\",\"pair\":"<<pairs<<",\"window\":"<<window<<",\"width\":"<<desc.width<<",\"height\":"<<desc.height<<",\"draw\":"<<draw<<"}"<<std::endl;
}
void both(gfx::Device& d,gfx::SwapChain& a,gfx::SwapChain& b){
    for(unsigned round=0;round<3;++round){
        // Both surfaces have an acquired image at the same time on one device.
        auto x=take(a.acquire()),y=take(b.acquire());
        for(unsigned i=0;i<2;++i){auto& t=i?y:x;auto& c=i?b:a;auto desc=t->description();unsigned draw=i?2-round:round;frame(d,t,draw,&c);
            std::cout<<"{\"event\":\"image\",\"pair\":"<<pairs<<",\"window\":"<<i<<",\"width\":"<<desc.width<<",\"height\":"<<desc.height<<",\"draw\":"<<draw<<"}"<<std::endl;}
    }
}
void resize(xcbPresentation::Window& w,unsigned width,unsigned height){
    w.resize(width,height);auto end=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    while(w.geometry()!=std::pair{width,height}&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(2));
    check(w.geometry()==std::pair{width,height},"Actual independent XCB geometry change");
}
auto record(gfx::Device& d,const gfx::TexturePtr& t){auto c=take(d.createCommandList());take(c->begin());take(c->transition(t,S::Present));take(c->end());return c;}
auto create(gfx::Device& d,xcbPresentation::Window& w,unsigned buffers,bool vsync){
    auto [width,height]=w.geometry();auto c=take(d.createSwapChain(w.surface(),{width,height,buffers,vsync}));auto n=native();
    check(n.format==44&&n.presentMode==(vsync?2u:0u)&&n.actualImages>=n.minImages&&n.minImages>=buffers&&n.minImages>=n.modeMinImages&&(!n.modeMaxImages||n.minImages<=n.modeMaxImages),"Actual native BGRA mode/count constraints");
    std::cout<<"{\"event\":\"configuration\",\"pair\":"<<pairs<<",\"buffers\":"<<buffers<<",\"vsync\":"<<std::boolalpha<<vsync<<",\"mode\":"<<n.presentMode<<",\"format\":"<<n.format<<",\"images\":"<<n.actualImages<<",\"minimum\":"<<n.minImages<<",\"mode_minimum\":"<<n.modeMinImages<<",\"mode_maximum\":"<<n.modeMaxImages<<"}"<<std::endl;return c;
}
void scene(gfx::Device& d,unsigned buffers,bool vsync){
    xcbPresentation::Window wa(17,9),wb(65,7);wa.map();wb.map();
    auto a=create(d,wa,buffers,vsync),b=create(d,wb,buffers,!vsync);both(d,*a,*b);
    auto held=take(a->acquire());auto commands=record(d,held);resize(wa,83,11);reject(a->resize(83,11));image(d,*b,1,1);
    resize(wb,129,13);take(b->resize(129,13));image(d,*b,1,2);
    held.reset();reject(a->resize(83,11));commands.reset();take(a->resize(83,11));both(d,*a,*b);
    held=take(a->acquire());commands=record(d,held);a.reset();reject(d.queue().submit(*commands));take(commands->begin());reject(commands->transition(held,S::RenderTarget));
    image(d,*b,1,0);commands.reset();held.reset();a=create(d,wa,buffers,vsync);image(d,*a,0,2);a.reset();image(d,*b,1,1);b.reset();empty(d);
    auto n=native();auto l=resources();check(n.chains==n.destroyedChains&&n.surfaces==n.destroyedSurfaces&&l.views==l.destroyedViews&&l.semaphores==l.destroyedSemaphores&&l.fences==l.destroyedFences&&!l.orderErrors,"All pair resources closed; peer survived resize/retirement");
    std::cout<<"{\"event\":\"pair\",\"pair\":"<<pairs<<",\"buffers\":"<<buffers<<",\"first_vsync\":"<<vsync<<",\"images\":17,\"rejections\":4}"<<std::endl;++pairs;
}
}
int main(int argc,char** argv)try{
    bool validation=false;const char* proxy=nullptr;for(int i=1;i<argc;++i)if(!std::strcmp(argv[i],"--validation"))validation=true;else if(!std::strcmp(argv[i],"--proxy")&&i+1<argc)proxy=argv[++i];else throw std::runtime_error("Unexpected matrix argument");
    check(proxy,"Exact matrix observer required");auto module=dlopen(proxy,RTLD_NOW|RTLD_LOCAL);check(module,"Pin matrix observer");query=reinterpret_cast<surfaceprobe::Query>(dlsym(module,"gfxSurfaceSnapshot"));lifetime=reinterpret_cast<lifetimeprobe::Query>(dlsym(module,"gfxLifetimeSnapshot"));check(query&&lifetime,"Matrix observer exports");
    auto adapters=take(gfx::enumerateAdapters(gfx::Backend::Vulkan));check(adapters.size()==1&&adapters[0].softwareAdapter,"Single actual software ICD only");gfx::DeviceDesc desc;desc.backend=gfx::Backend::Vulkan;desc.adapter=adapters[0].id;desc.softwareAdapter=true;desc.validation=validation;auto d=take(gfx::createDevice(desc));check(d->caps().swapChain,"Native XCB completion support");
    for(unsigned buffers:{2u,3u,4u})for(bool vsync:{false,true})scene(*d,buffers,vsync);d.reset();auto n=native();auto l=resources();check(n.instances==n.destroyedInstances&&n.devices==n.destroyedDevices,"All native parents closed");
    std::cout<<"{\"event\":\"matrix_result\",\"source\":"<<std::quoted(gfx::buildFingerprint())<<",\"validation\":"<<std::boolalpha<<validation<<",\"pairs\":"<<pairs<<",\"images\":"<<images<<",\"pixel_bytes\":"<<pixelBytes<<",\"padding_rows\":"<<paddedRows<<",\"rejections\":"<<rejections<<",\"acquires\":"<<n.acquires<<",\"presents\":"<<n.presents<<",\"fenced_presents\":"<<n.fencedPresents<<",\"waited_presents\":"<<n.waitedPresents<<",\"chains\":"<<n.chains<<",\"destroyed_chains\":"<<n.destroyedChains<<",\"surfaces\":"<<n.surfaces<<",\"destroyed_surfaces\":"<<n.destroyedSurfaces<<",\"instances\":"<<n.instances<<",\"destroyed_instances\":"<<n.destroyedInstances<<",\"devices\":"<<n.devices<<",\"destroyed_devices\":"<<n.destroyedDevices<<",\"views\":"<<l.views<<",\"destroyed_views\":"<<l.destroyedViews<<",\"semaphores\":"<<l.semaphores<<",\"destroyed_semaphores\":"<<l.destroyedSemaphores<<",\"fences\":"<<l.fences<<",\"destroyed_fences\":"<<l.destroyedFences<<",\"order_errors\":"<<l.orderErrors<<",\"validation_errors\":0,\"physical_gpu_accepted\":false,\"human_display_accepted\":false}"<<std::endl;dlclose(module);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
