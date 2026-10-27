#include "../BgraSupport.hpp"
#include "../linux_xcb_surface/Control.hpp"
#include "../../examples/xcb_presentation/XcbLoop.hpp"
#include <charconv>
#include <dlfcn.h>
#include <iomanip>
namespace {
using namespace bgraTest;
using Clock=presentation::FramePacer::Clock;
struct Options {std::uint64_t adapter{};bool software{},validation{};unsigned limit{};std::string session,proxy;};
std::uint64_t number(std::string_view text){std::uint64_t value{};auto r=std::from_chars(text.data(),text.data()+text.size(),value);check(r.ec==std::errc{}&&r.ptr==text.data()+text.size(),"Exact unsigned decimal argument required");return value;}
Options parse(int argc,char** argv){Options o;for(int i=1;i<argc;++i){std::string_view a=argv[i];if(a=="--software")o.software=true;else if(a=="--validation")o.validation=true;else if(a=="--adapter"&&i+1<argc&&!o.adapter)o.adapter=number(argv[++i]);else if(a=="--frames"&&i+1<argc&&!o.limit){auto n=number(argv[++i]);check(n>=2&&n<=120,"Bounded smoke frames 2..120");o.limit=unsigned(n);}else if(a=="--session"&&i+1<argc&&o.session.empty())o.session=argv[++i];else if(a=="--proxy"&&i+1<argc&&o.proxy.empty())o.proxy=argv[++i];else throw std::runtime_error("Unexpected manual consumer argument");}check(o.adapter&&o.session.size()==32&&o.session.find_first_not_of("0123456789abcdef")==std::string::npos&&!o.proxy.empty(),"Exact adapter, session and proxy required");return o;}
void state(const xcbPresentation::Session& app,unsigned resizes,unsigned pauses,unsigned restores){auto& p=app.policy;std::cout<<"{\"event\":\"window_state\",\"mapped\":"<<std::boolalpha<<p.mapped<<",\"minimized\":"<<p.minimized<<",\"closing\":"<<p.closing<<",\"destroyed\":"<<p.destroyed<<",\"width\":"<<p.width<<",\"height\":"<<p.height<<",\"frames\":"<<app.frames<<",\"resizes\":"<<resizes<<",\"pauses\":"<<pauses<<",\"restores\":"<<restores<<"}"<<std::endl;}
bool render(xcbPresentation::Session& app,gfx::PipelinePtr& pipeline,F& format,bool capture,unsigned& resizes){
    auto& d=app.device;auto& p=app.policy;if(!p.active())return false;p.pacer.started(Clock::now());
    if(!app.chain){auto r=d.createSwapChain(app.window.surface(),{p.width,p.height,2,true});if(!r)return app.handle(r.error());app.chain=std::move(*r);p.dirty=false;}
    else if(p.dirty){auto r=app.chain->resize(p.width,p.height);if(!r)return app.handle(r.error());p.dirty=false;++resizes;}
    auto acquired=app.chain->acquire();if(!acquired)return app.handle(acquired.error());auto t=*acquired;auto desc=t->description();auto f=t->footprint();
    if(!pipeline||format!=desc.format){pipeline=bgraTest::pipeline(d,desc.format,false);format=desc.format;}
    const unsigned rw=std::max(1u,desc.width/4),rh=std::max(1u,desc.height/3),x=(std::uint64_t(app.frames)*7)%(desc.width-rw+1),y=(desc.height-rh)/2;
    auto c=take(d.createCommandList());gfx::BufferPtr read,pad;take(c->begin());
    if(capture){read=take(d.createBuffer({f.size,gfx::Memory::Readback}));pad=take(d.createBuffer({f.size,gfx::Memory::Upload}));take(pad->write(0,std::vector<std::byte>(f.size,std::byte{0xb7})));take(c->copyBuffer(pad,read,f.size));}
    take(c->transition(t,S::RenderTarget));take(c->beginRenderPass(t,{.25f,.5f,.75f,1}));take(c->setPipeline(pipeline));take(c->setScissor(x,y,rw,rh));take(c->draw(3));take(c->endRenderPass());
    if(capture){take(c->transition(t,S::CopySource));take(c->copyTextureToBuffer(t,read));}
    take(c->transition(t,S::Present));take(c->end());auto fence=take(d.queue().submit(*c));auto presented=app.chain->present();take(d.queue().wait(fence));if(!presented)return app.handle(presented.error());
    if(capture){auto bytes=take(read->read(0,f.size));bool pixels=true,padding=true;for(unsigned row=0;row<desc.height;++row){for(unsigned col=0;col<desc.width;++col){std::array<float,4> color=col>=x&&col<x+rw&&row>=y&&row<y+rh?std::array<float,4>{1,0,0,1}:std::array<float,4>{.25f,.5f,.75f,1};for(unsigned k=0;k<4;++k){auto channel=desc.format==F::BGRA8UNorm&&k<3?2-k:k;pixels&=std::abs(std::to_integer<unsigned>(bytes[row*f.rowPitch+col*4+k])-color[channel]*255.f)<=1.1f;}}for(unsigned k=f.rowBytes;k<f.rowPitch&&std::uint64_t(row)*f.rowPitch+k<f.size;++k)padding&=bytes[row*f.rowPitch+k]==std::byte{0xb7};}check(pixels&&padding,"Every animated pixel and padding byte");++app.captures;}
    app.recovering=false;++app.frames;std::cout<<"{\"event\":\"frame\",\"ordinal\":"<<app.frames<<",\"width\":"<<desc.width<<",\"height\":"<<desc.height<<",\"x\":"<<x<<",\"y\":"<<y<<",\"rect_width\":"<<rw<<",\"rect_height\":"<<rh<<",\"captured\":"<<std::boolalpha<<capture<<"}"<<std::endl;return true;
}
}
int main(int argc,char** argv)try{
    auto o=parse(argc,argv);auto module=dlopen(o.proxy.c_str(),RTLD_NOW|RTLD_LOCAL);check(module,"Exact manual observer module");auto query=reinterpret_cast<surfaceprobe::Query>(dlsym(module,"gfxSurfaceSnapshot"));check(query,"Manual observer export");
    auto adapters=take(gfx::enumerateAdapters(gfx::Backend::Vulkan));auto selected=std::find_if(adapters.begin(),adapters.end(),[&](auto& a){return a.id.value==o.adapter;});check(selected!=adapters.end()&&selected->softwareAdapter==o.software,"Exact adapter/kind with no fallback");
    gfx::DeviceDesc desc;desc.backend=gfx::Backend::Vulkan;desc.adapter=selected->id;desc.softwareAdapter=o.software;desc.validation=o.validation;
    xcbPresentation::Window window(o.limit?129:640,o.limit?73:360);auto device=take(gfx::createDevice(desc));check(device->caps().adapterId==selected->id&&device->caps().swapChain,"Exact native XCB device");
    xcbPresentation::Session app(*device,window);gfx::PipelinePtr pipeline;F format{};unsigned resizes{},pauses{},restores{};bool wasActive=false;std::array<unsigned,6> last{~0u,0,0,0,0,0};
    std::cout<<"{\"event\":\"session\",\"session_id\":"<<std::quoted(o.session)<<",\"source\":"<<std::quoted(gfx::buildFingerprint())<<",\"adapter_id\":"<<std::quoted(std::to_string(o.adapter))<<",\"software\":"<<std::boolalpha<<o.software<<",\"validation\":"<<o.validation<<",\"automated\":"<<(o.limit!=0)<<",\"target_fps\":60,\"human_display_accepted\":false,\"physical_gpu_accepted\":false}"<<std::endl;
    auto title="GraphicsSDK XCB visual check ["+o.session.substr(0,8)+"]";xcb_change_property(window.connection,XCB_PROP_MODE_REPLACE,window.id,XCB_ATOM_WM_NAME,XCB_ATOM_STRING,8,title.size(),title.data());window.map();auto deadline=Clock::now()+std::chrono::seconds(300);std::string reason;
    while(true){app.pump();bool active=app.policy.active();if(wasActive&&!active&&!app.policy.closing)++pauses;if(!wasActive&&active&&app.frames)++restores;wasActive=active;
        std::array<unsigned,6> current{unsigned(app.policy.mapped),unsigned(app.policy.minimized),unsigned(app.policy.closing),unsigned(app.policy.destroyed),app.policy.width,app.policy.height};if(current!=last){state(app,resizes,pauses,restores);last=current;}
        if(app.policy.closing){reason=app.policy.destroyed?"window_or_connection_expired":"wm_delete";break;}if(o.limit&&app.frames>=o.limit){reason="frame_limit";break;}if(Clock::now()>=deadline){reason="deadline";break;}
        auto now=Clock::now();if(active&&app.policy.pacer.due(now)){if(render(app,pipeline,format,o.limit!=0,resizes)&&app.frames%30==0){auto title="GraphicsSDK XCB visual check ["+o.session.substr(0,8)+"] frame "+std::to_string(app.frames);xcb_change_property(window.connection,XCB_PROP_MODE_REPLACE,window.id,XCB_ATOM_WM_NAME,XCB_ATOM_STRING,8,title.size(),title.data());xcb_flush(window.connection);}}
        else {int timeout=app.policy.timeout(now);app.wait(timeout<0?500:std::min(timeout,500));}
    }
    app.stop();pipeline.reset();empty(*device);auto errors=take(device->validationErrorCount());device.reset();surfaceprobe::Snapshot n{};check(query(&n,sizeof(n))==1&&n.instances==n.destroyedInstances&&n.devices==n.destroyedDevices&&n.chains==n.destroyedChains&&n.surfaces==n.destroyedSurfaces&&n.presents==n.fencedPresents&&n.presents==n.waitedPresents,"Native teardown and present completion");
    std::cout<<"{\"event\":\"manual_result\",\"session_id\":"<<std::quoted(o.session)<<",\"frames\":"<<app.frames<<",\"captures\":"<<app.captures<<",\"resizes\":"<<resizes<<",\"pauses\":"<<pauses<<",\"restores\":"<<restores<<",\"waits\":"<<app.waits<<",\"recoveries\":"<<app.recoveries<<",\"close_reason\":"<<std::quoted(reason)<<",\"validation_errors\":"<<errors<<",\"presents\":"<<n.presents<<",\"waited_presents\":"<<n.waitedPresents<<",\"chains\":"<<n.chains<<",\"destroyed_chains\":"<<n.destroyedChains<<",\"surfaces\":"<<n.surfaces<<",\"destroyed_surfaces\":"<<n.destroyedSurfaces<<",\"instances\":"<<n.instances<<",\"destroyed_instances\":"<<n.destroyedInstances<<",\"devices\":"<<n.devices<<",\"destroyed_devices\":"<<n.destroyedDevices<<",\"window_alive\":"<<std::boolalpha<<window.exists()<<",\"human_display_accepted\":false,\"physical_gpu_accepted\":false}"<<std::endl;
    dlclose(module);return reason=="wm_delete"||reason=="frame_limit"?0:1;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
