#pragma once
#include <graphics/Surface.hpp>
#include <graphics/Types.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <vector>

namespace gfx::detail {
// One namespace for every backend in a linked SDK image. Never wrap/reuse a fence owner.
class GenerationCounter {
    std::atomic<std::uint64_t> next_;
public:
    explicit GenerationCounter(std::uint64_t initial = 1) noexcept : next_(initial) {}
    [[nodiscard]] std::uint64_t next() noexcept {
        auto value = next_.load(std::memory_order_relaxed);
        do {
            if (!value || value == UINT64_MAX) return 0;
        } while (!next_.compare_exchange_weak(value, value + 1, std::memory_order_relaxed));
        return value;
    }
};
[[nodiscard]] std::uint64_t allocateDeviceGeneration() noexcept;

struct ProfilingTracker {
    static constexpr std::size_t kMaxRetainedScopes = 256;

    std::atomic<bool> enabled{false};
    std::atomic<std::uint64_t> drawCalls{};
    std::atomic<std::uint64_t> indexedDrawCalls{};
    std::atomic<std::uint64_t> instancedDrawCalls{};
    std::atomic<std::uint64_t> dispatchCalls{};
    std::atomic<std::uint64_t> renderPasses{};
    std::atomic<std::uint64_t> copyCommands{};
    std::atomic<std::uint64_t> barriers{};
    std::atomic<std::uint64_t> uavBarriers{};
    std::atomic<std::uint64_t> submissions{};
    std::atomic<std::uint64_t> pipelineCacheHits{};
    std::atomic<std::uint64_t> pipelineCacheMisses{};
    std::atomic<std::uint64_t> queueWaits{};
    std::atomic<std::uint64_t> timelineWaits{};
    std::atomic<std::uint64_t> cpuStallNanoseconds{};

    mutable std::mutex scopesMutex;
    std::vector<TimestampScopeResult> gpuScopes;

    bool isEnabled() const noexcept { return enabled.load(std::memory_order_relaxed); }

    void setEnabled(bool value) noexcept { enabled.store(value, std::memory_order_release); }

    void reset() {
        drawCalls.store(0, std::memory_order_relaxed);
        indexedDrawCalls.store(0, std::memory_order_relaxed);
        instancedDrawCalls.store(0, std::memory_order_relaxed);
        dispatchCalls.store(0, std::memory_order_relaxed);
        renderPasses.store(0, std::memory_order_relaxed);
        copyCommands.store(0, std::memory_order_relaxed);
        barriers.store(0, std::memory_order_relaxed);
        uavBarriers.store(0, std::memory_order_relaxed);
        submissions.store(0, std::memory_order_relaxed);
        pipelineCacheHits.store(0, std::memory_order_relaxed);
        pipelineCacheMisses.store(0, std::memory_order_relaxed);
        queueWaits.store(0, std::memory_order_relaxed);
        timelineWaits.store(0, std::memory_order_relaxed);
        cpuStallNanoseconds.store(0, std::memory_order_relaxed);
        std::lock_guard lock(scopesMutex);
        gpuScopes.clear();
    }

    void recordDraw(bool indexed, std::uint32_t instances) noexcept {
        if (!isEnabled()) return;
        drawCalls.fetch_add(1, std::memory_order_relaxed);
        if (indexed) indexedDrawCalls.fetch_add(1, std::memory_order_relaxed);
        if (instances > 1u) instancedDrawCalls.fetch_add(1, std::memory_order_relaxed);
    }

    void recordDispatch() noexcept {
        if (!isEnabled()) return;
        dispatchCalls.fetch_add(1, std::memory_order_relaxed);
    }

    void recordRenderPass() noexcept {
        if (!isEnabled()) return;
        renderPasses.fetch_add(1, std::memory_order_relaxed);
    }

    void recordCopy() noexcept {
        if (!isEnabled()) return;
        copyCommands.fetch_add(1, std::memory_order_relaxed);
    }

    void recordBarrier() noexcept {
        if (!isEnabled()) return;
        barriers.fetch_add(1, std::memory_order_relaxed);
    }

    void recordUavBarrier() noexcept {
        if (!isEnabled()) return;
        uavBarriers.fetch_add(1, std::memory_order_relaxed);
    }

    void recordSubmit() noexcept {
        if (!isEnabled()) return;
        submissions.fetch_add(1, std::memory_order_relaxed);
    }

