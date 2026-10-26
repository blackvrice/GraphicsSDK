#pragma once
#include <graphics/Graphics.hpp>
#include "Policy.hpp"
#include <xcb/xcb.h>
#include <poll.h>
#include <array>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>
namespace xcbPresentation {
inline void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class T>T take(gfx::Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(*r);}
inline void take(gfx::Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
struct Window {
    xcb_connection_t* connection{};xcb_window_t id{},root{};bool live{true};
    unsigned initialWidth{},initialHeight{};
    xcb_atom_t protocols{},deleteWindow{},wmState{},changeState{},supportingWM{};
    xcb_atom_t atom(const char* name){auto r=xcb_intern_atom_reply(connection,xcb_intern_atom(connection,0,std::strlen(name),name),nullptr);require(r,"XCB atom reply");auto a=r->atom;std::free(r);return a;}
    explicit Window(unsigned width=640,unsigned height=360):initialWidth(width),initialHeight(height){require(width&&height&&width<=65535&&height<=65535,"XCB extent");std::unique_ptr<xcb_connection_t,decltype(&xcb_disconnect)> allocation(xcb_connect(nullptr,nullptr),&xcb_disconnect);connection=allocation.get();require(connection&&!xcb_connection_has_error(connection),"XCB display required");auto screen=xcb_setup_roots_iterator(xcb_get_setup(connection));require(screen.rem,"XCB screen required");root=screen.data->root;id=xcb_generate_id(connection);
        uint32_t mask=XCB_EVENT_MASK_STRUCTURE_NOTIFY|XCB_EVENT_MASK_PROPERTY_CHANGE|XCB_EVENT_MASK_EXPOSURE;auto request=xcb_create_window_checked(connection,XCB_COPY_FROM_PARENT,id,root,0,0,width,height,0,XCB_WINDOW_CLASS_INPUT_OUTPUT,screen.data->root_visual,XCB_CW_EVENT_MASK,&mask);auto error=xcb_request_check(connection,request);bool ok=!error;std::free(error);require(ok,"XCB window creation");
        protocols=atom("WM_PROTOCOLS");deleteWindow=atom("WM_DELETE_WINDOW");wmState=atom("WM_STATE");changeState=atom("WM_CHANGE_STATE");supportingWM=atom("_NET_SUPPORTING_WM_CHECK");
        xcb_change_property(connection,XCB_PROP_MODE_REPLACE,id,protocols,XCB_ATOM_ATOM,32,1,&deleteWindow);const char title[]="GraphicsSDK XCB lifecycle";xcb_change_property(connection,XCB_PROP_MODE_REPLACE,id,XCB_ATOM_WM_NAME,XCB_ATOM_STRING,8,sizeof(title)-1,title);xcb_flush(connection);(void)allocation.release();
    }
    bool managed()const{auto r=xcb_get_property_reply(connection,xcb_get_property(connection,0,root,supportingWM,XCB_ATOM_WINDOW,0,1),nullptr);bool ok=r&&r->format==32&&xcb_get_property_value_length(r)==4;std::free(r);return ok;}
    unsigned managerState()const{auto r=xcb_get_property_reply(connection,xcb_get_property(connection,0,id,wmState,wmState,0,2),nullptr);unsigned state=0;if(r&&r->format==32&&xcb_get_property_value_length(r)>=4)state=*static_cast<uint32_t*>(xcb_get_property_value(r));std::free(r);return state;}
    bool exists(){auto r=xcb_get_window_attributes_reply(connection,xcb_get_window_attributes(connection,id),nullptr);bool ok=r&&r->_class==XCB_WINDOW_CLASS_INPUT_OUTPUT;std::free(r);return ok;}
    std::pair<unsigned,unsigned> geometry(){auto r=xcb_get_geometry_reply(connection,xcb_get_geometry(connection,id),nullptr);require(r,"XCB geometry reply");auto size=std::pair<unsigned,unsigned>{r->width,r->height};std::free(r);return size;}
    void map(){xcb_map_window(connection,id);xcb_flush(connection);}
    void resize(unsigned width,unsigned height){uint32_t values[]{width,height};xcb_configure_window(connection,id,XCB_CONFIG_WINDOW_WIDTH|XCB_CONFIG_WINDOW_HEIGHT,values);xcb_flush(connection);}
    void hide(){xcb_unmap_window(connection,id);xcb_flush(connection);}
    void minimize(){xcb_client_message_event_t e{};e.response_type=XCB_CLIENT_MESSAGE;e.format=32;e.window=id;e.type=changeState;e.data.data32[0]=3;xcb_send_event(connection,0,root,XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT|XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,reinterpret_cast<const char*>(&e));xcb_flush(connection);}
    void requestClose(){xcb_client_message_event_t e{};e.response_type=XCB_CLIENT_MESSAGE;e.format=32;e.window=id;e.type=protocols;e.data.data32[0]=deleteWindow;xcb_send_event(connection,0,id,XCB_EVENT_MASK_NO_EVENT,reinterpret_cast<const char*>(&e));xcb_flush(connection);}
    void destroy(){if(live){xcb_destroy_window(connection,id);xcb_flush(connection);live=false;}}
    gfx::Surface surface()const{return {gfx::SurfaceKind::Xcb,nullptr,connection,id};}
    ~Window(){if(connection){destroy();xcb_disconnect(connection);}}
};
struct Session {
    gfx::Device& device;Window& window;Policy policy;std::unique_ptr<gfx::SwapChain> chain;
    unsigned frames{},captures{},waits{},idleWaits{},events{},recoveries{};bool recovering{};
    gfx::BufferPtr lastReadback;gfx::TextureDesc lastDescription;gfx::TextureFootprint lastFootprint;
    Session(gfx::Device& d,Window& w):device(d),window(w){policy.configure(w.initialWidth,w.initialHeight);}
    void stop(){chain.reset();lastReadback.reset();}
    void pump(){
        while(auto event=xcb_poll_for_event(window.connection)){
            ++events;const auto type=event->response_type&0x7f;
            if(type==XCB_MAP_NOTIFY&&reinterpret_cast<xcb_map_notify_event_t*>(event)->window==window.id)policy.map();
            if(type==XCB_UNMAP_NOTIFY&&reinterpret_cast<xcb_unmap_notify_event_t*>(event)->window==window.id)policy.unmap();
            if(type==XCB_CONFIGURE_NOTIFY){auto e=reinterpret_cast<xcb_configure_notify_event_t*>(event);if(e->window==window.id)policy.configure(e->width,e->height);}
            if(type==XCB_PROPERTY_NOTIFY){auto e=reinterpret_cast<xcb_property_notify_event_t*>(event);if(e->window==window.id&&e->atom==window.wmState)policy.iconic(window.managerState()==3);}
            if(type==XCB_CLIENT_MESSAGE){auto e=reinterpret_cast<xcb_client_message_event_t*>(event);if(e->window==window.id&&e->format==32&&e->type==window.protocols&&e->data.data32[0]==window.deleteWindow)policy.close();}
            if(type==XCB_DESTROY_NOTIFY&&reinterpret_cast<xcb_destroy_notify_event_t*>(event)->window==window.id){window.live=false;policy.expire();}
            std::free(event);
        }
        if(xcb_connection_has_error(window.connection)){window.live=false;policy.expire();}
        if(policy.closing)stop();
    }
    void wait(int timeout){++waits;if(timeout<0)++idleWaits;pollfd fd{xcb_get_file_descriptor(window.connection),POLLIN,0};const auto r=::poll(&fd,1,timeout);if(r<0)throw std::runtime_error("XCB event poll failed");pump();}
    bool handle(const gfx::Error& error){
        if(error.nativeCode==-1000000000){
            if(recovering)throw std::runtime_error("Replacement XCB surface is unavailable");
            recovering=true;++recoveries;stop();policy.dirty=true;return false;
        }
        if(error.nativeCode==-1000001004){auto [w,h]=window.geometry();policy.configure(w,h);policy.dirty=true;return false;}
        if(xcb_connection_has_error(window.connection)||!window.exists()){window.live=false;policy.expire();stop();return false;}
        throw std::runtime_error(error.message);
    }
    bool frame(bool capture=false){
        if(!policy.active())return false;
        policy.pacer.started(presentation::FramePacer::Clock::now());
        if(!chain){auto r=device.createSwapChain(window.surface(),{policy.width,policy.height,2,true});if(!r)return handle(r.error());chain=std::move(*r);policy.dirty=false;}
        else if(policy.dirty){auto r=chain->resize(policy.width,policy.height);if(!r)return handle(r.error());policy.dirty=false;}
        auto acquired=chain->acquire();if(!acquired)return handle(acquired.error());auto texture=*acquired;
        auto commands=take(device.createCommandList());take(commands->begin());gfx::BufferPtr read,pad;
        auto footprint=texture->footprint();if(capture){read=take(device.createBuffer({footprint.size,gfx::Memory::Readback}));pad=take(device.createBuffer({footprint.size,gfx::Memory::Upload}));take(pad->write(0,std::vector<std::byte>(footprint.size,std::byte{0xb7})));take(commands->copyBuffer(pad,read,footprint.size));}
        take(commands->transition(texture,gfx::ResourceState::RenderTarget));take(commands->beginRenderPass(texture,{.25f,.5f,.75f,1}));take(commands->endRenderPass());
        if(capture){take(commands->transition(texture,gfx::ResourceState::CopySource));take(commands->copyTextureToBuffer(texture,read));}
        take(commands->transition(texture,gfx::ResourceState::Present));take(commands->end());auto f=take(device.queue().submit(*commands));auto presented=chain->present();take(device.queue().wait(f));
        if(capture){lastReadback=read;lastDescription=texture->description();lastFootprint=footprint;++captures;}
        if(!presented)return handle(presented.error());
        recovering=false;++frames;return true;
    }
    ~Session(){stop();}
};
}
