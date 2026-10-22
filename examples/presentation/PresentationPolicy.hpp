#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace presentation {
struct Rectangle { std::uint32_t x,y,width,height; };
inline std::optional<Rectangle> rectangle(std::uint32_t width, std::uint32_t height, std::uint32_t frame) {
    if (!width || !height) return std::nullopt;
    const auto w=std::max(1u,width/2), h=std::max(1u,height/2);
    const auto x=width/8+static_cast<std::uint32_t>(static_cast<std::uint64_t>(frame%31)*width/128);
    return Rectangle{std::min(x,width-w),std::min(height/4,height-h),w,h};
}

class FramePacer {
public:
    using Clock=std::chrono::steady_clock;
    using Time=Clock::time_point;
    explicit FramePacer(unsigned fps, Time now) : deadline(now) {
        if (!fps || fps>240) throw std::invalid_argument("--fps requires an integer in 1..240");
        // Round upward, so integer duration conversion never exceeds the requested cap.
        period=std::chrono::nanoseconds((1000000000ull+fps-1)/fps);
    }
    bool due(Time now) const { return now>=deadline; }
    void reset(Time now) { deadline=now; }
    // Schedule from the actual start, not an overdue deadline: no catch-up burst.
    void started(Time now) { deadline=now+period; }
    std::int64_t waitTicks100ns(Time now) const {
        if (due(now)) return 0;
        using Tick=std::chrono::duration<std::int64_t,std::ratio<1,10000000>>;
        return std::chrono::ceil<Tick>(deadline-now).count();
    }
private:
    Time deadline;
    std::chrono::nanoseconds period{};
};
}