    void recordPipelineCache(bool hit) noexcept {
        if (!isEnabled()) return;
        if (hit) pipelineCacheHits.fetch_add(1, std::memory_order_relaxed);
        else pipelineCacheMisses.fetch_add(1, std::memory_order_relaxed);
    }

    void recordQueueWait(std::uint64_t elapsedNs) noexcept {
        if (!isEnabled()) return;
        queueWaits.fetch_add(1, std::memory_order_relaxed);
        cpuStallNanoseconds.fetch_add(elapsedNs, std::memory_order_relaxed);
    }

    void recordTimelineWait(std::uint64_t elapsedNs) noexcept {
        if (!isEnabled()) return;
        timelineWaits.fetch_add(1, std::memory_order_relaxed);
        cpuStallNanoseconds.fetch_add(elapsedNs, std::memory_order_relaxed);
    }

    void appendGpuScopes(const std::vector<TimestampScopeResult>& scopes) {
        if (!isEnabled() || scopes.empty()) return;
        std::lock_guard lock(scopesMutex);
        for (const auto& s : scopes) {
            if (gpuScopes.size() >= kMaxRetainedScopes) {
                gpuScopes.erase(gpuScopes.begin());
            }
            gpuScopes.push_back(s);
        }
    }

    ProfilingSnapshot snapshot() const {
        ProfilingSnapshot out{};
        out.enabled = enabled.load(std::memory_order_acquire);
        if (!out.enabled) {
            // Disabled instrumentation reports zero counters and std::nullopt for measured timings/memory.
            return out;
        }
        out.counters.drawCalls = drawCalls.load(std::memory_order_relaxed);
        out.counters.indexedDrawCalls = indexedDrawCalls.load(std::memory_order_relaxed);
        out.counters.instancedDrawCalls = instancedDrawCalls.load(std::memory_order_relaxed);
        out.counters.dispatchCalls = dispatchCalls.load(std::memory_order_relaxed);
        out.counters.renderPasses = renderPasses.load(std::memory_order_relaxed);
        out.counters.copyCommands = copyCommands.load(std::memory_order_relaxed);
        out.counters.barriers = barriers.load(std::memory_order_relaxed);
        out.counters.uavBarriers = uavBarriers.load(std::memory_order_relaxed);
        out.counters.submissions = submissions.load(std::memory_order_relaxed);
        out.counters.pipelineCacheHits = pipelineCacheHits.load(std::memory_order_relaxed);
        out.counters.pipelineCacheMisses = pipelineCacheMisses.load(std::memory_order_relaxed);
        out.counters.queueWaits = queueWaits.load(std::memory_order_relaxed);
        out.counters.timelineWaits = timelineWaits.load(std::memory_order_relaxed);
        out.cpuStallMilliseconds = static_cast<double>(cpuStallNanoseconds.load(std::memory_order_relaxed)) / 1000000.0;
        std::lock_guard lock(scopesMutex);
        out.gpuTimestampScopes = gpuScopes;
        return out;
    }
};

inline Result<std::uint32_t> resolveFrameLatency(const SwapChainDesc& desc) {
    if (desc.pacingMode != FramePacingMode::Balanced &&
        desc.pacingMode != FramePacingMode::LowLatency &&
        desc.pacingMode != FramePacingMode::Throughput) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "Unknown FramePacingMode"});
    }
    if (desc.presentMode) {
        const auto m = *desc.presentMode;
        if (m != PresentMode::Fifo && m != PresentMode::Immediate &&
            m != PresentMode::Mailbox && m != PresentMode::Tearing) {
            return std::unexpected(Error{ErrorCode::InvalidArgument, "Unknown PresentMode"});
        }
    }
    if (!std::isfinite(desc.stallThresholdMilliseconds) || desc.stallThresholdMilliseconds <= 0.0) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "stallThresholdMilliseconds must be positive and finite"});
    }
    if (desc.maxFrameLatency > 4u) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "maxFrameLatency must be 0 (default) or 1..4"});
    }
    if (desc.maxFrameLatency != 0u) return desc.maxFrameLatency;
    if (desc.pacingMode == FramePacingMode::LowLatency) return 1u;
    if (desc.pacingMode == FramePacingMode::Throughput) return std::min<std::uint32_t>(3u, std::max<std::uint32_t>(1u, desc.buffers));
    return std::min<std::uint32_t>(2u, std::max<std::uint32_t>(1u, desc.buffers));
}

