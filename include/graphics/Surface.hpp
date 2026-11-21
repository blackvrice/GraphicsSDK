#pragma once
#include "Types.hpp"
#include <cstdint>
#include <optional>
#include <vector>

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
// Live native surface geometry, DPI scale, and minimized/occluded state.
struct SurfaceMetrics {
    std::uint32_t clientWidth{};
    std::uint32_t clientHeight{};
    float dpiScale{1.0f};
    std::uint32_t dpi{96};
    bool minimized{};
    bool occluded{};
    bool operator==(const SurfaceMetrics&) const = default;
};
struct MultiPresentResult {
    std::uint32_t presentedCount{};
    std::uint32_t skippedMinimizedCount{};
    bool operator==(const MultiPresentResult&) const = default;
};
enum class PresentMode { Fifo, Immediate, Mailbox, Tearing };
enum class FramePacingMode { Balanced, LowLatency, Throughput };
enum class ColorSpace {
    SrgbNonlinear,
    LinearSrgb,
    DisplayP3Nonlinear,
    Rec2020Linear,
    Hdr10St2084,
    ExtendedLinearScRgb
};
// Static HDR10 mastering display and content light-level metadata (SMPTE ST 2086 / CTA-861.3).
struct HdrMetadata {
    // CIE 1931 xy chromaticity coordinates in [0.0, 1.0].
    float redPrimaryX{0.708f}, redPrimaryY{0.292f};
    float greenPrimaryX{0.170f}, greenPrimaryY{0.797f};
    float bluePrimaryX{0.131f}, bluePrimaryY{0.046f};
    float whitePointX{0.3127f}, whitePointY{0.3290f};
    // Luminance values in nits (cd/m^2).
    float maxMasteringLuminance{1000.0f};
    float minMasteringLuminance{0.001f};
    float maxContentLightLevel{1000.0f};
    float maxFrameAverageLightLevel{400.0f};
    bool operator==(const HdrMetadata&) const = default;
};
struct SurfaceColorFormatCapability {
    Format format{Format::RGBA8UNorm};
    ColorSpace colorSpace{ColorSpace::SrgbNonlinear};
    bool operator==(const SurfaceColorFormatCapability&) const = default;
};
struct SurfaceColorCapabilities {
    std::vector<SurfaceColorFormatCapability> supportedFormats;
    bool srgbNonlinear{true};
    bool linearSrgb{};
    bool displayP3{};
    bool rec2020Linear{};
    bool hdr10St2084{};
    bool extendedLinearScRgb{};
    bool tenBitOutput{};
    bool hdrMetadata{};
    ColorSpace activeDisplayColorSpace{ColorSpace::SrgbNonlinear};
    bool displayHdrEnabled{};
    float displayMaxLuminanceNits{};
    float displayMinLuminanceNits{};
};
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
    // Optional explicit backbuffer format. When unset, preserves legacy backend default (RGBA8UNorm/BGRA8UNorm).
    std::optional<Format> format;
    // Requested swapchain output color space. Defaults to SrgbNonlinear.
    ColorSpace colorSpace{ColorSpace::SrgbNonlinear};
    // Optional static HDR10 mastering metadata applied on creation when supported.
    std::optional<HdrMetadata> hdrMetadata;
};
}
