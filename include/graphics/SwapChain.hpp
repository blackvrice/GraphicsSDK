#pragma once
#include "Resources.hpp"
#include "Surface.hpp"
#include <chrono>

namespace gfx {
class SwapChain {
public:
    virtual ~SwapChain() = default;
    // A retained buffer is usable only while this swapchain exists and that
    // buffer is current. Commands recorded before its next present become stale.
    [[nodiscard]] virtual Result<TexturePtr> acquire() = 0;
    [[nodiscard]] virtual Result<void> present() = 0;
    // Release acquired references first; resize waits for queued GPU work.
    [[nodiscard]] virtual Result<void> resize(std::uint32_t width, std::uint32_t height) = 0;
    [[nodiscard]] virtual PresentMode presentMode() const = 0;
    [[nodiscard]] virtual FramePacingMode pacingMode() const = 0;
    [[nodiscard]] virtual std::uint32_t maxFrameLatency() const = 0;
    // Waits on the swapchain's frame-latency pacing primitive when available.
    [[nodiscard]] virtual Result<void> waitForNextFrame(std::chrono::milliseconds timeout = std::chrono::seconds(5)) = 0;
    // Returns real wall-clock frame pacing, queue-latency, and GPU stall statistics.
    [[nodiscard]] virtual Result<FramePacingStats> framePacingStats() const = 0;
    [[nodiscard]] virtual Result<void> resetFramePacingStats() = 0;
};
}
