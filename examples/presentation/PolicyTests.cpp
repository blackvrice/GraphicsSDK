#include "PresentationPolicy.hpp"
#include <array>
#include <iostream>
#include <limits>

namespace {
unsigned assertions{};
void check(bool value, const char* message) {
    ++assertions;
    if (!value) throw std::runtime_error(message);
}
}
int main() try {
    using namespace std::chrono_literals;
    using P=presentation::FramePacer;
    const P::Time zero{};
    for (unsigned fps : {1u,30u,60u,144u,240u}) {
        P clock(fps,zero);
        check(clock.due(zero),"Initial frame must be immediately due");
        clock.started(zero);
        check(!clock.due(zero),"New deadline must defer another frame");
        const auto ns=std::chrono::nanoseconds((1000000000ull+fps-1)/fps);
        check(!clock.due(zero+ns-1ns) && clock.due(zero+ns),"Exact deadline boundary");
        check(clock.waitTicks100ns(zero+ns-1ns)==1,"Round timer ticks upward to avoid an early frame");
        check(clock.waitTicks100ns(zero+ns)==0,"Elapsed deadline must not wait");
        check(clock.waitTicks100ns(zero)<=10000000,"Bounded active wait");
        // Input wakes the OS wait but does not make an early frame due.
        for (unsigned i=0; i<100; ++i) check(!clock.due(zero+ns/2),"Message wake bypassed cap");
        clock.started(zero+10s);
        check(!clock.due(zero+10s) && clock.due(zero+10s+ns),"Slow frames must not catch up in a burst");
        clock.reset(zero+20s);
        check(clock.due(zero+20s),"Restoring a suspended window must render immediately");
    }
    for (unsigned fps : {0u,241u,std::numeric_limits<unsigned>::max()}) {
        bool rejected=false;
        try { P invalid(fps,zero); } catch (const std::invalid_argument&) { rejected=true; }
        check(rejected,"Reject invalid frame cap");
    }
    check(!presentation::rectangle(0,1,0) && !presentation::rectangle(1,0,0),"Zero extent must remain suspended");
    constexpr std::array<std::uint32_t,12> extents{1,2,3,4,9,127,129,257,639,16384,0x7fffffffu,0xffffffffu};
    for (auto width : extents) for (auto height : extents) for (auto frame : {0u,1u,15u,30u,31u,0xffffffffu}) {
        const auto r=*presentation::rectangle(width,height,frame);
        check(r.width>0 && r.height>0,"Positive target must have a drawable rectangle");
        check(r.x<=width-r.width && r.y<=height-r.height,"Scissor must stay inside target without overflow");
        if (width==1 && height==1) check(r.x==0 && r.y==0 && r.width==1 && r.height==1,"1x1 target must be covered");
    }
    std::cout << "PASS: " << assertions << " presentation policy assertions\n";
    return 0;
} catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
