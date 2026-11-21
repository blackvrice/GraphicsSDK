#pragma once
#include <cstdint>
#include <optional>

namespace gfx {
enum class SurfaceKind { Win32, Xcb, MetalLayer };
// Win32 window is an HWND cast to void*. No platform header enters the public API.
// Xcb uses connection (xcb_connection_t*) and windowId; window must be null.
// MetalLayer uses a CAMetalLayer* bridged to void* in window. Use it on the macOS main thread.
// Caller owns all native handles and keeps them alive through SDK surface use.
struct Surface {
    SurfaceKind kind{SurfaceKind::Win32}; void* window{};
    void* connection{}; std::uint32_t windowId{};
};
enum class PresentMode { Fifo, Immediate, Mailbox, Tearing };
enum class FramePacingMode { Balanced, LowLatency, Throughput };
struct SurfacePresentCapabilities {
    bool fifo{true};
    bool immediate{};
    bool mailbox{};
    bool tearing{};
    bool frameLatencyWaitable{};
    std::uint32_t minImageCount{2};
    std::uint32_t maxImageCount{4};
    bool operator==(const SurfacePresentCapabilities&) const = default;
};
struct FramePacingStats {
    std::uint64_t presentedFrames{};
    double lastFrameMilliseconds{};
    double averageFrameMilliseconds{};
    double frameTimeStdDevMilliseconds{};
    double lastAcquireWaitMilliseconds{};
    double lastPresentWaitMilliseconds{};
    double lastQueueLatencyMilliseconds{};
    std::uint64_t gpuStallCount{};
    PresentMode activePresentMode{PresentMode::Fifo};
    FramePacingMode activePacingMode{FramePacingMode::Balanced};
    std::uint32_t maxFrameLatency{1};
};
struct SwapChainDesc {
    std::uint32_t width{}, height{}, buffers{2};
    bool vsync{};
    // Optional explicit present mode. When unset, preserves legacy vsync flag behavior.
    std::optional<PresentMode> presentMode;
    FramePacingMode pacingMode{FramePacingMode::Balanced};
    // 0 selects the pacingMode default (LowLatency=1, Balanced=min(2,buffers), Throughput=min(3,buffers)).
    // Explicit values must be in 1..4.
    std::uint32_t maxFrameLatency{};
    // Frame duration threshold (ms) above which a frame is flagged as a GPU stall.
    double stallThresholdMilliseconds{50.0};
};
}
