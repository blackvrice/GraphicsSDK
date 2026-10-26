#include "XcbLoop.hpp"
#include <iostream>
int main(int argc,char** argv)try{
    gfx::DeviceDesc desc;desc.backend=gfx::Backend::Vulkan;
    for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--software")desc.softwareAdapter=true;else if(std::string_view(argv[i])=="--validation")desc.validation=true;else throw std::runtime_error("Usage: gfx_xcb_window [--software] [--validation]");
    auto device=xcbPresentation::take(gfx::createDevice(desc));xcbPresentation::Window window;xcbPresentation::Session app(*device,window);window.map();
    while(!app.policy.closing){app.pump();if(app.policy.closing)break;auto now=presentation::FramePacer::Clock::now();if(app.policy.active()&&app.policy.pacer.due(now))app.frame();else app.wait(app.policy.timeout(now));}
    app.stop();return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
