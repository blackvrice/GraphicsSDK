#pragma once
#include "CommandList.hpp"
#include <chrono>
#include <memory>
#include <span>

namespace gfx {
class TimelineFence {
public:
    virtual ~TimelineFence() = default;
    TimelineFence(const TimelineFence&) = delete;
    TimelineFence& operator=(const TimelineFence&) = delete;
    // Creating thread only. Returns the highest value completed by GPU or CPU so far.
    [[nodiscard]] virtual Result<std::uint64_t> completedValue() = 0;
    // Creating thread only. Advances the timeline fence from CPU to a strictly higher value.
    [[nodiscard]] virtual Result<void> signal(std::uint64_t value) = 0;
    // Creating thread only. Waits on CPU until completedValue() >= value or timeout expires.
    // Finite timeout range: 0..4294967294ms.
    [[nodiscard]] virtual Result<void> wait(std::uint64_t value, std::chrono::milliseconds timeout = std::chrono::seconds(30)) = 0;
protected:
    TimelineFence() = default;
};
using TimelineFencePtr = std::shared_ptr<TimelineFence>;

struct TimelineWait {
    TimelineFencePtr fence;
    std::uint64_t value{};
};
struct TimelineSignal {
    TimelineFencePtr fence;
    std::uint64_t value{};
};
struct SubmitDesc {
    std::span<CommandList* const> commandLists;
    std::span<const TimelineWait> waits;
    std::span<const TimelineSignal> signals;
};

class CommandQueue {
public:
    virtual ~CommandQueue() = default;
    [[nodiscard]] virtual QueueType type() const = 0;
    [[nodiscard]] virtual Result<Fence> submit(CommandList&) = 0;
    // Creating thread only. Atomically validates and submits an ordered batch of recorded
    // command lists in a single queue submission. Lists in a batch may share read-only
    // resources; mutated or transitioned resources must be disjoint within the batch.
    [[nodiscard]] virtual Result<Fence> submit(std::span<CommandList* const> lists) = 0;
    // Creating thread only. Atomically validates and submits command lists with GPU-side
    // timeline fence waits (ordered before execution) and signals (ordered after execution).
    [[nodiscard]] virtual Result<Fence> submit(const SubmitDesc& desc) = 0;
    // Creating thread only. Zero polls without creating/registering a wait event.
    // Finite timeout range: 0..4294967294ms, including already completed fences.
    // Timeout keeps submissions/resources alive and permits retrying the same fence.
    [[nodiscard]] virtual Result<void> wait(Fence, std::chrono::milliseconds timeout = std::chrono::seconds(30)) = 0;
    [[nodiscard]] virtual Result<void> waitIdle() = 0;
    [[nodiscard]] virtual Result<std::uint64_t> completedValue() = 0;
};
}
