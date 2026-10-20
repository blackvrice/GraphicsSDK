#pragma once
#include "CommandList.hpp"
#include <chrono>

namespace gfx {
class CommandQueue {
public:
    virtual ~CommandQueue() = default;
    [[nodiscard]] virtual Result<Fence> submit(CommandList&) = 0;
    [[nodiscard]] virtual Result<void> wait(Fence, std::chrono::milliseconds timeout = std::chrono::seconds(30)) = 0;
    [[nodiscard]] virtual Result<void> waitIdle() = 0;
    [[nodiscard]] virtual Result<std::uint64_t> completedValue() = 0;
};
}
