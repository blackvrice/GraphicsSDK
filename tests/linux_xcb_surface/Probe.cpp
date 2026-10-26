#include <graphics/Graphics.hpp>
#include <xcb/xcb.h>
#include "Control.hpp"
#include <dlfcn.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <thread>
namespace {
unsigned assertions{},checked{},bytes{};
constexpr std::int64_t VK_SURFACE_LOST_VALUE=-1000000000;
void check(bool b,const char* message){++assertions;if(!b)throw std::runtime_error(message);}
template<class T>T take(gfx::Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(*r);}
void take(gfx::Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
template<class T>void reject(gfx::Result<T> r,gfx::ErrorCode code){check(!r&&r.error().code==code,"Expected public surface error");}
struct Window {
    xcb_connection_t* connection{};xcb_window_t window{};xcb_screen_t* screen{};
    Window(bool headless){int number{};connection=xcb_connect(nullptr,&number);check(connection!=nullptr,"XCB connection allocation");if(headless)return;
        check(!xcb_connection_has_error(connection),"Actual X server required");auto it=xcb_setup_roots_iterator(xcb_get_setup(connection));for(int i=0;i<number&&it.rem;++i)xcb_screen_next(&it);check(it.rem!=0,"Screen exists");screen=it.data;
        window=xcb_generate_id(connection);auto cookie=xcb_create_window_checked(connection,XCB_COPY_FROM_PARENT,window,screen->root,0,0,65,49,0,XCB_WINDOW_CLASS_INPUT_OUTPUT,screen->root_visual,0,nullptr);auto error=xcb_request_check(connection,cookie);check(!error,"Create native window");std::free(error);xcb_map_window(connection,window);xcb_flush(connection);
    }
    ~Window(){if(window)xcb_destroy_window(connection,window);if(connection)xcb_disconnect(connection);}
    gfx::Surface surface()const{return {gfx::SurfaceKind::Xcb,nullptr,connection,window?window:1};}
};
void copy(gfx::Device& d){
    auto upload=take(d.createBuffer({257,gfx::Memory::Upload}));auto read=take(d.createBuffer({257,gfx::Memory::Readback}));std::array<std::byte,257> pattern{};for(unsigned i=0;i<pattern.size();++i)pattern[i]=std::byte(i%251);take(upload->write(0,pattern));
    auto c=take(d.createCommandList());take(c->begin());take(c->copyBuffer(upload,read,pattern.size()));take(c->end());take(d.queue().wait(take(d.queue().submit(*c))));auto actual=take(read->read(0,pattern.size()));check(std::equal(actual.begin(),actual.end(),pattern.begin(),pattern.end()),"Offscreen full buffer reference");bytes+=pattern.size();
}
}
int main(int argc,char** argv)try{
    bool disabled{},headless{};const char* proxy{};
    for(int i=1;i<argc;++i)if(!std::strcmp(argv[i],"--disabled"))disabled=true;else if(!std::strcmp(argv[i],"--headless"))headless=true;else if(!std::strcmp(argv[i],"--proxy")&&i+1<argc)proxy=argv[++i];else throw std::runtime_error("Unknown probe argument");
    check(proxy!=nullptr,"Exact proxy path required");auto module=dlopen(proxy,RTLD_NOW|RTLD_LOCAL);check(module!=nullptr,"Pin own proxy");auto query=reinterpret_cast<surfaceprobe::Query>(dlsym(module,"gfxSurfaceSnapshot"));check(query!=nullptr,"Exact observer export");
    auto snapshot=[&]{surfaceprobe::Snapshot s{};check(query(&s,sizeof(s))==1,"Observer snapshot");return s;};
    auto m=std::getenv("GFX_SURFACE_CONTROL");const auto mode=m?std::atoi(m):0;Window window(headless);
    const auto adapters=take(gfx::enumerateAdapters(gfx::Backend::Vulkan));check(!adapters.empty(),"Actual adapters required");
    for(auto& adapter:adapters){
        gfx::DeviceDesc desc;desc.backend=gfx::Backend::Vulkan;desc.adapter=adapter.id;desc.softwareAdapter=adapter.softwareAdapter;
        auto device=take(gfx::createDevice(desc));check(device->caps().swapChain==(!disabled&&mode!=1&&mode!=4),"Linux swapchain capability follows compiled path and runtime support");copy(*device);
        const auto before=snapshot();auto r=device->checkSurfaceSupport(window.surface());
        if(disabled||mode==1||mode==2||mode==3||mode==4)reject(std::move(r),gfx::ErrorCode::Unsupported);
        else if(mode==5){check(!r&&r.error().code==gfx::ErrorCode::BackendFailure&&r.error().nativeCode==VK_SURFACE_LOST_VALUE,"SurfaceLost reason preserved");}
        else if(headless)reject(std::move(r),gfx::ErrorCode::InvalidState);
        else{take(std::move(r));++checked;}
        const auto after=snapshot();const bool created=!disabled&&!headless&&mode!=1&&mode!=4;
        check(after.surfaces-before.surfaces==unsigned(created)&&after.destroyedSurfaces-before.destroyedSurfaces==unsigned(created),"Temporary native surface always retired");
        check(after.surfaceQueries-before.surfaceQueries==unsigned(created)&&after.visualQueries-before.visualQueries==unsigned(created),"Actual selected queue/visual queried");
        if(!disabled&&mode!=1&&mode!=4&&!headless){
            reject(device->checkSurfaceSupport({gfx::SurfaceKind::Xcb,nullptr,window.connection,0}),gfx::ErrorCode::InvalidArgument);
            reject(device->checkSurfaceSupport({gfx::SurfaceKind::Xcb}),gfx::ErrorCode::InvalidArgument);
            reject(device->checkSurfaceSupport({gfx::SurfaceKind::Xcb,window.connection,window.connection,window.window}),gfx::ErrorCode::InvalidArgument);
            reject(device->checkSurfaceSupport({static_cast<gfx::SurfaceKind>(99)}),gfx::ErrorCode::InvalidArgument);
            reject(device->checkSurfaceSupport({gfx::SurfaceKind::Win32}),gfx::ErrorCode::Unsupported);
            auto expired=xcb_generate_id(window.connection);xcb_create_window(window.connection,XCB_COPY_FROM_PARENT,expired,window.screen->root,0,0,7,5,0,XCB_WINDOW_CLASS_INPUT_OUTPUT,window.screen->root_visual,0,nullptr);xcb_destroy_window(window.connection,expired);xcb_flush(window.connection);
            reject(device->checkSurfaceSupport({gfx::SurfaceKind::Xcb,nullptr,window.connection,expired}),gfx::ErrorCode::InvalidArgument);
            auto input=xcb_generate_id(window.connection);xcb_create_window(window.connection,0,input,window.screen->root,0,0,7,5,0,XCB_WINDOW_CLASS_INPUT_ONLY,XCB_COPY_FROM_PARENT,0,nullptr);xcb_flush(window.connection);
            reject(device->checkSurfaceSupport({gfx::SurfaceKind::Xcb,nullptr,window.connection,input}),gfx::ErrorCode::InvalidArgument);xcb_destroy_window(window.connection,input);
            const auto rejected=snapshot();check(rejected.surfaces==after.surfaces,"Invalid native windows never reach Vulkan creation");
        }
        bool threadRejected{};std::thread worker([&]{auto result=device->checkSurfaceSupport(window.surface());threadRejected=!result&&result.error().code==gfx::ErrorCode::InvalidState;});worker.join();check(threadRejected,"Creating thread required");
        if(!headless){
            if(mode==0&&!disabled){auto swap=take(device->createSwapChain(window.surface(),{65,49}));auto image=take(swap->acquire());image.reset();take(swap->present());}
            else reject(device->createSwapChain(window.surface(),{65,49}),mode==5?gfx::ErrorCode::BackendFailure:gfx::ErrorCode::Unsupported);
        }
        take(device->checkStatus());copy(*device);take(device->queue().waitIdle());auto resources=take(device->resourceStatistics());check(!resources.uniqueResources&&!resources.trackedObjects&&!resources.pendingSubmissions,"No resource retention");
        device.reset();if(!headless){auto geometry=xcb_get_geometry_reply(window.connection,xcb_get_geometry(window.connection,window.window),nullptr);check(geometry&&geometry->width==65,"Caller window/connection remain owned by caller");std::free(geometry);}
        std::cout<<"{\"event\":\"adapter\",\"id\":"<<adapter.id.value<<",\"software\":"<<std::boolalpha<<adapter.softwareAdapter<<",\"name\":"<<std::quoted(adapter.name)<<"}\n";
    }
    const auto s=snapshot();check(s.instances==s.destroyedInstances&&s.surfaces==s.destroyedSurfaces,"Native instance/surface closure");
    std::cout<<"{\"event\":\"result\",\"source\":"<<std::quoted(gfx::buildFingerprint())<<",\"mode\":"<<mode<<",\"disabled\":"<<std::boolalpha<<disabled<<",\"headless\":"<<headless<<",\"adapters\":"<<adapters.size()<<",\"supported_surfaces\":"<<checked<<",\"instances\":"<<s.instances<<",\"destroyed_instances\":"<<s.destroyedInstances<<",\"xcb_enabled_instances\":"<<s.xcbEnabled<<",\"surfaces\":"<<s.surfaces<<",\"destroyed_surfaces\":"<<s.destroyedSurfaces<<",\"surface_queries\":"<<s.surfaceQueries<<",\"visual_queries\":"<<s.visualQueries<<",\"native_surface_true\":"<<s.nativeSurfaceTrue<<",\"native_visual_true\":"<<s.nativeVisualTrue<<",\"family\":"<<s.family<<",\"buffer_bytes\":"<<bytes<<",\"assertions\":"<<assertions<<",\"swapchain_implemented\":"<<(!disabled)<<",\"presented\":"<<(mode==0&&!disabled&&!headless)<<"}\n";dlclose(module);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
