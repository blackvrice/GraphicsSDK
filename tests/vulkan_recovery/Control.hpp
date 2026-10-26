#pragma once
#include <cstdint>
namespace recoveryprobe {
enum Mode : unsigned { None, SubmitLoss, PollLoss, FenceLoss, CreateOOM, CreateLoss };
struct Snapshot {
    std::uint64_t workCalls{}, idleCalls{}, created{}, destroyed{}, createAttempts{}, failedCreates{};
    std::uint64_t injected[6]{}, drains{}, errors{}, prematureCreations{}, lastLossSerial{};
    std::uint64_t reversedEnumerations{};
    unsigned armed{}, liveInstances{}, liveDevices{};
};
using Arm=int (*)(unsigned);
using Query=int (*)(Snapshot*,unsigned);
using Reverse=void (*)(unsigned);
}
