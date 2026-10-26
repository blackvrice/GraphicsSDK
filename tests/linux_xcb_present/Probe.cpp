#include "../BgraSupport.hpp"
#include "../linux_xcb_surface/Control.hpp"
#include <xcb/xcb.h>
#include <dlfcn.h>
#include <cstdlib>
#include <cstring>
#include <iomanip>
namespace {
using namespace bgraTest;
struct Window {
    xcb_connection_t* connection{};xcb_window_t window{};
    Window(unsigned width,unsigned height){int screen{};connection=xcb_connect(nullptr,&screen);check(connection&&!xcb_connection_has_error(connection),"Actual XCB display required");auto it=xcb_setup_roots_iterator(xcb_get_setup(connection));while(screen-->0&&it.rem)xcb_screen_next(&it);check(it.rem!=0,"Native XCB screen");
        window=xcb_generate_id(connection);auto c=xcb_create_window_checked(connection,XCB_COPY_FROM_PARENT,window,it.data->root,0,0,width,height,0,XCB_WINDOW_CLASS_INPUT_OUTPUT,it.data->root_visual,0,nullptr);auto e=xcb_request_check(connection,c);bool ok=!e;std::free(e);check(ok,"Native XCB window");xcb_map_window(connection,window);xcb_flush(connection);
    }
    ~Window(){if(window)xcb_destroy_window(connection,window);if(connection)xcb_disconnect(connection);}
    gfx::Surface surface()const{return {gfx::SurfaceKind::Xcb,nullptr,connection,window};}
};
unsigned cases{},rejected{};
void scene(gfx::Device& d,unsigned buffers,bool vsync,unsigned mode,surfaceprobe::Query query){
    const auto width=vsync?17u:65u,height=vsync?9u:7u;Window w(width,height);take(d.checkSurfaceSupport(w.surface()));
    auto chain=take(d.createSwapChain(w.surface(),{width,height,buffers,vsync}));surfaceprobe::Snapshot s{};check(query(&s,sizeof(s))==1,"Native configuration snapshot");
    check(s.format==44&&s.minImages>=buffers&&s.actualImages>=s.minImages&&s.minImages>=s.modeMinImages&&(!s.modeMaxImages||s.minImages<=s.modeMaxImages),"Actual BGRA8 UNORM fallback and selected-mode image count");check(s.presentMode==(vsync?2u:mode==8?1u:0u),"Actual native FIFO/IMMEDIATE/MAILBOX selection");
    auto wrong=d.createSwapChain(w.surface(),{width+1,height,buffers,vsync});check(!wrong&&wrong.error().code==gfx::ErrorCode::InvalidArgument,"Native extent mismatch rejected");
    for(unsigned draw=0;draw<3;++draw){auto t=take(chain->acquire());check(t->description().width==width&&t->description().height==height&&t->description().format==F::BGRA8UNorm,"Actual native backbuffer description");frame(d,t,draw,chain.get());}
    chain.reset();empty(d);++cases;
    std::cout<<"{\"event\":\"chain\",\"requested_buffers\":"<<buffers<<",\"vsync\":"<<std::boolalpha<<vsync<<",\"width\":"<<width<<",\"height\":"<<height<<",\"native_format\":"<<s.format<<",\"native_mode\":"<<s.presentMode<<",\"native_min_images\":"<<s.minImages<<",\"native_images\":"<<s.actualImages<<",\"mode_min_images\":"<<s.modeMinImages<<",\"mode_max_images\":"<<s.modeMaxImages<<"}\n";
}
}
int main(int argc,char** argv)try{
    bool validation{};const char* proxy{};for(int i=1;i<argc;++i)if(!std::strcmp(argv[i],"--validation"))validation=true;else if(!std::strcmp(argv[i],"--proxy")&&i+1<argc)proxy=argv[++i];else throw std::runtime_error("Unexpected XCB present argument");
    check(proxy!=nullptr,"Exact observer required");auto module=dlopen(proxy,RTLD_NOW|RTLD_LOCAL);check(module!=nullptr,"Pin own observer");auto query=reinterpret_cast<surfaceprobe::Query>(dlsym(module,"gfxSurfaceSnapshot"));check(query!=nullptr,"Own observer export");auto control=std::getenv("GFX_SURFACE_CONTROL");unsigned mode=control?std::atoi(control):0;check(mode==0||mode==6||mode==7||mode==8,"Declared present control required");
    auto adapters=take(gfx::enumerateAdapters(gfx::Backend::Vulkan));check(!adapters.empty(),"Actual software adapter required");
    for(auto& a:adapters){check(a.softwareAdapter,"CI software scope explicit");gfx::DeviceDesc desc;desc.backend=gfx::Backend::Vulkan;desc.validation=validation;desc.softwareAdapter=true;desc.adapter=a.id;auto d=take(gfx::createDevice(desc));check(d->caps().swapChain,"XCB implementation and completion feature enabled");offscreen(*d);empty(*d);
        if(mode==6||mode==7){Window w(65,7);auto r=d->createSwapChain(w.surface(),{65,7,2,mode==6});check(!r&&r.error().code==gfx::ErrorCode::Unsupported,"No implicit SRGB conversion or FIFO fallback for vsync=false");++rejected;}
        if(mode==0)for(unsigned n:{2u,3u,4u})for(bool sync:{false,true})scene(*d,n,sync,mode,query);
        if(mode==7)scene(*d,2,true,mode,query);if(mode==8)scene(*d,2,false,mode,query);
        empty(*d);d.reset();std::cout<<"{\"event\":\"adapter\",\"id\":"<<a.id.value<<",\"software\":true,\"name\":"<<std::quoted(a.name)<<"}\n";
    }
    surfaceprobe::Snapshot s{};check(query(&s,sizeof(s))==1,"Final observer");check(s.instances==s.destroyedInstances&&s.devices==s.destroyedDevices&&s.surfaces==s.destroyedSurfaces&&s.chains==s.destroyedChains,"All native parents, surfaces and swapchains closed");check(s.acquires==cases*3&&s.presents==cases*3&&s.fencedPresents==s.presents&&s.waitedPresents==s.presents,"Actual acquire/present and completion waits");
    std::cout<<"{\"event\":\"present_result\",\"source\":"<<std::quoted(gfx::buildFingerprint())<<",\"validation\":"<<std::boolalpha<<validation<<",\"control\":"<<mode<<",\"adapters\":"<<adapters.size()<<",\"cases\":"<<cases<<",\"rejected\":"<<rejected<<",\"images\":"<<images<<",\"pixel_bytes\":"<<pixelBytes<<",\"transfers\":"<<transfers<<",\"transfer_bytes\":"<<transferBytes<<",\"padding_rows\":"<<paddedRows<<",\"acquires\":"<<s.acquires<<",\"presents\":"<<s.presents<<",\"fenced_presents\":"<<s.fencedPresents<<",\"waited_presents\":"<<s.waitedPresents<<",\"chains\":"<<s.chains<<",\"destroyed_chains\":"<<s.destroyedChains<<",\"surfaces\":"<<s.surfaces<<",\"destroyed_surfaces\":"<<s.destroyedSurfaces<<",\"instances\":"<<s.instances<<",\"destroyed_instances\":"<<s.destroyedInstances<<",\"devices\":"<<s.devices<<",\"destroyed_devices\":"<<s.destroyedDevices<<",\"validation_errors\":0,\"human_display_accepted\":false}\n";dlclose(module);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
