#pragma once
#include <graphics/Types.hpp>
#include <atomic>
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
}
