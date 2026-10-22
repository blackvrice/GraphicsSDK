#pragma once
#include "CommandList.hpp"
#include <chrono>

namespace gfx {
class CommandQueue {
public:
    virtual ~CommandQueue() = default;
    [[nodiscard]] virtual Result<Fence> submit(CommandList&) = 0;
    // Creating thread only. Zero polls without creating/registering a wait event.
    // Finite timeout range: 0..4294967294ms, including already completed fences.
    // Timeout keeps submissions/resources alive and permits retrying the same fence.
    [[nodiscard]] virtual Result<void> wait(Fence, std::chrono::milliseconds timeout = std::chrono::seconds(30)) = 0;
    [[nodiscard]] virtual Result<void> waitIdle() = 0;
    [[nodiscard]] virtual Result<std::uint64_t> completedValue() = 0;
};
}
