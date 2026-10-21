#pragma once
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace bench {
inline constexpr const char* gpuTimestampScope="draw_calls_after_clear_and_bind_before_pass_end_v1";
inline double validGpuMilliseconds(double value) {
    if (!std::isfinite(value) || value<0) throw std::runtime_error("Invalid GPU timestamp duration");
    return value;
}
inline double gpuTicksToMilliseconds(std::uint64_t begin,std::uint64_t end,double frequency) {
    if (end<begin || !std::isfinite(frequency) || frequency<=0)
        throw std::runtime_error("Invalid GPU timestamp counters/frequency");
    return validGpuMilliseconds(static_cast<double>(end-begin)/frequency*1000.0);
}
}
