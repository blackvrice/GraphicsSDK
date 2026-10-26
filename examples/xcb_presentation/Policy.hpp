#pragma once
#include "../presentation/PresentationPolicy.hpp"
#include <climits>
namespace xcbPresentation {
struct Policy {
    unsigned width{640},height{360};bool mapped{},minimized{},closing{},destroyed{},dirty{true};
    presentation::FramePacer pacer{60,presentation::FramePacer::Clock::now()};
    bool active()const{return mapped&&!minimized&&!closing&&!destroyed&&width&&height;}
    void map(){if(closing||destroyed)return;mapped=true;pacer.reset(presentation::FramePacer::Clock::now());}
    void unmap(){mapped=false;}
    void iconic(bool value){minimized=value;if(!value)pacer.reset(presentation::FramePacer::Clock::now());}
    void configure(unsigned w,unsigned h){if(width!=w||height!=h){width=w;height=h;dirty=true;}}
    void close(){closing=true;}
    void expire(){destroyed=true;closing=true;mapped=false;}
    int timeout(presentation::FramePacer::Time now)const{
        if(!active())return -1;
        const auto ticks=pacer.waitTicks100ns(now);return static_cast<int>(std::min<std::int64_t>(INT_MAX,(ticks+9999)/10000));
    }
};
}
