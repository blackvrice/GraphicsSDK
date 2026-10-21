#include "common/GpuTimestamp.hpp"
#include <iostream>
#include <limits>

int main() {
    unsigned checks{};
    auto check=[&](bool value) { ++checks; if (!value) throw std::runtime_error("Timestamp conversion contract failed"); };
    auto rejects=[&](auto operation) { try { operation(); } catch (const std::runtime_error&) { return true; } return false; };
    check(bench::gpuTicksToMilliseconds(123,223,1000)==100.0);
    check(bench::gpuTicksToMilliseconds(0,1000000,1e9)==1.0);
    check(bench::gpuTicksToMilliseconds(UINT64_MAX-10,UINT64_MAX,1000)==10.0);
    check(bench::gpuTicksToMilliseconds(42,42,1000)==0.0);
    check(rejects([] { bench::gpuTicksToMilliseconds(100,1,1000); }));
    for (double invalid : {0.0,-1.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
        check(rejects([&] { bench::gpuTicksToMilliseconds(1,2,invalid); }));
    for (double invalid : {-1.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
        check(rejects([&] { bench::validGpuMilliseconds(invalid); }));
    std::cout << checks << " timestamp conversion checks passed\n";
}
