#include "common/Telemetry.hpp"
#include <limits>
#include <iostream>
int main() {
    unsigned checks=0;
    auto check=[&](bool ok) { ++checks; if (!ok) throw std::runtime_error("CPU telemetry arithmetic failed"); };
    check(bench::cpuOneCorePercent(0,10000000,1000)==100);
    check(bench::cpuOneCorePercent(0,20000000,1000)==200);
    check(bench::cpuOneCorePercent(100,100,10)==0);
    check(bench::cpuOneCorePercent(UINT64_MAX-10000,UINT64_MAX,2)==50);
    for (double wall:{0.,-1.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
        bool rejected=false; try { bench::cpuOneCorePercent(0,1,wall); } catch(const std::runtime_error&) { rejected=true; }
        check(rejected);
    }
    bool rejected=false; try { bench::cpuOneCorePercent(2,1,1); } catch(const std::runtime_error&) { rejected=true; }
    check(rejected);
    std::cout<<checks<<" CPU telemetry checks passed\n";
}
