#pragma once
#include "Types.hpp"
#include <memory>
#include <span>
#include <vector>

namespace gfx {
class Buffer {
public:
    virtual ~Buffer() = default;
    [[nodiscard]] virtual BufferDesc description() const = 0;
    [[nodiscard]] virtual Result<void> write(std::uint64_t offset, std::span<const std::byte> data) = 0;
    [[nodiscard]] virtual Result<std::vector<std::byte>> read(std::uint64_t offset, std::uint64_t bytes) = 0;
};
class Texture {
public:
    virtual ~Texture() = default;
    [[nodiscard]] virtual TextureDesc description() const = 0;
    [[nodiscard]] virtual TextureFootprint footprint() const = 0;
};
using BufferPtr = std::shared_ptr<Buffer>;
using TexturePtr = std::shared_ptr<Texture>;
class TextureView {
public:
    virtual ~TextureView() = default;
    [[nodiscard]] virtual TextureViewDesc description() const = 0;
    [[nodiscard]] virtual TexturePtr texture() const = 0;
};
using TextureViewPtr = std::shared_ptr<TextureView>;

struct UploadRingDesc {
    std::uint64_t capacity{4u * 1024u * 1024u};
    std::uint64_t alignment{256u};
    std::uint32_t segmentCount{3u};
    QueueType queue{QueueType::Graphics};
};
struct UploadAllocation {
    BufferPtr buffer;
    std::uint64_t offset{};
    std::uint64_t size{};
    std::uint64_t reservedBytes{};
    std::uint32_t segmentIndex{};
};
struct UploadRingStats {
    std::uint64_t capacityBytes{};
    std::uint64_t segmentBytes{};
    std::uint64_t activeBytes{};
    std::uint64_t inFlightBytes{};
    std::uint64_t availableBytes{};
    std::uint32_t segmentCount{};
    std::uint32_t inFlightSegments{};
    std::uint64_t totalAllocations{};
    std::uint64_t totalWrapCount{};
    std::uint64_t totalReclaims{};
};
[[nodiscard]] Result<void> validateUploadRingDesc(const UploadRingDesc&);

class UploadRing {
public:
    virtual ~UploadRing() = default;
    UploadRing(const UploadRing&) = delete;
    UploadRing& operator=(const UploadRing&) = delete;
    [[nodiscard]] virtual UploadRingDesc description() const = 0;
    // Creating thread only. Sub-allocates an aligned range from the current active ring segment.
    // Returns OutOfMemory if all segments are in flight and cannot yet be reclaimed.
    [[nodiscard]] virtual Result<UploadAllocation> allocate(std::uint64_t bytes, std::uint64_t alignment = 0) = 0;
    // Creating thread only. Allocates and copies CPU bytes atomically into the ring.
    [[nodiscard]] virtual Result<UploadAllocation> upload(std::span<const std::byte> data, std::uint64_t alignment = 0) = 0;
    // Creating thread only. Associates all active unsubmitted segments with the submitted queue Fence.
    [[nodiscard]] virtual Result<void> commit(Fence fence) = 0;
    // Creating thread only. Discards unsubmitted allocations made since the last commit.
    [[nodiscard]] virtual Result<void> discardPending() = 0;
    // Creating thread only. Non-blockingly reclaims completed in-flight segments and returns freed bytes.
    [[nodiscard]] virtual Result<std::uint64_t> reclaim() = 0;
    [[nodiscard]] virtual Result<UploadRingStats> statistics() = 0;
protected:
    UploadRing() = default;
};

struct ReadbackPoolDesc {
    std::uint64_t maxPooledBytes{64u * 1024u * 1024u};
    std::uint32_t maxPooledBuffers{32u};
    QueueType queue{QueueType::Graphics};
};
struct PoolStats {
    std::uint64_t pooledBytes{};
    std::uint64_t inFlightBytes{};
    std::uint32_t pooledCount{};
    std::uint32_t inFlightCount{};
    std::uint64_t hits{};
    std::uint64_t misses{};
    std::uint64_t evictions{};
};
[[nodiscard]] Result<void> validateReadbackPoolDesc(const ReadbackPoolDesc&);

class ReadbackPool {
public:
    virtual ~ReadbackPool() = default;
    ReadbackPool(const ReadbackPool&) = delete;
    ReadbackPool& operator=(const ReadbackPool&) = delete;
    [[nodiscard]] virtual ReadbackPoolDesc description() const = 0;
    // Creating thread only. Acquires a Readback buffer of at least minBytes, reusing idle entries when available.
    [[nodiscard]] virtual Result<BufferPtr> acquire(std::uint64_t minBytes) = 0;
    // Creating thread only. Returns a buffer acquired from this pool; if fence.value > 0, waits for completion before reuse.
    [[nodiscard]] virtual Result<void> recycle(BufferPtr buffer, Fence fence = {}) = 0;
    // Creating thread only. Non-blockingly moves completed in-flight buffers back to the idle pool.
    [[nodiscard]] virtual Result<std::uint64_t> reclaim() = 0;
    // Creating thread only. Evicts oldest idle buffers until pooledBytes <= targetMaxBytes.
    [[nodiscard]] virtual Result<std::uint64_t> trim(std::uint64_t targetMaxBytes = 0) = 0;
    [[nodiscard]] virtual Result<PoolStats> statistics() = 0;
protected:
    ReadbackPool() = default;
};

struct BudgetPressurePolicy {
    bool enabled{true};
    double softPressureRatio{0.80};
    double hardPressureRatio{0.92};
    std::uint64_t softTargetBytes{};
};
enum class BudgetPressureLevel { Normal, Soft, Hard, Unavailable };
struct BudgetTrimReport {
    BudgetPressureLevel level{BudgetPressureLevel::Unavailable};
    std::uint64_t budgetBytes{};
    std::uint64_t usageBytes{};
    std::uint64_t freedBytes{};
    std::uint32_t evictedResources{};
};
struct TransientPoolDesc {
    std::uint64_t maxPooledBytes{128u * 1024u * 1024u};
    std::uint32_t maxPooledResources{64u};
    QueueType queue{QueueType::Graphics};
    BudgetPressurePolicy budgetPolicy{};
};
[[nodiscard]] Result<void> validateTransientPoolDesc(const TransientPoolDesc&);

class TransientResourcePool {
public:
    virtual ~TransientResourcePool() = default;
    TransientResourcePool(const TransientResourcePool&) = delete;
    TransientResourcePool& operator=(const TransientResourcePool&) = delete;
    [[nodiscard]] virtual TransientPoolDesc description() const = 0;
    [[nodiscard]] virtual Result<BufferPtr> acquireBuffer(BufferDesc desc) = 0;
    [[nodiscard]] virtual Result<TexturePtr> acquireTexture(TextureDesc desc) = 0;
    [[nodiscard]] virtual Result<void> recycle(BufferPtr buffer, Fence fence = {}) = 0;
    [[nodiscard]] virtual Result<void> recycle(TexturePtr texture, Fence fence = {}) = 0;
    [[nodiscard]] virtual Result<std::uint64_t> reclaim() = 0;
    [[nodiscard]] virtual Result<std::uint64_t> trim(std::uint64_t targetMaxBytes = 0) = 0;
    // Creating thread only. Queries OS local VRAM budget and trims idle resources when under pressure.
    // Returns BudgetPressureLevel::Unavailable without error on backends where queryMemoryBudget is unsupported.
    [[nodiscard]] virtual Result<BudgetTrimReport> applyBudgetPressure() = 0;
    // Creating thread only. Applies the configured budget pressure policy against explicit usage/budget values.
    [[nodiscard]] virtual Result<BudgetTrimReport> applyBudgetPressure(std::uint64_t usageBytes, std::uint64_t budgetBytes) = 0;
    [[nodiscard]] virtual Result<PoolStats> statistics() = 0;
protected:
    TransientResourcePool() = default;
};
}
