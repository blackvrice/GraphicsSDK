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
    [[nodiscard]] virtual Format format() const = 0;
    [[nodiscard]] virtual ColorSpace colorSpace() const = 0;
    // Sets static HDR10 mastering metadata when supported by the backend/surface.
    [[nodiscard]] virtual Result<void> setHdrMetadata(const HdrMetadata& metadata) = 0;
    [[nodiscard]] virtual std::optional<HdrMetadata> hdrMetadata() const = 0;
    [[nodiscard]] virtual std::uint32_t width() const = 0;
    [[nodiscard]] virtual std::uint32_t height() const = 0;
    // Queries live native surface metrics (extent, DPI scale, minimized/occluded state).
    [[nodiscard]] virtual Result<SurfaceMetrics> surfaceMetrics() const = 0;
    // Resizes the swapchain to match the native surface when changed; returns true if resized,
    // false if already matching, or InvalidState when the surface is minimized (0x0).
    [[nodiscard]] virtual Result<bool> syncExtent() = 0;
};
}
