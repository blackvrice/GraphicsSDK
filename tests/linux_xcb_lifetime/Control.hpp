#pragma once
#include <cstdint>
namespace lifetimeprobe {
enum Operation : unsigned { Arm=1, Release=2, Blocked=3, Cleanup=4, FailFence=10, FailSubmit=11, FailSecondView=12, IdleEntered=20, PresentEntered=21 };
struct Snapshot {
    std::uint64_t views{}, destroyedViews{}, semaphores{}, destroyedSemaphores{}, fences{}, destroyedFences{};
    std::uint64_t submits{}, gates{}, blocked{}, released{}, cleaned{}, idleBoundaries{}, presentBoundaries{}, faults{}, orderErrors{};
};
using Control=int(*)(unsigned);
using Query=int(*)(Snapshot*,unsigned);
}