struct FramePacingTracker {
    using Clock = std::chrono::steady_clock;
    PresentMode activeMode{PresentMode::Fifo};
    FramePacingMode pacingMode{FramePacingMode::Balanced};
    std::uint32_t maxLatency{1};
    double stallThresholdMs{50.0};

    std::uint64_t presentedFrames{};
    std::uint64_t intervalSamples{};
    double meanFrameMs{};
    double m2FrameMs{};
    double lastFrameMs{};
    double lastAcquireWaitMs{};
    double lastPresentWaitMs{};
    double lastQueueLatencyMs{};
    std::uint64_t gpuStallCount{};
    bool hasAcquireStart{};
    bool hasLastPresent{};
    Clock::time_point acquireStart{};
    Clock::time_point lastPresentAt{};

    void configure(PresentMode mode, FramePacingMode pacing, std::uint32_t latency, double stallMs) noexcept {
        activeMode = mode;
        pacingMode = pacing;
        maxLatency = latency;
        stallThresholdMs = stallMs;
    }

    void reset() noexcept {
        presentedFrames = 0;
        intervalSamples = 0;
        meanFrameMs = 0.0;
        m2FrameMs = 0.0;
        lastFrameMs = 0.0;
        lastAcquireWaitMs = 0.0;
        lastPresentWaitMs = 0.0;
        lastQueueLatencyMs = 0.0;
        gpuStallCount = 0;
        hasAcquireStart = false;
        hasLastPresent = false;
    }

    void recordAcquireWait(double waitMs, Clock::time_point startedAt) noexcept {
        lastAcquireWaitMs = waitMs >= 0.0 ? waitMs : 0.0;
        if (!hasAcquireStart) {
            acquireStart = startedAt;
            hasAcquireStart = true;
        }
    }

    void recordStall() noexcept { ++gpuStallCount; }

    void recordPresent(double presentWaitMs, Clock::time_point finishedAt) noexcept {
        lastPresentWaitMs = presentWaitMs >= 0.0 ? presentWaitMs : 0.0;
        ++presentedFrames;
        if (hasAcquireStart) {
            lastQueueLatencyMs = std::chrono::duration<double, std::milli>(finishedAt - acquireStart).count();
            if (lastQueueLatencyMs < 0.0) lastQueueLatencyMs = 0.0;
            hasAcquireStart = false;
        } else {
            lastQueueLatencyMs = lastPresentWaitMs;
        }
        if (hasLastPresent) {
            const double frameMs = std::chrono::duration<double, std::milli>(finishedAt - lastPresentAt).count();
            lastFrameMs = frameMs >= 0.0 ? frameMs : 0.0;
            ++intervalSamples;
            const double delta = lastFrameMs - meanFrameMs;
            meanFrameMs += delta / static_cast<double>(intervalSamples);
            const double delta2 = lastFrameMs - meanFrameMs;
            m2FrameMs += delta * delta2;
            if (lastFrameMs > stallThresholdMs) ++gpuStallCount;
        } else {
            lastFrameMs = lastQueueLatencyMs;
            if (lastFrameMs > stallThresholdMs) ++gpuStallCount;
        }
        lastPresentAt = finishedAt;
        hasLastPresent = true;
    }

    FramePacingStats snapshot() const noexcept {
        FramePacingStats out{};
        out.presentedFrames = presentedFrames;
        out.lastFrameMilliseconds = lastFrameMs;
        out.averageFrameMilliseconds = intervalSamples > 0u ? meanFrameMs : lastFrameMs;
        out.frameTimeStdDevMilliseconds = intervalSamples > 1u ? std::sqrt(m2FrameMs / static_cast<double>(intervalSamples)) : 0.0;
        out.lastAcquireWaitMilliseconds = lastAcquireWaitMs;
        out.lastPresentWaitMilliseconds = lastPresentWaitMs;
        out.lastQueueLatencyMilliseconds = lastQueueLatencyMs;
        out.gpuStallCount = gpuStallCount;
        out.activePresentMode = activeMode;
        out.activePacingMode = pacingMode;
        out.maxFrameLatency = maxLatency;
        return out;
    }
};
}
