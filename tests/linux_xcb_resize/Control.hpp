#pragma once
#include <cstdint>
namespace resizeprobe {
enum Operation : unsigned { AcquireOutOfDate=1, AcquireSuboptimal=2, PresentOutOfDate=3, PresentSuboptimal=4, FailAllocation=5, FailEnumeration=6, FailSecondView=7 };
struct Snapshot {
    std::uint64_t creates{}, oldAttempts{}, freshAttempts{}, nativeFailures{}, allocations{}, enumFailures{};
    std::uint64_t acquireOutOfDate{}, acquireSuboptimal{}, presentOutOfDate{}, presentSuboptimal{}, orderErrors{};
};
using Control=int(*)(unsigned);
using Query=int(*)(Snapshot*,unsigned);
}
