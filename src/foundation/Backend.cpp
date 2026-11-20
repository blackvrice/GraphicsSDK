#include <graphics/Graphics.hpp>
#include "DeviceGeneration.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <deque>
#include <limits>
#include <list>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace gfx {
struct Device::GraphicsCache {
    struct Entry {
        GraphicsPipelineDesc desc;
        PipelinePtr pipeline;
    };
    std::list<Entry> entries;
};

namespace {
bool validQueueType(QueueType type) noexcept {
    return type == QueueType::Graphics || type == QueueType::Compute || type == QueueType::Copy;
}

bool validAlignment(std::uint64_t alignment) noexcept {
    return alignment >= 4u && alignment <= 65536u && std::has_single_bit(alignment);
}

std::uint64_t alignUp(std::uint64_t value, std::uint64_t alignment) noexcept {
    const std::uint64_t rem = value % alignment;
    return rem == 0 ? value : (value + (alignment - rem));
}

bool sameTextureDesc(const TextureDesc& a, const TextureDesc& b) noexcept {
    return a.width == b.width && a.height == b.height && a.format == b.format &&
           a.renderTarget == b.renderTarget && a.depthStencil == b.depthStencil &&
           a.storage == b.storage && a.sampleCount == b.sampleCount;
}

std::uint64_t estimateTextureBytes(const Texture& texture) noexcept {
    const auto fp = texture.footprint().size;
    if (fp > 0) return fp;
    const auto d = texture.description();
    const std::uint64_t bpp = (d.format == Format::RGBA16Float) ? 8u : 4u;
    const std::uint64_t samples = std::max<std::uint32_t>(1u, d.sampleCount);
    return static_cast<std::uint64_t>(d.width) * d.height * bpp * samples;
}

bool cacheableKey(const GraphicsPipelineDesc& desc) {
    // Bound copies of bytecode and semantic strings. Backend validation bounds layout counts.
    std::size_t available = 1024 * 1024;
    const auto charge = [&available](std::size_t bytes) {
        if (bytes > available) return false;
        available -= bytes;
        return true;
    };
    return charge(desc.vertex.bytes.size()) && charge(desc.fragment.bytes.size()) &&
           std::ranges::all_of(desc.vertexLayout.attributes, [&](const auto& attribute) {
               return charge(attribute.semantic.size());
           });
}

class UploadRingImpl final : public UploadRing {
    enum class SegState { Free, Active, InFlight };
    struct Segment {
        BufferPtr buffer;
        SegState state{SegState::Free};
        std::uint64_t cursor{};
        std::uint64_t fenceValue{};
    };
    Device* device{};
    std::shared_ptr<bool> alive;
    UploadRingDesc desc{};
    std::uint64_t segmentBytes{};
    std::vector<Segment> segments;
    std::vector<std::uint32_t> activeOrder;
    std::uint32_t nextFreeHint{};
    std::uint64_t observedQueueId{};
    std::uint64_t lastCommittedFence{};
    std::uint64_t totalAllocations{};
    std::uint64_t totalWrapCount{};
    std::uint64_t totalReclaims{};

    Result<CommandQueue*> targetQueue() {
        if (!alive || !*alive) {
            return std::unexpected(Error{ErrorCode::DeviceLost, "Owning device has been destroyed"});
        }
        return device->queue(desc.queue);
    }

    Result<std::uint64_t> reclaimInternal(CommandQueue& q) {
        auto completed = q.completedValue();
        if (!completed) return std::unexpected(completed.error());
        std::uint64_t freed = 0;
        for (auto& seg : segments) {
            if (seg.state == SegState::InFlight && seg.fenceValue <= *completed) {
                freed += seg.cursor;
                seg.state = SegState::Free;
                seg.cursor = 0;
                seg.fenceValue = 0;
                ++totalReclaims;
            }
        }
        return freed;
    }

public:
    UploadRingImpl(Device* dev, std::shared_ptr<bool> guard, UploadRingDesc d, std::uint64_t segBytes, std::vector<BufferPtr> buffers)
        : device(dev), alive(std::move(guard)), desc(d), segmentBytes(segBytes) {
        segments.reserve(buffers.size());
        for (auto& buf : buffers) {
            segments.push_back(Segment{std::move(buf), SegState::Free, 0, 0});
        }
    }

    UploadRingDesc description() const override { return desc; }

    Result<UploadAllocation> allocate(std::uint64_t bytes, std::uint64_t alignment) override {
        auto q = targetQueue();
        if (!q) return std::unexpected(q.error());
        auto reclaimed = reclaimInternal(**q);
        if (!reclaimed) return std::unexpected(reclaimed.error());

        const std::uint64_t align = alignment ? alignment : desc.alignment;
        if (!bytes) {
            return std::unexpected(Error{ErrorCode::InvalidArgument, "UploadRing allocation requires positive size"});
        }
        if (!validAlignment(align) || align > segmentBytes) {
            return std::unexpected(Error{ErrorCode::InvalidArgument, "UploadRing alignment must be a power of two within segment capacity"});
        }
        if (bytes > segmentBytes) {
            return std::unexpected(Error{ErrorCode::InvalidArgument, "UploadRing allocation exceeds single-segment capacity"});
        }

        if (!activeOrder.empty()) {
            const std::uint32_t curIdx = activeOrder.back();
            auto& seg = segments[curIdx];
            const std::uint64_t aligned = alignUp(seg.cursor, align);
            if (aligned <= segmentBytes && bytes <= segmentBytes - aligned) {
                const std::uint64_t prev = seg.cursor;
                seg.cursor = aligned + bytes;
                ++totalAllocations;
                return UploadAllocation{seg.buffer, aligned, bytes, seg.cursor - prev, curIdx};
            }
        }

        const std::uint32_t count = static_cast<std::uint32_t>(segments.size());
        const std::uint32_t startIdx = activeOrder.empty() ? nextFreeHint : ((activeOrder.back() + 1u) % count);
        for (std::uint32_t step = 0; step < count; ++step) {
            const std::uint32_t idx = (startIdx + step) % count;
            if (segments[idx].state == SegState::Free) {
                if (!activeOrder.empty() && idx < activeOrder.back()) {
                    ++totalWrapCount;
                } else if (activeOrder.empty() && idx == 0 && totalAllocations > 0 && nextFreeHint == 0) {
                    ++totalWrapCount;
                }
                auto& seg = segments[idx];
                seg.state = SegState::Active;
                seg.cursor = bytes;
                seg.fenceValue = 0;
                activeOrder.push_back(idx);
                ++totalAllocations;
                return UploadAllocation{seg.buffer, 0, bytes, bytes, idx};
            }
        }
        return std::unexpected(Error{ErrorCode::OutOfMemory, "UploadRing saturated; wait for in-flight fences or increase capacity"});
    }

    Result<UploadAllocation> upload(std::span<const std::byte> data, std::uint64_t alignment) override {
        const std::size_t prevActiveSize = activeOrder.size();
        const std::uint32_t prevLastIdx = prevActiveSize ? activeOrder.back() : 0u;
        const std::uint64_t prevCursor = prevActiveSize ? segments[prevLastIdx].cursor : 0u;
        const std::uint64_t prevAllocs = totalAllocations;
        const std::uint64_t prevWraps = totalWrapCount;

        auto alloc = allocate(data.size(), alignment);
        if (!alloc) return alloc;
        auto written = alloc->buffer->write(alloc->offset, data);
        if (!written) {
            // Roll back reservation atomically on write failure.
            if (activeOrder.size() > prevActiveSize) {
                const std::uint32_t addedIdx = activeOrder.back();
                activeOrder.pop_back();
                segments[addedIdx].state = SegState::Free;
                segments[addedIdx].cursor = 0;
            } else if (prevActiveSize > 0) {
                segments[prevLastIdx].cursor = prevCursor;
            }
            totalAllocations = prevAllocs;
            totalWrapCount = prevWraps;
            return std::unexpected(written.error());
        }
        return alloc;
    }

    Result<void> commit(Fence fence) override {
        auto q = targetQueue();
        if (!q) return std::unexpected(q.error());
        auto reclaimed = reclaimInternal(**q);
        if (!reclaimed) return std::unexpected(reclaimed.error());
        if (!fence.value || !fence.device) {
            return std::unexpected(Error{ErrorCode::InvalidArgument, "UploadRing::commit requires a valid submitted Fence"});
        }
        if (observedQueueId && fence.device != observedQueueId) {
            return std::unexpected(Error{ErrorCode::WrongDevice, "UploadRing::commit Fence belongs to another queue or device"});
        }
        if (fence.value < lastCommittedFence) {
            return std::unexpected(Error{ErrorCode::InvalidArgument, "UploadRing::commit Fence value must be monotonic"});
        }
        auto completed = (*q)->completedValue();
        if (!completed) return std::unexpected(completed.error());
        observedQueueId = fence.device;
        lastCommittedFence = fence.value;
        if (!activeOrder.empty()) {
            const std::uint32_t lastIdx = activeOrder.back();
            for (const std::uint32_t idx : activeOrder) {
                auto& seg = segments[idx];
                if (fence.value <= *completed) {
                    seg.state = SegState::Free;
                    seg.cursor = 0;
                    seg.fenceValue = 0;
                    ++totalReclaims;
                } else {
                    seg.state = SegState::InFlight;
                    seg.fenceValue = fence.value;
                }
            }
            activeOrder.clear();
            nextFreeHint = (lastIdx + 1u) % static_cast<std::uint32_t>(segments.size());
        }
        return {};
    }

    Result<void> discardPending() override {
        auto q = targetQueue();
        if (!q) return std::unexpected(q.error());
        for (const std::uint32_t idx : activeOrder) {
            segments[idx].state = SegState::Free;
            segments[idx].cursor = 0;
            segments[idx].fenceValue = 0;
        }
        activeOrder.clear();
        return {};
    }

    Result<std::uint64_t> reclaim() override {
        auto q = targetQueue();
        if (!q) return std::unexpected(q.error());
        return reclaimInternal(**q);
    }

    Result<UploadRingStats> statistics() override {
        auto q = targetQueue();
        if (!q) return std::unexpected(q.error());
        auto reclaimed = reclaimInternal(**q);
        if (!reclaimed) return std::unexpected(reclaimed.error());

        UploadRingStats stats{};
        stats.segmentCount = static_cast<std::uint32_t>(segments.size());
        stats.segmentBytes = segmentBytes;
        stats.capacityBytes = segmentBytes * stats.segmentCount;
        stats.totalAllocations = totalAllocations;
        stats.totalWrapCount = totalWrapCount;
        stats.totalReclaims = totalReclaims;
        for (std::size_t i = 0; i < segments.size(); ++i) {
            const auto& seg = segments[i];
            if (seg.state == SegState::Free) {
                stats.availableBytes += segmentBytes;
            } else if (seg.state == SegState::Active) {
                stats.activeBytes += seg.cursor;
                if (!activeOrder.empty() && activeOrder.back() == i) {
                    stats.availableBytes += (segmentBytes - seg.cursor);
                }
            } else {
                stats.inFlightBytes += seg.cursor;
                ++stats.inFlightSegments;
            }
        }
        return stats;
    }
};

class ReadbackPoolImpl final : public ReadbackPool {
    struct IdleEntry {
        BufferPtr buffer;
        std::uint64_t size{};
    };
    struct InFlightEntry {
        BufferPtr buffer;
        std::uint64_t size{};
        std::uint64_t fenceValue{};
    };
    Device* device{};
    std::shared_ptr<bool> alive;
    ReadbackPoolDesc desc{};
    std::uint64_t observedQueueId{};
    std::deque<IdleEntry> idle;
    std::deque<InFlightEntry> inFlight;
    std::unordered_map<const Buffer*, std::weak_ptr<Buffer>> leased;
    std::uint64_t pooledBytes{};
    std::uint64_t inFlightBytes{};
    std::uint64_t hits{};
    std::uint64_t misses{};
    std::uint64_t evictions{};

    Result<CommandQueue*> targetQueue() {
        if (!alive || !*alive) {
            return std::unexpected(Error{ErrorCode::DeviceLost, "Owning device has been destroyed"});
        }
        return device->queue(desc.queue);
    }

    void sweepLeased() {
        std::erase_if(leased, [](const auto& pair) { return pair.second.expired(); });
    }

    bool isPooledOrInFlight(const Buffer* ptr) const {
        for (const auto& e : idle) if (e.buffer.get() == ptr) return true;
        for (const auto& e : inFlight) if (e.buffer.get() == ptr) return true;
        return false;
    }

    void enforceCaps() {
        while (!idle.empty() && (pooledBytes > desc.maxPooledBytes || idle.size() > desc.maxPooledBuffers)) {
            pooledBytes -= idle.front().size;
            idle.pop_front();
            ++evictions;
        }
    }

    Result<std::uint64_t> reclaimInternal(CommandQueue& q) {
        auto completed = q.completedValue();
        if (!completed) return std::unexpected(completed.error());
        std::uint64_t moved = 0;
        std::deque<InFlightEntry> remaining;
        for (auto& entry : inFlight) {
            if (entry.fenceValue <= *completed) {
                inFlightBytes -= entry.size;
                pooledBytes += entry.size;
                moved += entry.size;
                idle.push_back(IdleEntry{std::move(entry.buffer), entry.size});
            } else {
                remaining.push_back(std::move(entry));
            }
        }
        inFlight = std::move(remaining);
        enforceCaps();
        return moved;
    }

public:
    ReadbackPoolImpl(Device* dev, std::shared_ptr<bool> guard, ReadbackPoolDesc d)
        : device(dev), alive(std::move(guard)), desc(d) {}

    ReadbackPoolDesc description() const override { return desc; }

    Result<BufferPtr> acquire(std::uint64_t minBytes) override {
        auto q = targetQueue();
        if (!q) return std::unexpected(q.error());
        auto reclaimed = reclaimInternal(**q);
        if (!reclaimed) return std::unexpected(reclaimed.error());
        if (!minBytes || minBytes > (std::uint64_t{1} << 62)) {
            return std::unexpected(Error{ErrorCode::InvalidArgument, "ReadbackPool::acquire requires positive size within bounds"});
        }
        const std::uint64_t maxAcceptable = std::max<std::uint64_t>(minBytes * 2u, 512u);
        auto best = idle.end();
        for (auto it = idle.begin(); it != idle.end(); ++it) {
            if (it->size >= minBytes && it->size <= maxAcceptable) {
                if (best == idle.end() || it->size < best->size) best = it;
            }
        }
        if (best != idle.end()) {
            BufferPtr buf = std::move(best->buffer);
            pooledBytes -= best->size;
            idle.erase(best);
            ++hits;
            sweepLeased();
            leased[buf.get()] = buf;
            return buf;
        }
        const std::uint64_t bucketBytes = std::max<std::uint64_t>(256u, std::bit_ceil(minBytes));
        auto created = device->createBuffer({bucketBytes, Memory::Readback});
        if (!created) return std::unexpected(created.error());
        ++misses;
        sweepLeased();
        leased[created->get()] = *created;
        return created;
    }

    Result<void> recycle(BufferPtr buffer, Fence fence) override {
        auto q = targetQueue();
        if (!q) return std::unexpected(q.error());
        auto reclaimed = reclaimInternal(**q);
        if (!reclaimed) return std::unexpected(reclaimed.error());
        if (!buffer) {
            return std::unexpected(Error{ErrorCode::InvalidArgument, "ReadbackPool::recycle requires a non-null buffer"});
        }
        if (buffer->description().memory != Memory::Readback) {
            return std::unexpected(Error{ErrorCode::InvalidArgument, "ReadbackPool::recycle requires a Readback memory buffer"});
        }
        if (isPooledOrInFlight(buffer.get())) {
            return std::unexpected(Error{ErrorCode::InvalidState, "Buffer is already recycled in ReadbackPool"});
        }
        const auto found = leased.find(buffer.get());
        if (found == leased.end() || found->second.expired()) {
            return std::unexpected(Error{ErrorCode::WrongDevice, "Buffer was not acquired from this ReadbackPool"});
        }
        if (fence.value) {
            if (!fence.device) {
                return std::unexpected(Error{ErrorCode::InvalidArgument, "Invalid recycle Fence device identity"});
            }
            if (observedQueueId && fence.device != observedQueueId) {
                return std::unexpected(Error{ErrorCode::WrongDevice, "Recycle Fence belongs to another queue or device"});
            }
            observedQueueId = fence.device;
        }
        auto completed = (*q)->completedValue();
        if (!completed) return std::unexpected(completed.error());
        const std::uint64_t size = buffer->description().size;
        leased.erase(found);
        if (fence.value > *completed) {
            inFlightBytes += size;
            inFlight.push_back(InFlightEntry{std::move(buffer), size, fence.value});
        } else {
            pooledBytes += size;
            idle.push_back(IdleEntry{std::move(buffer), size});
            enforceCaps();
        }
        return {};
    }

    Result<std::uint64_t> reclaim() override {
        auto q = targetQueue();
        if (!q) return std::unexpected(q.error());
        return reclaimInternal(**q);
    }

    Result<std::uint64_t> trim(std::uint64_t targetMaxBytes) override {
        auto q = targetQueue();
        if (!q) return std::unexpected(q.error());
        auto reclaimed = reclaimInternal(**q);
        if (!reclaimed) return std::unexpected(reclaimed.error());
        std::uint64_t freed = 0;
        while (!idle.empty() && pooledBytes > targetMaxBytes) {
            freed += idle.front().size;
            pooledBytes -= idle.front().size;
            idle.pop_front();
            ++evictions;
        }
        return freed;
    }

    Result<PoolStats> statistics() override {
        auto q = targetQueue();
        if (!q) return std::unexpected(q.error());
        auto reclaimed = reclaimInternal(**q);
        if (!reclaimed) return std::unexpected(reclaimed.error());
        return PoolStats{
            pooledBytes,
            inFlightBytes,
            static_cast<std::uint32_t>(idle.size()),
            static_cast<std::uint32_t>(inFlight.size()),
            hits,
            misses,
            evictions
        };
    }
};

class TransientResourcePoolImpl final : public TransientResourcePool {
    struct IdleBuffer {
        BufferPtr buffer;
        BufferDesc desc;
        std::uint64_t size{};
        std::uint64_t seq{};
    };
    struct InFlightBuffer {
        BufferPtr buffer;
        BufferDesc desc;
        std::uint64_t size{};
        std::uint64_t fenceValue{};
    };
    struct IdleTexture {
        TexturePtr texture;
        TextureDesc desc;
        std::uint64_t bytes{};
        std::uint64_t seq{};
    };
    struct InFlightTexture {
        TexturePtr texture;
        TextureDesc desc;
        std::uint64_t bytes{};
        std::uint64_t fenceValue{};
    };

    Device* device{};
    std::shared_ptr<bool> alive;
    TransientPoolDesc desc{};
    std::uint64_t observedQueueId{};
    std::uint64_t nextIdleSeq{1};
    std::deque<IdleBuffer> idleBuffers;
    std::deque<InFlightBuffer> inFlightBuffers;
    std::deque<IdleTexture> idleTextures;
    std::deque<InFlightTexture> inFlightTextures;
    std::unordered_map<const Buffer*, std::weak_ptr<Buffer>> leasedBuffers;
    std::unordered_map<const Texture*, std::weak_ptr<Texture>> leasedTextures;
    std::uint64_t pooledBytes{};
    std::uint64_t inFlightBytes{};
    std::uint64_t hits{};
    std::uint64_t misses{};
    std::uint64_t evictions{};

    Result<CommandQueue*> targetQueue() {
        if (!alive || !*alive) {
            return std::unexpected(Error{ErrorCode::DeviceLost, "Owning device has been destroyed"});
        }
        return device->queue(desc.queue);
    }

    void sweepLeased() {
        std::erase_if(leasedBuffers, [](const auto& pair) { return pair.second.expired(); });
        std::erase_if(leasedTextures, [](const auto& pair) { return pair.second.expired(); });
    }

    std::size_t totalIdleCount() const noexcept {
        return idleBuffers.size() + idleTextures.size();
    }

    std::pair<std::uint64_t, std::uint32_t> evictUntil(std::uint64_t targetMaxBytes, std::size_t targetMaxCount) {
        std::uint64_t freed = 0;
        std::uint32_t count = 0;
        while ((pooledBytes > targetMaxBytes || totalIdleCount() > targetMaxCount) && totalIdleCount() > 0) {
            const bool popBuf = !idleBuffers.empty() &&
                (idleTextures.empty() || idleBuffers.front().seq <= idleTextures.front().seq);
            if (popBuf) {
                freed += idleBuffers.front().size;
                pooledBytes -= idleBuffers.front().size;
                idleBuffers.pop_front();
            } else {
                freed += idleTextures.front().bytes;
                pooledBytes -= idleTextures.front().bytes;
                idleTextures.pop_front();
            }
            ++count;
            ++evictions;
        }
        return {freed, count};
    }

    Result<std::uint64_t> reclaimInternal(CommandQueue& q) {
        auto completed = q.completedValue();
        if (!completed) return std::unexpected(completed.error());
        std::uint64_t moved = 0;
        {
            std::deque<InFlightBuffer> remaining;
            for (auto& e : inFlightBuffers) {
                if (e.fenceValue <= *completed) {
                    inFlightBytes -= e.size;
                    pooledBytes += e.size;
                    moved += e.size;
                    idleBuffers.push_back(IdleBuffer{std::move(e.buffer), e.desc, e.size, nextIdleSeq++});
                } else {
                    remaining.push_back(std::move(e));
                }
            }
            inFlightBuffers = std::move(remaining);
        }
        {
            std::deque<InFlightTexture> remaining;
            for (auto& e : inFlightTextures) {
                if (e.fenceValue <= *completed) {
                    inFlightBytes -= e.bytes;
                    pooledBytes += e.bytes;
                    moved += e.bytes;
                    idleTextures.push_back(IdleTexture{std::move(e.texture), e.desc, e.bytes, nextIdleSeq++});
                } else {
                    remaining.push_back(std::move(e));
                }
            }
            inFlightTextures = std::move(remaining);
        }
        (void)evictUntil(desc.maxPooledBytes, desc.maxPooledResources);
        return moved;
    }

    Result<BudgetTrimReport> applyBudgetInternal(std::uint64_t usageBytes, std::uint64_t budgetBytes) {
        BudgetTrimReport report{};
        report.budgetBytes = budgetBytes;
        report.usageBytes = usageBytes;
        if (!desc.budgetPolicy.enabled || budgetBytes == 0) {
            report.level = BudgetPressureLevel::Normal;
            return report;
        }
        const double ratio = static_cast<double>(usageBytes) / static_cast<double>(budgetBytes);
        if (ratio >= desc.budgetPolicy.hardPressureRatio) {
            report.level = BudgetPressureLevel::Hard;
            const auto [freed, count] = evictUntil(0, 0);
            report.freedBytes = freed;
            report.evictedResources = count;
        } else if (ratio >= desc.budgetPolicy.softPressureRatio) {
            report.level = BudgetPressureLevel::Soft;
            const std::uint64_t target = desc.budgetPolicy.softTargetBytes
                ? std::min(desc.budgetPolicy.softTargetBytes, desc.maxPooledBytes)
                : (desc.maxPooledBytes / 2u);
            const auto [freed, count] = evictUntil(target, desc.maxPooledResources);
            report.freedBytes = freed;
            report.evictedResources = count;
        } else {
            report.level = BudgetPressureLevel::Normal;
        }
        return report;
    }

public:
    TransientResourcePoolImpl(Device* dev, std::shared_ptr<bool> guard, TransientPoolDesc d)
        : device(dev), alive(std::move(guard)), desc(d) {}

    TransientPoolDesc description() const override { return desc; }

    Result<BufferPtr> acquireBuffer(BufferDesc req) override {
        auto q = targetQueue();
        if (!q) return std::unexpected(q.error());
        auto reclaimed = reclaimInternal(**q);
        if (!reclaimed) return std::unexpected(reclaimed.error());
        if (!req.size || req.size > (std::uint64_t{1} << 62)) {
            return std::unexpected(Error{ErrorCode::InvalidArgument, "TransientResourcePool::acquireBuffer requires positive size within bounds"});
        }
        const std::uint64_t maxAcceptable = std::max<std::uint64_t>(req.size * 2u, 512u);
        auto best = idleBuffers.end();
        for (auto it = idleBuffers.begin(); it != idleBuffers.end(); ++it) {
            if (it->desc.memory == req.memory && it->desc.storage == req.storage &&
                it->size >= req.size && it->size <= maxAcceptable) {
                if (best == idleBuffers.end() || it->size < best->size) best = it;
            }
        }
        if (best != idleBuffers.end()) {
            BufferPtr buf = std::move(best->buffer);
            pooledBytes -= best->size;
            idleBuffers.erase(best);
            ++hits;
            sweepLeased();
            leasedBuffers[buf.get()] = buf;
            return buf;
        }
        auto created = device->createBuffer(req);
        if (!created) return std::unexpected(created.error());
        ++misses;
        sweepLeased();
        leasedBuffers[created->get()] = *created;
        return created;
    }

    Result<TexturePtr> acquireTexture(TextureDesc req) override {
        auto q = targetQueue();
        if (!q) return std::unexpected(q.error());
        auto reclaimed = reclaimInternal(**q);
        if (!reclaimed) return std::unexpected(reclaimed.error());
        for (auto it = idleTextures.begin(); it != idleTextures.end(); ++it) {
            if (sameTextureDesc(it->desc, req)) {
                TexturePtr tex = std::move(it->texture);
                pooledBytes -= it->bytes;
                idleTextures.erase(it);
                ++hits;
                sweepLeased();
                leasedTextures[tex.get()] = tex;
                return tex;
            }
        }
        auto created = device->createTexture(req);
        if (!created) return std::unexpected(created.error());
        ++misses;
        sweepLeased();
        leasedTextures[created->get()] = *created;
        return created;
    }

    Result<void> recycle(BufferPtr buffer, Fence fence) override {
        auto q = targetQueue();
        if (!q) return std::unexpected(q.error());
        auto reclaimed = reclaimInternal(**q);
        if (!reclaimed) return std::unexpected(reclaimed.error());
        if (!buffer) {
            return std::unexpected(Error{ErrorCode::InvalidArgument, "TransientResourcePool::recycle requires a non-null buffer"});
        }
        for (const auto& e : idleBuffers) if (e.buffer.get() == buffer.get())
            return std::unexpected(Error{ErrorCode::InvalidState, "Buffer is already recycled in TransientResourcePool"});
        for (const auto& e : inFlightBuffers) if (e.buffer.get() == buffer.get())
            return std::unexpected(Error{ErrorCode::InvalidState, "Buffer is already recycled in TransientResourcePool"});
        const auto found = leasedBuffers.find(buffer.get());
        if (found == leasedBuffers.end() || found->second.expired()) {
            return std::unexpected(Error{ErrorCode::WrongDevice, "Buffer was not acquired from this TransientResourcePool"});
        }
        if (fence.value) {
            if (!fence.device) return std::unexpected(Error{ErrorCode::InvalidArgument, "Invalid recycle Fence device identity"});
            if (observedQueueId && fence.device != observedQueueId)
                return std::unexpected(Error{ErrorCode::WrongDevice, "Recycle Fence belongs to another queue or device"});
            observedQueueId = fence.device;
        }
        auto completed = (*q)->completedValue();
        if (!completed) return std::unexpected(completed.error());
        const auto bDesc = buffer->description();
        leasedBuffers.erase(found);
        if (fence.value > *completed) {
            inFlightBytes += bDesc.size;
            inFlightBuffers.push_back(InFlightBuffer{std::move(buffer), bDesc, bDesc.size, fence.value});
        } else {
            pooledBytes += bDesc.size;
            idleBuffers.push_back(IdleBuffer{std::move(buffer), bDesc, bDesc.size, nextIdleSeq++});
            (void)evictUntil(desc.maxPooledBytes, desc.maxPooledResources);
        }
        return {};
    }

    Result<void> recycle(TexturePtr texture, Fence fence) override {
        auto q = targetQueue();
        if (!q) return std::unexpected(q.error());
        auto reclaimed = reclaimInternal(**q);
        if (!reclaimed) return std::unexpected(reclaimed.error());
        if (!texture) {
            return std::unexpected(Error{ErrorCode::InvalidArgument, "TransientResourcePool::recycle requires a non-null texture"});
        }
        for (const auto& e : idleTextures) if (e.texture.get() == texture.get())
            return std::unexpected(Error{ErrorCode::InvalidState, "Texture is already recycled in TransientResourcePool"});
        for (const auto& e : inFlightTextures) if (e.texture.get() == texture.get())
            return std::unexpected(Error{ErrorCode::InvalidState, "Texture is already recycled in TransientResourcePool"});
        const auto found = leasedTextures.find(texture.get());
        if (found == leasedTextures.end() || found->second.expired()) {
            return std::unexpected(Error{ErrorCode::WrongDevice, "Texture was not acquired from this TransientResourcePool"});
        }
        if (fence.value) {
            if (!fence.device) return std::unexpected(Error{ErrorCode::InvalidArgument, "Invalid recycle Fence device identity"});
            if (observedQueueId && fence.device != observedQueueId)
                return std::unexpected(Error{ErrorCode::WrongDevice, "Recycle Fence belongs to another queue or device"});
            observedQueueId = fence.device;
        }
        auto completed = (*q)->completedValue();
        if (!completed) return std::unexpected(completed.error());
        const auto tDesc = texture->description();
        const std::uint64_t bytes = estimateTextureBytes(*texture);
        leasedTextures.erase(found);
        if (fence.value > *completed) {
            inFlightBytes += bytes;
            inFlightTextures.push_back(InFlightTexture{std::move(texture), tDesc, bytes, fence.value});
        } else {
            pooledBytes += bytes;
            idleTextures.push_back(IdleTexture{std::move(texture), tDesc, bytes, nextIdleSeq++});
            (void)evictUntil(desc.maxPooledBytes, desc.maxPooledResources);
        }
        return {};
    }

    Result<std::uint64_t> reclaim() override {
        auto q = targetQueue();
        if (!q) return std::unexpected(q.error());
        return reclaimInternal(**q);
    }

    Result<std::uint64_t> trim(std::uint64_t targetMaxBytes) override {
        auto q = targetQueue();
        if (!q) return std::unexpected(q.error());
        auto reclaimed = reclaimInternal(**q);
        if (!reclaimed) return std::unexpected(reclaimed.error());
        return evictUntil(targetMaxBytes, desc.maxPooledResources).first;
    }

    Result<BudgetTrimReport> applyBudgetPressure() override {
        auto q = targetQueue();
        if (!q) return std::unexpected(q.error());
        auto reclaimed = reclaimInternal(**q);
        if (!reclaimed) return std::unexpected(reclaimed.error());
        auto budget = device->queryMemoryBudget(MemorySegment::Local);
        if (!budget) {
            if (budget.error().code == ErrorCode::Unsupported) {
                return BudgetTrimReport{BudgetPressureLevel::Unavailable, 0, 0, 0, 0};
            }
            return std::unexpected(budget.error());
        }
        return applyBudgetInternal(budget->usageBytes, budget->budgetBytes);
    }

    Result<BudgetTrimReport> applyBudgetPressure(std::uint64_t usageBytes, std::uint64_t budgetBytes) override {
        auto q = targetQueue();
        if (!q) return std::unexpected(q.error());
        auto reclaimed = reclaimInternal(**q);
        if (!reclaimed) return std::unexpected(reclaimed.error());
        return applyBudgetInternal(usageBytes, budgetBytes);
    }

    Result<PoolStats> statistics() override {
        auto q = targetQueue();
        if (!q) return std::unexpected(q.error());
        auto reclaimed = reclaimInternal(**q);
        if (!reclaimed) return std::unexpected(reclaimed.error());
        return PoolStats{
            pooledBytes,
            inFlightBytes,
            static_cast<std::uint32_t>(idleBuffers.size() + idleTextures.size()),
            static_cast<std::uint32_t>(inFlightBuffers.size() + inFlightTextures.size()),
            hits,
            misses,
            evictions
        };
    }
};
} // namespace

Result<void> validateUploadRingDesc(const UploadRingDesc& desc) {
    if (!validQueueType(desc.queue)) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "Unknown UploadRing queue type"});
    }
    if (desc.segmentCount < 2u || desc.segmentCount > 16u) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "UploadRing segmentCount must be 2..16"});
    }
    if (!validAlignment(desc.alignment)) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "UploadRing alignment must be a power of two in 4..65536"});
    }
    if (!desc.capacity || desc.capacity / desc.segmentCount < desc.alignment) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "UploadRing capacity must hold at least alignment bytes per segment"});
    }
    return {};
}

Result<void> validateReadbackPoolDesc(const ReadbackPoolDesc& desc) {
    if (!validQueueType(desc.queue)) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "Unknown ReadbackPool queue type"});
    }
    if (!desc.maxPooledBytes || !desc.maxPooledBuffers) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "ReadbackPool caps must be positive"});
    }
    return {};
}

Result<void> validateTransientPoolDesc(const TransientPoolDesc& desc) {
    if (!validQueueType(desc.queue)) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "Unknown TransientResourcePool queue type"});
    }
    if (!desc.maxPooledBytes || !desc.maxPooledResources) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "TransientResourcePool caps must be positive"});
    }
    if (desc.budgetPolicy.enabled) {
        const double soft = desc.budgetPolicy.softPressureRatio;
        const double hard = desc.budgetPolicy.hardPressureRatio;
        if (!std::isfinite(soft) || !std::isfinite(hard) || soft <= 0.0 || soft >= hard || hard > 1.0) {
            return std::unexpected(Error{ErrorCode::InvalidArgument, "BudgetPressurePolicy requires 0 < softPressureRatio < hardPressureRatio <= 1"});
        }
    }
    return {};
}

Device::Device() noexcept = default;
Device::~Device() {
    if (lifetimeGuard) *lifetimeGuard = false;
}

Result<std::unique_ptr<UploadRing>> Device::createUploadRing(UploadRingDesc desc) {
    if (auto status = checkStatus(); !status) return std::unexpected(status.error());
    if (auto valid = validateUploadRingDesc(desc); !valid) return std::unexpected(valid.error());
    if (auto q = queue(desc.queue); !q) return std::unexpected(q.error());
    if (!lifetimeGuard) lifetimeGuard = std::make_shared<bool>(true);
    const std::uint64_t segmentBytes = ((desc.capacity / desc.segmentCount) / desc.alignment) * desc.alignment;
    std::vector<BufferPtr> buffers;
    buffers.reserve(desc.segmentCount);
    for (std::uint32_t i = 0; i < desc.segmentCount; ++i) {
        auto buf = createBuffer({segmentBytes, Memory::Upload});
        if (!buf) return std::unexpected(buf.error());
        buffers.push_back(std::move(*buf));
    }
    return std::unique_ptr<UploadRing>(std::make_unique<UploadRingImpl>(
        this, lifetimeGuard, desc, segmentBytes, std::move(buffers)));
}

Result<std::unique_ptr<ReadbackPool>> Device::createReadbackPool(ReadbackPoolDesc desc) {
    if (auto status = checkStatus(); !status) return std::unexpected(status.error());
    if (auto valid = validateReadbackPoolDesc(desc); !valid) return std::unexpected(valid.error());
    if (auto q = queue(desc.queue); !q) return std::unexpected(q.error());
    if (!lifetimeGuard) lifetimeGuard = std::make_shared<bool>(true);
    return std::unique_ptr<ReadbackPool>(std::make_unique<ReadbackPoolImpl>(this, lifetimeGuard, desc));
}

Result<std::unique_ptr<TransientResourcePool>> Device::createTransientResourcePool(TransientPoolDesc desc) {
    if (auto status = checkStatus(); !status) return std::unexpected(status.error());
    if (auto valid = validateTransientPoolDesc(desc); !valid) return std::unexpected(valid.error());
    if (auto q = queue(desc.queue); !q) return std::unexpected(q.error());
    if (!lifetimeGuard) lifetimeGuard = std::make_shared<bool>(true);
    return std::unique_ptr<TransientResourcePool>(std::make_unique<TransientResourcePoolImpl>(this, lifetimeGuard, desc));
}

Result<CachedGraphicsPipeline> Device::createCachedGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    if (auto status = checkStatus(); !status) return std::unexpected(status.error());
    if (graphicsCache) {
        auto& entries = graphicsCache->entries;
        const auto found = std::ranges::find(entries, desc, &GraphicsCache::Entry::desc);
        if (found != entries.end()) {
            entries.splice(entries.end(), entries, found);
            return CachedGraphicsPipeline{found->pipeline, true};
        }
    }
    auto pipeline = createGraphicsPipeline(desc);
    if (!pipeline) return std::unexpected(pipeline.error());
    if (cacheableKey(desc)) {
        if (!graphicsCache) graphicsCache = std::make_unique<GraphicsCache>();
        auto& entries = graphicsCache->entries;
        entries.push_back({desc, *pipeline});
        if (entries.size() > 8) entries.pop_front();
    }
    return CachedGraphicsPipeline{std::move(*pipeline), false};
}

Result<void> Device::clearGraphicsPipelineCache() {
    if (auto status = checkStatus(); !status) return status;
    graphicsCache.reset();
    return {};
}

namespace {
bool sameBufferDesc(const BufferDesc& a, const BufferDesc& b) noexcept {
    return a.size == b.size && a.memory == b.memory && a.storage == b.storage;
}

bool isHostBufferMemory(Memory memory) noexcept {
    return memory == Memory::Upload || memory == Memory::Readback;
}

bool validHostBufferState(Memory memory, ResourceState state) noexcept {
    if (memory == Memory::Upload) {
        return state == ResourceState::CopySource ||
               state == ResourceState::VertexBuffer ||
               state == ResourceState::IndexBuffer ||
               state == ResourceState::VertexAndIndexBuffer ||
               state == ResourceState::ConstantBuffer;
    }
    if (memory == Memory::Readback) {
        return state == ResourceState::CopyDestination;
    }
    return true;
}

bool validQueueState(QueueType queue, ResourceState state, bool isTexture) noexcept {
    if (queue == QueueType::Copy) {
        return state == ResourceState::Common ||
               state == ResourceState::CopySource ||
               state == ResourceState::CopyDestination;
    }
    if (queue == QueueType::Compute) {
        if (isTexture) {
            return state != ResourceState::RenderTarget &&
                   state != ResourceState::DepthWrite &&
                   state != ResourceState::DepthRead &&
                   state != ResourceState::Present &&
                   state != ResourceState::ResolveSource &&
                   state != ResourceState::ResolveDestination;
        }
        return state != ResourceState::VertexBuffer &&
               state != ResourceState::IndexBuffer &&
               state != ResourceState::VertexAndIndexBuffer;
    }
    return true;
}

const char* queueTypeName(QueueType queue) noexcept {
    switch (queue) {
    case QueueType::Graphics: return "Graphics";
    case QueueType::Compute: return "Compute";
    case QueueType::Copy: return "Copy";
    }
    return "Unknown";
}

const char* resourceStateName(ResourceState state) noexcept {
    switch (state) {
    case ResourceState::Common: return "Common";
    case ResourceState::CopySource: return "CopySource";
    case ResourceState::CopyDestination: return "CopyDestination";
    case ResourceState::VertexBuffer: return "VertexBuffer";
    case ResourceState::IndexBuffer: return "IndexBuffer";
    case ResourceState::VertexAndIndexBuffer: return "VertexAndIndexBuffer";
    case ResourceState::ConstantBuffer: return "ConstantBuffer";
    case ResourceState::RenderTarget: return "RenderTarget";
    case ResourceState::ShaderRead: return "ShaderRead";
    case ResourceState::DepthWrite: return "DepthWrite";
    case ResourceState::DepthRead: return "DepthRead";
    case ResourceState::Present: return "Present";
    case ResourceState::UnorderedAccess: return "UnorderedAccess";
    case ResourceState::ResolveSource: return "ResolveSource";
    case ResourceState::ResolveDestination: return "ResolveDestination";
    }
    return "Unknown";
}

std::string escapeDotLabel(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}
} // namespace

struct RenderGraph::Impl {
    enum class ResourceKind { Buffer, Texture };

    struct ResourceNode {
        std::uint32_t id{}; // 1-based handle id
        std::string name;
        ResourceKind kind{ResourceKind::Buffer};
        bool imported{};
        BufferDesc bufferDesc{};
        TextureDesc textureDesc{};
        BufferPtr importedBuffer;
        TexturePtr importedTexture;
        ResourceState initialState{ResourceState::Common};
        std::optional<ResourceState> finalState;

        bool live{};
        std::uint32_t firstExec{UINT32_MAX};
        std::uint32_t lastExec{};
        std::uint32_t physicalSlot{UINT32_MAX};
    };

    struct PassNode {
        std::uint32_t index{};
        RGPassDesc desc;
        bool live{};
        std::uint32_t execOrder{UINT32_MAX};
        std::uint32_t batchIndex{UINT32_MAX};
    };

    struct PhysicalSlot {
        std::uint32_t index{};
        ResourceKind kind{ResourceKind::Buffer};
        bool imported{};
        std::uint32_t importedResourceIndex{UINT32_MAX};
        BufferDesc bufferDesc{};
        TextureDesc textureDesc{};
        ResourceState initialState{ResourceState::Common};
        std::optional<ResourceState> finalState;
        std::uint32_t lastExec{};
        std::uint32_t lastPassIndex{UINT32_MAX};
        std::vector<std::uint32_t> assignedResources;
    };

    struct PlannedBarrier {
        std::uint32_t physicalSlot{};
        RGResourceHandle resource{};
        ResourceState before{ResourceState::Common};
        ResourceState after{ResourceState::Common};
        bool isUavBarrier{};
    };

    struct PlannedPass {
        std::uint32_t passIndex{};
        std::uint32_t execOrder{};
        std::vector<PlannedBarrier> preBarriers;
        std::vector<PlannedBarrier> postBarriers;
        std::vector<std::uint32_t> declaredResources; // 0-based resource indices
    };

    struct BatchWait {
        std::uint32_t producerBatch{};
        QueueType producerQueue{QueueType::Graphics};
        std::uint64_t relativeValue{};
        bool operator==(const BatchWait&) const = default;
    };

    struct PlannedBatch {
        std::uint32_t index{};
        QueueType queue{QueueType::Graphics};
        std::vector<PlannedPass> passes;
        std::vector<BatchWait> waits;
        std::uint64_t signalTimelineValue{};
        bool hasCrossQueueConsumers{};
        bool isLastOnQueue{};
    };

    struct QueueExecPool {
        std::vector<std::unique_ptr<CommandPool>> pools;
        std::vector<std::unique_ptr<CommandList>> lists;
    };

    struct ExecCache {
        Device* devicePtr{};
        std::weak_ptr<bool> deviceGuard;
        std::array<QueueExecPool, 3> queues;
        // GPU-to-GPU cross-queue sync timelines (never host-waited) and per-queue host completion timelines.
        std::vector<TimelineFencePtr> syncTimelines;
        std::vector<std::uint64_t> syncCursors;
        std::array<TimelineFencePtr, 3> hostTimelines;
        std::array<std::uint64_t, 3> hostCursors{};

        void clear() {
            for (auto& q : queues) {
                q.lists.clear();
                q.pools.clear();
            }
            syncTimelines.clear();
            syncCursors.clear();
            for (auto& t : hostTimelines) t.reset();
            hostCursors = {0, 0, 0};
            devicePtr = nullptr;
            deviceGuard.reset();
        }
    };

    class PassResourceResolver final : public RenderGraphResources {
        const std::vector<ResourceNode>& resources;
        const std::vector<BufferPtr>& slotBuffers;
        const std::vector<TexturePtr>& slotTextures;
        const std::vector<std::uint32_t>& allowedResources;

        Result<std::uint32_t> lookup(RGResourceHandle handle, ResourceKind expectedKind) const {
            if (!handle || handle.id > resources.size()) {
                return std::unexpected(Error{ErrorCode::InvalidArgument, "Invalid RenderGraph resource handle"});
            }
            const std::uint32_t resIdx = handle.id - 1u;
            if (std::ranges::find(allowedResources, resIdx) == allowedResources.end()) {
                return std::unexpected(Error{ErrorCode::InvalidArgument,
                    "Resource was not declared in this pass's reads or writes"});
            }
            const auto& node = resources[resIdx];
            if (!node.live || node.physicalSlot == UINT32_MAX) {
                return std::unexpected(Error{ErrorCode::InvalidState, "Resource is not active in the compiled graph"});
            }
            if (node.kind != expectedKind) {
                return std::unexpected(Error{ErrorCode::InvalidArgument,
                    expectedKind == ResourceKind::Buffer
                        ? "RenderGraph handle refers to a texture, not a buffer"
                        : "RenderGraph handle refers to a buffer, not a texture"});
            }
            return node.physicalSlot;
        }

    public:
        PassResourceResolver(
            const std::vector<ResourceNode>& res,
            const std::vector<BufferPtr>& bufs,
            const std::vector<TexturePtr>& texs,
            const std::vector<std::uint32_t>& allowed)
            : resources(res), slotBuffers(bufs), slotTextures(texs), allowedResources(allowed) {}

        Result<BufferPtr> buffer(RGResourceHandle handle) const override {
            auto slot = lookup(handle, ResourceKind::Buffer);
            if (!slot) return std::unexpected(slot.error());
            if (*slot >= slotBuffers.size() || !slotBuffers[*slot]) {
                return std::unexpected(Error{ErrorCode::InvalidState, "Physical buffer slot is not bound"});
            }
            return slotBuffers[*slot];
        }

        Result<TexturePtr> texture(RGResourceHandle handle) const override {
            auto slot = lookup(handle, ResourceKind::Texture);
            if (!slot) return std::unexpected(slot.error());
            if (*slot >= slotTextures.size() || !slotTextures[*slot]) {
                return std::unexpected(Error{ErrorCode::InvalidState, "Physical texture slot is not bound"});
            }
            return slotTextures[*slot];
        }
    };

    std::vector<ResourceNode> resources;
    std::vector<PassNode> passes;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> compiledEdges;
    std::vector<PhysicalSlot> slots;
    std::vector<PlannedBatch> batches;
    std::vector<RGBarrierInfo> barriers;
    std::vector<RGSyncEdge> syncEdges;
    RGCompileStats stats{};
    bool compiled{};
    ExecCache execCache;

    void invalidateCompiled() {
        compiled = false;
        compiledEdges.clear();
        slots.clear();
        batches.clear();
        barriers.clear();
        syncEdges.clear();
        stats = {};
    }

    Result<void> ensureExecCache(Device& device, bool needTimeline, std::size_t batchCount) {
        if (auto status = device.checkStatus(); !status) {
            execCache.clear();
            return std::unexpected(status.error());
        }
        if (!device.lifetimeGuard) {
            device.lifetimeGuard = std::make_shared<bool>(true);
        }
        const auto guard = execCache.deviceGuard.lock();
        if (execCache.devicePtr != &device || !guard || !*guard || guard != device.lifetimeGuard) {
            execCache.clear();
            execCache.devicePtr = &device;
            execCache.deviceGuard = device.lifetimeGuard;
        }
        if (needTimeline) {
            for (std::size_t qi = 0; qi < 3; ++qi) {
                if (!execCache.hostTimelines[qi] || execCache.hostCursors[qi] > (std::numeric_limits<std::uint64_t>::max() - 1024u)) {
                    auto tf = device.createTimelineFence(0);
                    if (!tf) return std::unexpected(tf.error());
                    execCache.hostTimelines[qi] = std::move(*tf);
                    execCache.hostCursors[qi] = 0;
                }
            }
            if (execCache.syncTimelines.size() < batchCount) {
                const std::size_t prev = execCache.syncTimelines.size();
                execCache.syncTimelines.resize(batchCount);
                execCache.syncCursors.resize(batchCount, 0);
                for (std::size_t i = prev; i < batchCount; ++i) {
                    auto tf = device.createTimelineFence(0);
                    if (!tf) return std::unexpected(tf.error());
                    execCache.syncTimelines[i] = std::move(*tf);
                }
            }
            for (std::size_t i = 0; i < batchCount; ++i) {
                if (execCache.syncCursors[i] > (std::numeric_limits<std::uint64_t>::max() - 1024u)) {
                    auto tf = device.createTimelineFence(0);
                    if (!tf) return std::unexpected(tf.error());
                    execCache.syncTimelines[i] = std::move(*tf);
                    execCache.syncCursors[i] = 0;
                }
            }
        }
        return {};
    }

    Result<CommandList*> acquireBatchCommandList(Device& device, QueueType queue, std::size_t slotIndex) {
        const std::size_t qi = static_cast<std::size_t>(queue);
        auto& qPool = execCache.queues[qi];
        while (qPool.pools.size() <= slotIndex) {
            auto pool = device.createCommandPool(queue);
            if (!pool) return std::unexpected(pool.error());
            auto cmd = (*pool)->createCommandList();
            if (!cmd) return std::unexpected(cmd.error());
            qPool.pools.push_back(std::move(*pool));
            qPool.lists.push_back(std::move(*cmd));
        }
        if (auto ok = qPool.pools[slotIndex]->reset(); !ok) {
            return std::unexpected(ok.error());
        }
        return qPool.lists[slotIndex].get();
    }
};

RenderGraph::RenderGraph() : impl(std::make_unique<Impl>()) {}
RenderGraph::~RenderGraph() = default;
RenderGraph::RenderGraph(RenderGraph&&) noexcept = default;
RenderGraph& RenderGraph::operator=(RenderGraph&&) noexcept = default;

Result<RGResourceHandle> RenderGraph::createBuffer(std::string name, BufferDesc desc) {
    if (name.empty()) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "RenderGraph buffer name must not be empty"});
    }
    if (!desc.size || desc.size > (std::uint64_t{1} << 62)) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "RenderGraph transient buffer requires positive size within bounds"});
    }
    if (desc.memory != Memory::Device) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "RenderGraph transient buffers must use Memory::Device"});
    }
    impl->invalidateCompiled();
    const std::uint32_t id = static_cast<std::uint32_t>(impl->resources.size() + 1u);
    Impl::ResourceNode node{};
    node.id = id;
    node.name = std::move(name);
    node.kind = Impl::ResourceKind::Buffer;
    node.imported = false;
    node.bufferDesc = desc;
    node.initialState = ResourceState::Common;
    impl->resources.push_back(std::move(node));
    return RGResourceHandle{id};
}

Result<RGResourceHandle> RenderGraph::createTexture(std::string name, TextureDesc desc) {
    if (name.empty()) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "RenderGraph texture name must not be empty"});
    }
    if (!desc.width || !desc.height) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "RenderGraph transient texture requires positive dimensions"});
    }
    impl->invalidateCompiled();
    const std::uint32_t id = static_cast<std::uint32_t>(impl->resources.size() + 1u);
    Impl::ResourceNode node{};
    node.id = id;
    node.name = std::move(name);
    node.kind = Impl::ResourceKind::Texture;
    node.imported = false;
    node.textureDesc = desc;
    node.initialState = ResourceState::Common;
    impl->resources.push_back(std::move(node));
    return RGResourceHandle{id};
}

Result<RGResourceHandle> RenderGraph::importBuffer(
    std::string name,
    BufferPtr buffer,
    std::optional<ResourceState> initialState,
    std::optional<ResourceState> finalState) {
    if (name.empty()) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "RenderGraph imported buffer name must not be empty"});
    }
    if (!buffer) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "RenderGraph::importBuffer requires a non-null buffer"});
    }
    const auto bDesc = buffer->description();
    const ResourceState resolvedInitial = initialState.value_or(
        bDesc.memory == Memory::Upload ? ResourceState::CopySource
        : bDesc.memory == Memory::Readback ? ResourceState::CopyDestination
        : ResourceState::Common);
    if (!validHostBufferState(bDesc.memory, resolvedInitial)) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "Invalid initialState for imported host buffer"});
    }
    if (finalState.has_value() && !validHostBufferState(bDesc.memory, *finalState)) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "Invalid finalState for imported host buffer"});
    }
    impl->invalidateCompiled();
    const std::uint32_t id = static_cast<std::uint32_t>(impl->resources.size() + 1u);
    Impl::ResourceNode node{};
    node.id = id;
    node.name = std::move(name);
    node.kind = Impl::ResourceKind::Buffer;
    node.imported = true;
    node.bufferDesc = bDesc;
    node.importedBuffer = std::move(buffer);
    node.initialState = resolvedInitial;
    node.finalState = finalState;
    impl->resources.push_back(std::move(node));
    return RGResourceHandle{id};
}

Result<RGResourceHandle> RenderGraph::importTexture(
    std::string name,
    TexturePtr texture,
    std::optional<ResourceState> initialState,
    std::optional<ResourceState> finalState) {
    if (name.empty()) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "RenderGraph imported texture name must not be empty"});
    }
    if (!texture) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "RenderGraph::importTexture requires a non-null texture"});
    }
    impl->invalidateCompiled();
    const std::uint32_t id = static_cast<std::uint32_t>(impl->resources.size() + 1u);
    Impl::ResourceNode node{};
    node.id = id;
    node.name = std::move(name);
    node.kind = Impl::ResourceKind::Texture;
    node.imported = true;
    node.textureDesc = texture->description();
    node.importedTexture = std::move(texture);
    node.initialState = initialState.value_or(ResourceState::Common);
    node.finalState = finalState;
    impl->resources.push_back(std::move(node));
    return RGResourceHandle{id};
}

Result<std::uint32_t> RenderGraph::addPass(RGPassDesc desc) {
    if (desc.name.empty()) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "RenderGraph pass name must not be empty"});
    }
    if (!validQueueType(desc.queue)) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "Unknown RenderGraph pass QueueType"});
    }
    if (!desc.execute) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "RenderGraph pass requires an execute callback"});
    }

    std::unordered_map<std::uint32_t, ResourceState> seenStates;
    const auto validateUse = [&](const RGResourceUse& use, bool isWrite) -> Result<void> {
        if (!use.resource || use.resource.id > impl->resources.size()) {
            return std::unexpected(Error{ErrorCode::InvalidArgument, "RenderGraph pass references an unknown resource handle"});
        }
        const auto& res = impl->resources[use.resource.id - 1u];
        const bool isTex = (res.kind == Impl::ResourceKind::Texture);
        if (!validQueueState(desc.queue, use.state, isTex)) {
            return std::unexpected(Error{ErrorCode::InvalidArgument, "ResourceState is incompatible with pass QueueType"});
        }
        if (!isTex) {
            if (!validHostBufferState(res.bufferDesc.memory, use.state)) {
                return std::unexpected(Error{ErrorCode::InvalidArgument, "ResourceState is incompatible with host buffer memory"});
            }
            if (isWrite && res.bufferDesc.memory == Memory::Upload) {
                return std::unexpected(Error{ErrorCode::InvalidArgument, "RenderGraph pass cannot write to an Upload buffer"});
            }
            if (!isWrite && res.bufferDesc.memory == Memory::Readback) {
                return std::unexpected(Error{ErrorCode::InvalidArgument, "RenderGraph pass cannot read from a Readback buffer"});
            }
            if (use.state == ResourceState::UnorderedAccess && !res.bufferDesc.storage) {
                return std::unexpected(Error{ErrorCode::InvalidArgument, "UnorderedAccess requires a storage buffer"});
            }
        } else {
            if (use.state == ResourceState::UnorderedAccess && !res.textureDesc.storage) {
                return std::unexpected(Error{ErrorCode::InvalidArgument, "UnorderedAccess requires a storage texture"});
            }
            if (use.state == ResourceState::RenderTarget && !res.textureDesc.renderTarget) {
                return std::unexpected(Error{ErrorCode::InvalidArgument, "RenderTarget state requires a render target texture"});
            }
            if ((use.state == ResourceState::DepthWrite || use.state == ResourceState::DepthRead) && !res.textureDesc.depthStencil) {
                return std::unexpected(Error{ErrorCode::InvalidArgument, "Depth state requires a depth-stencil texture"});
            }
        }
        auto it = seenStates.find(use.resource.id);
        if (it != seenStates.end() && it->second != use.state) {
            return std::unexpected(Error{ErrorCode::InvalidArgument,
                "RenderGraph pass declares conflicting states for the same resource"});
        }
        seenStates[use.resource.id] = use.state;
        return {};
    };

    for (const auto& r : desc.reads) {
        if (auto ok = validateUse(r, false); !ok) return std::unexpected(ok.error());
    }
    for (const auto& w : desc.writes) {
        if (auto ok = validateUse(w, true); !ok) return std::unexpected(ok.error());
    }

    impl->invalidateCompiled();
    const std::uint32_t passIndex = static_cast<std::uint32_t>(impl->passes.size());
    Impl::PassNode node{};
    node.index = passIndex;
    node.desc = std::move(desc);
    impl->passes.push_back(std::move(node));
    return passIndex;
}

Result<RGCompileStats> RenderGraph::compile() {
    impl->invalidateCompiled();

    const std::uint32_t passCount = static_cast<std::uint32_t>(impl->passes.size());
    const std::uint32_t resCount = static_cast<std::uint32_t>(impl->resources.size());

    for (auto& r : impl->resources) {
        r.live = false;
        r.firstExec = UINT32_MAX;
        r.lastExec = 0;
        r.physicalSlot = UINT32_MAX;
    }
    for (auto& p : impl->passes) {
        p.live = false;
        p.execOrder = UINT32_MAX;
        p.batchIndex = UINT32_MAX;
    }

    // 1. Build dependency DAG from explicit dependsOn and resource read/write hazards (RAW/WAW/WAR).
    std::vector<std::vector<std::uint32_t>> adj(passCount);
    std::vector<std::vector<std::uint32_t>> revAdj(passCount);
    auto addEdge = [&](std::uint32_t u, std::uint32_t v) {
        if (std::ranges::find(adj[u], v) == adj[u].end()) {
            adj[u].push_back(v);
            revAdj[v].push_back(u);
        }
    };

    for (std::uint32_t p = 0; p < passCount; ++p) {
        for (const std::uint32_t dep : impl->passes[p].desc.dependsOn) {
            if (dep >= passCount) {
                return std::unexpected(Error{ErrorCode::InvalidArgument, "RenderGraph pass dependsOn index out of range"});
            }
            if (dep == p) {
                return std::unexpected(Error{ErrorCode::InvalidState, "RenderGraph contains a self-dependency cycle"});
            }
            addEdge(dep, p);
        }
    }

    std::vector<std::optional<std::uint32_t>> lastWriter(resCount);
    std::vector<std::vector<std::uint32_t>> readersSinceWriter(resCount);

    for (std::uint32_t p = 0; p < passCount; ++p) {
        const auto& pass = impl->passes[p].desc;
        std::vector<std::uint32_t> writeSet;
        writeSet.reserve(pass.writes.size());
        for (const auto& w : pass.writes) {
            const std::uint32_t rIdx = w.resource.id - 1u;
            if (std::ranges::find(writeSet, rIdx) == writeSet.end()) writeSet.push_back(rIdx);
        }

        // Reads (excluding resources also written in the same pass, which are handled as RMW below).
        for (const auto& r : pass.reads) {
            const std::uint32_t rIdx = r.resource.id - 1u;
            if (std::ranges::find(writeSet, rIdx) != writeSet.end()) continue;
            if (lastWriter[rIdx].has_value() && *lastWriter[rIdx] != p) {
                addEdge(*lastWriter[rIdx], p);
            }
            if (std::ranges::find(readersSinceWriter[rIdx], p) == readersSinceWriter[rIdx].end()) {
                readersSinceWriter[rIdx].push_back(p);
            }
        }

        // Writes (WAW + WAR).
        for (const std::uint32_t rIdx : writeSet) {
            if (lastWriter[rIdx].has_value() && *lastWriter[rIdx] != p) {
                addEdge(*lastWriter[rIdx], p);
            }
            for (const std::uint32_t reader : readersSinceWriter[rIdx]) {
                if (reader != p) addEdge(reader, p);
            }
            lastWriter[rIdx] = p;
            readersSinceWriter[rIdx].clear();
        }
    }

    // 2. Kahn's topological sort with deterministic declaration-order tie-breaking + cycle detection.
    std::vector<std::uint32_t> inDegree(passCount, 0);
    for (std::uint32_t u = 0; u < passCount; ++u) {
        for (const std::uint32_t v : adj[u]) ++inDegree[v];
    }
    std::priority_queue<std::uint32_t, std::vector<std::uint32_t>, std::greater<std::uint32_t>> ready;
    for (std::uint32_t p = 0; p < passCount; ++p) {
        if (inDegree[p] == 0) ready.push(p);
    }
    std::vector<std::uint32_t> topoOrder;
    topoOrder.reserve(passCount);
    while (!ready.empty()) {
        const std::uint32_t u = ready.top();
        ready.pop();
        topoOrder.push_back(u);
        for (const std::uint32_t v : adj[u]) {
            if (--inDegree[v] == 0) ready.push(v);
        }
    }
    if (topoOrder.size() != passCount) {
        return std::unexpected(Error{ErrorCode::InvalidState, "RenderGraph contains a dependency cycle"});
    }

    // 3. Dead pass culling via reverse BFS from sideEffect passes and passes writing imported resources.
    std::queue<std::uint32_t> liveQueue;
    for (std::uint32_t p = 0; p < passCount; ++p) {
        bool isRoot = impl->passes[p].desc.sideEffect;
        if (!isRoot) {
            for (const auto& w : impl->passes[p].desc.writes) {
                if (impl->resources[w.resource.id - 1u].imported) {
                    isRoot = true;
                    break;
                }
            }
        }
        if (isRoot) {
            impl->passes[p].live = true;
            liveQueue.push(p);
        }
    }
    while (!liveQueue.empty()) {
        const std::uint32_t curr = liveQueue.front();
        liveQueue.pop();
        for (const std::uint32_t pred : revAdj[curr]) {
            if (!impl->passes[pred].live) {
                impl->passes[pred].live = true;
                liveQueue.push(pred);
            }
        }
    }

    std::vector<std::uint32_t> execPasses;
    execPasses.reserve(passCount);
    for (const std::uint32_t p : topoOrder) {
        if (impl->passes[p].live) {
            impl->passes[p].execOrder = static_cast<std::uint32_t>(execPasses.size());
            execPasses.push_back(p);
        }
    }

    // Record live DAG edges for diagnostics and cross-queue sync planning.
    for (std::uint32_t u = 0; u < passCount; ++u) {
        if (!impl->passes[u].live) continue;
        for (const std::uint32_t v : adj[u]) {
            if (impl->passes[v].live) {
                impl->compiledEdges.emplace_back(u, v);
            }
        }
    }

    // 4. Compute liveness intervals [firstExec, lastExec] for resources used by surviving passes.
    for (std::uint32_t execIdx = 0; execIdx < execPasses.size(); ++execIdx) {
        const auto& pass = impl->passes[execPasses[execIdx]].desc;
        auto mark = [&](const RGResourceUse& use) {
            auto& res = impl->resources[use.resource.id - 1u];
            res.live = true;
            res.firstExec = std::min(res.firstExec, execIdx);
            res.lastExec = std::max(res.lastExec, execIdx);
        };
        for (const auto& r : pass.reads) mark(r);
        for (const auto& w : pass.writes) mark(w);
    }

    // Validate finalState compatibility on last pass for imported resources.
    for (std::uint32_t rIdx = 0; rIdx < resCount; ++rIdx) {
        const auto& res = impl->resources[rIdx];
        if (res.imported && res.live && res.finalState.has_value()) {
            const QueueType lastQueue = impl->passes[execPasses[res.lastExec]].desc.queue;
            if (!validQueueState(lastQueue, *res.finalState, res.kind == Impl::ResourceKind::Texture)) {
                return std::unexpected(Error{ErrorCode::InvalidArgument,
                    "Imported resource finalState is incompatible with its last pass QueueType"});
            }
        }
    }

    // 5. Group consecutive surviving passes with the same QueueType into queue batches.
    for (std::uint32_t execIdx = 0; execIdx < execPasses.size(); ++execIdx) {
        const std::uint32_t passIdx = execPasses[execIdx];
        const QueueType q = impl->passes[passIdx].desc.queue;
        if (impl->batches.empty() || impl->batches.back().queue != q) {
            Impl::PlannedBatch batch{};
            batch.index = static_cast<std::uint32_t>(impl->batches.size());
            batch.queue = q;
            impl->batches.push_back(std::move(batch));
        }
        impl->passes[passIdx].batchIndex = impl->batches.back().index;
        Impl::PlannedPass pp{};
        pp.passIndex = passIdx;
        pp.execOrder = execIdx;
        impl->batches.back().passes.push_back(std::move(pp));
    }

    // 6. Assign physical slots: 1:1 for live imported resources, greedy non-overlapping aliasing for live transients.
    std::uint32_t importedCount = 0;
    std::uint32_t transientDeclared = 0;
    std::uint32_t transientLive = 0;
    std::uint32_t transientCulled = 0;

    std::vector<std::uint32_t> liveTransientIndices;
    for (std::uint32_t rIdx = 0; rIdx < resCount; ++rIdx) {
        auto& res = impl->resources[rIdx];
        if (res.imported) {
            ++importedCount;
            if (res.live) {
                const std::uint32_t slotIdx = static_cast<std::uint32_t>(impl->slots.size());
                Impl::PhysicalSlot slot{};
                slot.index = slotIdx;
                slot.kind = res.kind;
                slot.imported = true;
                slot.importedResourceIndex = rIdx;
                slot.bufferDesc = res.bufferDesc;
                slot.textureDesc = res.textureDesc;
                slot.initialState = res.initialState;
                slot.finalState = res.finalState;
                slot.lastExec = res.lastExec;
                slot.lastPassIndex = execPasses[res.lastExec];
                slot.assignedResources.push_back(rIdx);
                impl->slots.push_back(std::move(slot));
                res.physicalSlot = slotIdx;
            }
        } else {
            ++transientDeclared;
            if (res.live) {
                ++transientLive;
                liveTransientIndices.push_back(rIdx);
            } else {
                ++transientCulled;
            }
        }
    }

    std::ranges::sort(liveTransientIndices, [&](std::uint32_t a, std::uint32_t b) {
        const auto& ra = impl->resources[a];
        const auto& rb = impl->resources[b];
        if (ra.firstExec != rb.firstExec) return ra.firstExec < rb.firstExec;
        if (ra.lastExec != rb.lastExec) return ra.lastExec < rb.lastExec;
        return a < b;
    });

    std::uint32_t physicalTransientSlots = 0;
    for (const std::uint32_t rIdx : liveTransientIndices) {
        auto& res = impl->resources[rIdx];
        std::uint32_t chosenSlot = UINT32_MAX;
        for (auto& slot : impl->slots) {
            if (slot.imported) continue;
            if (slot.kind != res.kind) continue;
            if (slot.lastExec >= res.firstExec) continue;
            const bool descMatch = (res.kind == Impl::ResourceKind::Texture)
                ? sameTextureDesc(slot.textureDesc, res.textureDesc)
                : sameBufferDesc(slot.bufferDesc, res.bufferDesc);
            if (!descMatch) continue;
            chosenSlot = slot.index;
            break;
        }
        if (chosenSlot == UINT32_MAX) {
            chosenSlot = static_cast<std::uint32_t>(impl->slots.size());
            Impl::PhysicalSlot slot{};
            slot.index = chosenSlot;
            slot.kind = res.kind;
            slot.imported = false;
            slot.bufferDesc = res.bufferDesc;
            slot.textureDesc = res.textureDesc;
            slot.initialState = ResourceState::Common;
            slot.lastExec = res.lastExec;
            slot.lastPassIndex = execPasses[res.lastExec];
            slot.assignedResources.push_back(rIdx);
            impl->slots.push_back(std::move(slot));
            ++physicalTransientSlots;
        } else {
            auto& slot = impl->slots[chosenSlot];
            // Add an implicit memory-reuse dependency from the slot's previous last pass to the new owner's first pass.
            const std::uint32_t prevPass = slot.lastPassIndex;
            const std::uint32_t nextPass = execPasses[res.firstExec];
            if (prevPass != UINT32_MAX && prevPass != nextPass) {
                if (std::ranges::find(impl->compiledEdges, std::pair{prevPass, nextPass}) == impl->compiledEdges.end()) {
                    impl->compiledEdges.emplace_back(prevPass, nextPass);
                }
            }
            slot.lastExec = res.lastExec;
            slot.lastPassIndex = execPasses[res.lastExec];
            slot.assignedResources.push_back(rIdx);
        }
        res.physicalSlot = chosenSlot;
    }

    // 7. Plan cross-queue timeline sync edges between batches (dedicated GPU sync timelines + per-queue host completion).
    std::array<std::uint64_t, 3> nextSignalPerQueue{1, 1, 1};
    std::vector<std::pair<std::uint32_t, std::uint32_t>> batchCrossDeps;
    for (const auto& [u, v] : impl->compiledEdges) {
        const std::uint32_t bu = impl->passes[u].batchIndex;
        const std::uint32_t bv = impl->passes[v].batchIndex;
        if (bu < bv && impl->batches[bu].queue != impl->batches[bv].queue) {
            if (std::ranges::find(batchCrossDeps, std::pair{bu, bv}) == batchCrossDeps.end()) {
                batchCrossDeps.emplace_back(bu, bv);
            }
        }
    }
    std::ranges::sort(batchCrossDeps);
    for (const auto& [bu, bv] : batchCrossDeps) {
        auto& prod = impl->batches[bu];
        auto& cons = impl->batches[bv];
        const std::size_t pqi = static_cast<std::size_t>(prod.queue);
        if (prod.signalTimelineValue == 0) {
            prod.signalTimelineValue = nextSignalPerQueue[pqi]++;
        }
        prod.hasCrossQueueConsumers = true;
        const Impl::BatchWait bw{bu, prod.queue, prod.signalTimelineValue};
        if (std::ranges::find(cons.waits, bw) == cons.waits.end()) {
            cons.waits.push_back(bw);
        }
        impl->syncEdges.push_back(RGSyncEdge{
            bu, bv, prod.queue, cons.queue, prod.signalTimelineValue
        });
    }
    if (impl->batches.size() > 1u) {
        for (std::uint32_t b = 0; b < impl->batches.size(); ++b) {
            bool hasLaterSameQueue = false;
            for (std::uint32_t later = b + 1u; later < impl->batches.size(); ++later) {
                if (impl->batches[later].queue == impl->batches[b].queue) {
                    hasLaterSameQueue = true;
                    break;
                }
            }
            if (!hasLaterSameQueue) {
                impl->batches[b].isLastOnQueue = true;
                if (impl->batches[b].signalTimelineValue == 0) {
                    const std::size_t pqi = static_cast<std::size_t>(impl->batches[b].queue);
                    impl->batches[b].signalTimelineValue = nextSignalPerQueue[pqi]++;
                }
            }
        }
    }

    // 8. Automatic barrier & cross-queue Common release planning per physical slot.
    struct SlotUseRecord {
        std::uint32_t execOrder{};
        std::uint32_t passIndex{};
        std::uint32_t resourceIndex{};
        QueueType queue{QueueType::Graphics};
        ResourceState state{ResourceState::Common};
        bool isWrite{};
    };
    const std::size_t slotCount = impl->slots.size();
    std::vector<std::vector<SlotUseRecord>> slotUses(slotCount);

    for (auto& batch : impl->batches) {
        for (auto& pp : batch.passes) {
            const auto& pass = impl->passes[pp.passIndex].desc;
            struct LocalUse {
                std::uint32_t resIdx{};
                ResourceState state{ResourceState::Common};
                bool isWrite{};
            };
            std::vector<LocalUse> merged;
            auto addLocal = [&](const RGResourceUse& use, bool isWrite) {
                const std::uint32_t resIdx = use.resource.id - 1u;
                auto it = std::ranges::find(merged, resIdx, &LocalUse::resIdx);
                if (it == merged.end()) {
                    merged.push_back(LocalUse{resIdx, use.state, isWrite});
                } else {
                    it->isWrite = it->isWrite || isWrite;
                }
            };
            for (const auto& r : pass.reads) addLocal(r, false);
            for (const auto& w : pass.writes) addLocal(w, true);

            pp.declaredResources.reserve(merged.size());
            for (const auto& lu : merged) {
                pp.declaredResources.push_back(lu.resIdx);
                const std::uint32_t slotIdx = impl->resources[lu.resIdx].physicalSlot;
                slotUses[slotIdx].push_back(SlotUseRecord{
                    pp.execOrder, pp.passIndex, lu.resIdx, batch.queue, lu.state, lu.isWrite
                });
            }
        }
    }

    std::vector<ResourceState> slotState(slotCount, ResourceState::Common);
    std::vector<bool> slotLastUavWrite(slotCount, false);
    for (std::size_t s = 0; s < slotCount; ++s) {
        slotState[s] = impl->slots[s].initialState;
    }

    std::vector<std::size_t> slotCursor(slotCount, 0);
    std::uint32_t insertedBarriers = 0;
    std::uint32_t insertedUavBarriers = 0;

    for (auto& batch : impl->batches) {
        for (auto& pp : batch.passes) {
            std::vector<std::uint32_t> touchedSlots;
            touchedSlots.reserve(pp.declaredResources.size());
            for (const std::uint32_t resIdx : pp.declaredResources) {
                const std::uint32_t s = impl->resources[resIdx].physicalSlot;
                const auto& uses = slotUses[s];
                const std::size_t cur = slotCursor[s];
                const auto& u = uses[cur];
                touchedSlots.push_back(s);

                if (slotState[s] != u.state) {
                    pp.preBarriers.push_back(Impl::PlannedBarrier{
                        s, RGResourceHandle{resIdx + 1u}, slotState[s], u.state, false
                    });
                    impl->barriers.push_back(RGBarrierInfo{
                        pp.passIndex, pp.execOrder, RGResourceHandle{resIdx + 1u}, s,
                        slotState[s], u.state, false, false
                    });
                    slotState[s] = u.state;
                    slotLastUavWrite[s] = (u.state == ResourceState::UnorderedAccess && u.isWrite);
                    ++insertedBarriers;
                } else if (u.state == ResourceState::UnorderedAccess && slotLastUavWrite[s]) {
                    pp.preBarriers.push_back(Impl::PlannedBarrier{
                        s, RGResourceHandle{resIdx + 1u}, u.state, u.state, true
                    });
                    impl->barriers.push_back(RGBarrierInfo{
                        pp.passIndex, pp.execOrder, RGResourceHandle{resIdx + 1u}, s,
                        u.state, u.state, true, false
                    });
                    slotLastUavWrite[s] = u.isWrite;
                    ++insertedUavBarriers;
                } else if (u.state == ResourceState::UnorderedAccess && u.isWrite) {
                    slotLastUavWrite[s] = true;
                }
            }

            // Post-pass release transitions for cross-queue handoff, transient Common normalization, or imported finalState.
            for (const std::uint32_t s : touchedSlots) {
                const auto& slot = impl->slots[s];
                const auto& uses = slotUses[s];
                const std::size_t cur = slotCursor[s]++;
                const auto& u = uses[cur];
                const bool hostBuf = (slot.kind == Impl::ResourceKind::Buffer && isHostBufferMemory(slot.bufferDesc.memory));
                if (hostBuf) continue;

                const bool hasNext = (cur + 1u < uses.size());
                if (hasNext) {
                    const auto& nextUse = uses[cur + 1u];
                    if (nextUse.queue != u.queue && slotState[s] != ResourceState::Common) {
                        pp.postBarriers.push_back(Impl::PlannedBarrier{
                            s, RGResourceHandle{u.resourceIndex + 1u}, slotState[s], ResourceState::Common, false
                        });
                        impl->barriers.push_back(RGBarrierInfo{
                            pp.passIndex, pp.execOrder, RGResourceHandle{u.resourceIndex + 1u}, s,
                            slotState[s], ResourceState::Common, false, true
                        });
                        slotState[s] = ResourceState::Common;
                        slotLastUavWrite[s] = false;
                        ++insertedBarriers;
                    }
                } else {
                    if (!slot.imported) {
                        // Normalize transient physical slots to Common before returning to TransientResourcePool.
                        if (slotState[s] != ResourceState::Common) {
                            pp.postBarriers.push_back(Impl::PlannedBarrier{
                                s, RGResourceHandle{u.resourceIndex + 1u}, slotState[s], ResourceState::Common, false
                            });
                            impl->barriers.push_back(RGBarrierInfo{
                                pp.passIndex, pp.execOrder, RGResourceHandle{u.resourceIndex + 1u}, s,
                                slotState[s], ResourceState::Common, false, true
                            });
                            slotState[s] = ResourceState::Common;
                            slotLastUavWrite[s] = false;
                            ++insertedBarriers;
                        }
                    } else if (slot.finalState.has_value() && slotState[s] != *slot.finalState) {
                        pp.postBarriers.push_back(Impl::PlannedBarrier{
                            s, RGResourceHandle{u.resourceIndex + 1u}, slotState[s], *slot.finalState, false
                        });
                        impl->barriers.push_back(RGBarrierInfo{
                            pp.passIndex, pp.execOrder, RGResourceHandle{u.resourceIndex + 1u}, s,
                            slotState[s], *slot.finalState, false, true
                        });
                        slotState[s] = *slot.finalState;
                        slotLastUavWrite[s] = false;
                        ++insertedBarriers;
                    }
                }
            }
        }
    }

    impl->stats = RGCompileStats{
        passCount,
        static_cast<std::uint32_t>(execPasses.size()),
        passCount - static_cast<std::uint32_t>(execPasses.size()),
        importedCount,
        transientDeclared,
        transientCulled,
        physicalTransientSlots,
        transientLive - physicalTransientSlots,
        insertedBarriers,
        insertedUavBarriers,
        static_cast<std::uint32_t>(impl->batches.size()),
        static_cast<std::uint32_t>(impl->syncEdges.size())
    };
    impl->compiled = true;
    return impl->stats;
}

Result<void> RenderGraph::execute(Device& device, TransientResourcePool* transientPool) {
    if (!impl->compiled) {
        return std::unexpected(Error{ErrorCode::InvalidState, "RenderGraph must be compiled before execute"});
    }
    if (auto status = device.checkStatus(); !status) {
        impl->execCache.clear();
        return std::unexpected(status.error());
    }
    if (impl->batches.empty()) {
        return {};
    }

    const bool multiBatch = (impl->batches.size() > 1u);
    if (auto ok = impl->ensureExecCache(device, multiBatch, impl->batches.size()); !ok) {
        return std::unexpected(ok.error());
    }

    // Acquire physical backing resources for all live slots (imported -> direct, transient -> pool or device).
    const std::size_t slotCount = impl->slots.size();
    std::vector<BufferPtr> slotBuffers(slotCount);
    std::vector<TexturePtr> slotTextures(slotCount);
    std::vector<std::uint32_t> pooledTransientSlots;
    pooledTransientSlots.reserve(slotCount);

    for (std::size_t s = 0; s < slotCount; ++s) {
        const auto& slot = impl->slots[s];
        if (slot.imported) {
            const auto& res = impl->resources[slot.importedResourceIndex];
            if (slot.kind == Impl::ResourceKind::Buffer) slotBuffers[s] = res.importedBuffer;
            else slotTextures[s] = res.importedTexture;
        } else {
            if (slot.kind == Impl::ResourceKind::Buffer) {
                auto buf = transientPool
                    ? transientPool->acquireBuffer(slot.bufferDesc)
                    : device.createBuffer(slot.bufferDesc);
                if (!buf) return std::unexpected(buf.error());
                slotBuffers[s] = std::move(*buf);
            } else {
                auto tex = transientPool
                    ? transientPool->acquireTexture(slot.textureDesc)
                    : device.createTexture(slot.textureDesc);
                if (!tex) return std::unexpected(tex.error());
                slotTextures[s] = std::move(*tex);
            }
            if (transientPool) pooledTransientSlots.push_back(static_cast<std::uint32_t>(s));
        }
    }

    auto emitBarriers = [&](CommandList& cmd, const std::vector<Impl::PlannedBarrier>& list) -> Result<void> {
        for (const auto& b : list) {
            const auto& slot = impl->slots[b.physicalSlot];
            if (b.isUavBarrier) {
                if (slot.kind == Impl::ResourceKind::Buffer) {
                    if (auto ok = cmd.uavBarrier(slotBuffers[b.physicalSlot]); !ok) return ok;
                } else {
                    if (auto ok = cmd.uavBarrier(slotTextures[b.physicalSlot]); !ok) return ok;
                }
            } else {
                if (slot.kind == Impl::ResourceKind::Buffer) {
                    if (auto ok = cmd.transition(slotBuffers[b.physicalSlot], b.after); !ok) return ok;
                } else {
                    if (auto ok = cmd.transition(slotTextures[b.physicalSlot], b.after); !ok) return ok;
                }
            }
        }
        return {};
    };

    std::array<std::uint64_t, 3> hostSignaled{};
    std::array<std::size_t, 3> queueBatchOrdinal{0, 0, 0};
    std::vector<std::uint64_t> batchSyncValue(impl->batches.size(), 0);

    auto waitSubmittedOnError = [&]() {
        if (multiBatch) {
            for (std::size_t qi = 0; qi < 3; ++qi) {
                if (hostSignaled[qi] > 0 && impl->execCache.hostTimelines[qi]) {
                    (void)impl->execCache.hostTimelines[qi]->wait(hostSignaled[qi], std::chrono::seconds(10));
                }
            }
        }
    };

    for (const auto& batch : impl->batches) {
        auto q = device.queue(batch.queue);
        if (!q) {
            waitSubmittedOnError();
            return std::unexpected(q.error());
        }
        const std::size_t qi = static_cast<std::size_t>(batch.queue);
        auto cmdRes = impl->acquireBatchCommandList(device, batch.queue, queueBatchOrdinal[qi]++);
        if (!cmdRes) {
            waitSubmittedOnError();
            return std::unexpected(cmdRes.error());
        }
        CommandList& cmd = **cmdRes;
        if (auto ok = cmd.begin(); !ok) {
            waitSubmittedOnError();
            return std::unexpected(ok.error());
        }

        for (const auto& pp : batch.passes) {
            if (auto ok = emitBarriers(cmd, pp.preBarriers); !ok) {
                waitSubmittedOnError();
                return std::unexpected(ok.error());
            }
            Impl::PassResourceResolver resolver(impl->resources, slotBuffers, slotTextures, pp.declaredResources);
            if (auto ok = impl->passes[pp.passIndex].desc.execute(cmd, resolver); !ok) {
                waitSubmittedOnError();
                return std::unexpected(ok.error());
            }
            if (auto ok = emitBarriers(cmd, pp.postBarriers); !ok) {
                waitSubmittedOnError();
                return std::unexpected(ok.error());
            }
        }

        if (auto ok = cmd.end(); !ok) {
            waitSubmittedOnError();
            return std::unexpected(ok.error());
        }

        if (!multiBatch) {
            auto fence = (*q)->submit(cmd);
            if (!fence) return std::unexpected(fence.error());
            if (auto ok = (*q)->wait(*fence); !ok) return std::unexpected(ok.error());
        } else {
            std::vector<TimelineWait> waits;
            waits.reserve(batch.waits.size());
            for (const auto& bw : batch.waits) {
                waits.push_back(TimelineWait{
                    impl->execCache.syncTimelines[bw.producerBatch],
                    batchSyncValue[bw.producerBatch]
                });
            }
            std::vector<TimelineSignal> signals;
            if (batch.hasCrossQueueConsumers) {
                const std::uint64_t val = ++impl->execCache.syncCursors[batch.index];
                batchSyncValue[batch.index] = val;
                signals.push_back(TimelineSignal{impl->execCache.syncTimelines[batch.index], val});
            }
            if (batch.isLastOnQueue) {
                const std::uint64_t val = ++impl->execCache.hostCursors[qi];
                hostSignaled[qi] = val;
                signals.push_back(TimelineSignal{impl->execCache.hostTimelines[qi], val});
            }
            CommandList* lists[]{&cmd};
            SubmitDesc submitDesc{lists, waits, signals};
            if (auto ok = (*q)->submit(submitDesc); !ok) {
                waitSubmittedOnError();
                return std::unexpected(ok.error());
            }
        }
    }

    if (multiBatch) {
        for (std::size_t qi = 0; qi < 3; ++qi) {
            if (hostSignaled[qi] > 0) {
                if (auto ok = impl->execCache.hostTimelines[qi]->wait(hostSignaled[qi], std::chrono::seconds(30)); !ok) {
                    return std::unexpected(ok.error());
                }
            }
        }
    }

    if (transientPool) {
        for (const std::uint32_t s : pooledTransientSlots) {
            if (impl->slots[s].kind == Impl::ResourceKind::Buffer) {
                if (auto ok = transientPool->recycle(std::move(slotBuffers[s]), Fence{}); !ok) {
                    return std::unexpected(ok.error());
                }
            } else {
                if (auto ok = transientPool->recycle(std::move(slotTextures[s]), Fence{}); !ok) {
                    return std::unexpected(ok.error());
                }
            }
        }
    }

    return {};
}

void RenderGraph::reset() {
    impl->resources.clear();
    impl->passes.clear();
    impl->invalidateCompiled();
}

void RenderGraph::releaseGpuResources() {
    impl->execCache.clear();
}

bool RenderGraph::isCompiled() const noexcept {
    return impl->compiled;
}

const RGCompileStats& RenderGraph::stats() const noexcept {
    return impl->stats;
}

const std::vector<RGBarrierInfo>& RenderGraph::barriers() const noexcept {
    return impl->barriers;
}

const std::vector<RGSyncEdge>& RenderGraph::syncEdges() const noexcept {
    return impl->syncEdges;
}

std::string RenderGraph::dumpText() const {
    std::ostringstream oss;
    oss << "RenderGraph (" << (impl->compiled ? "compiled" : "uncompiled") << ")\n";
    if (!impl->compiled) {
        oss << "  declaredPasses=" << impl->passes.size()
            << " declaredResources=" << impl->resources.size() << "\n";
        return oss.str();
    }
    const auto& st = impl->stats;
    oss << "  passes: total=" << st.totalPasses
        << " executed=" << st.executedPasses
        << " culled=" << st.culledPasses << "\n"
        << "  resources: imported=" << st.importedResources
        << " transientDeclared=" << st.transientResourcesDeclared
        << " transientCulled=" << st.transientResourcesCulled
        << " physicalTransientSlots=" << st.physicalTransientSlots
        << " aliased=" << st.aliasedResources << "\n"
        << "  sync: batches=" << st.queueBatches
        << " crossQueueSyncs=" << st.crossQueueSyncs
        << " barriers=" << st.insertedBarriers
        << " uavBarriers=" << st.insertedUavBarriers << "\n";
    oss << "  physicalSlots:\n";
    for (const auto& slot : impl->slots) {
        oss << "    slot#" << slot.index << " [" << (slot.imported ? "imported" : "transient") << " "
            << (slot.kind == Impl::ResourceKind::Texture ? "Texture" : "Buffer") << "] <- {";
        for (std::size_t i = 0; i < slot.assignedResources.size(); ++i) {
            if (i) oss << ", ";
            const auto& r = impl->resources[slot.assignedResources[i]];
            oss << r.name << "(id=" << r.id << ", [" << r.firstExec << ".." << r.lastExec << "])";
        }
        oss << "}\n";
    }
    oss << "  schedule:\n";
    for (const auto& batch : impl->batches) {
        oss << "    Batch#" << batch.index << " [" << queueTypeName(batch.queue) << "]";
        if (!batch.waits.empty()) {
            oss << " waits={";
            for (std::size_t i = 0; i < batch.waits.size(); ++i) {
                if (i) oss << ",";
                oss << queueTypeName(batch.waits[i].producerQueue) << ":" << batch.waits[i].relativeValue;
            }
            oss << "}";
        }
        if (batch.signalTimelineValue) oss << " signal=" << batch.signalTimelineValue;
        oss << "\n";
        for (const auto& pp : batch.passes) {
            const auto& p = impl->passes[pp.passIndex];
            oss << "      [" << pp.execOrder << "] Pass#" << pp.passIndex << " \"" << p.desc.name << "\"\n";
            for (const auto& b : pp.preBarriers) {
                const auto& r = impl->resources[b.resource.id - 1u];
                oss << "        pre : " << r.name << " (slot#" << b.physicalSlot << ") "
                    << (b.isUavBarrier ? "UAV_BARRIER" : (std::string(resourceStateName(b.before)) + " -> " + resourceStateName(b.after))) << "\n";
            }
            for (const auto& b : pp.postBarriers) {
                const auto& r = impl->resources[b.resource.id - 1u];
                oss << "        post: " << r.name << " (slot#" << b.physicalSlot << ") "
                    << resourceStateName(b.before) << " -> " << resourceStateName(b.after) << "\n";
            }
        }
    }
    for (const auto& p : impl->passes) {
        if (!p.live) {
            oss << "    [CULLED] Pass#" << p.index << " \"" << p.desc.name << "\" [" << queueTypeName(p.desc.queue) << "]\n";
        }
    }
    return oss.str();
}

std::string RenderGraph::dumpDot() const {
    std::ostringstream oss;
    oss << "digraph RenderGraph {\n";
    oss << "  rankdir=LR;\n";
    for (const auto& p : impl->passes) {
        oss << "  p" << p.index << " [label=\"" << escapeDotLabel(p.desc.name)
            << "\\n(" << queueTypeName(p.desc.queue);
        if (impl->compiled) {
            if (p.live) oss << ", batch " << p.batchIndex << ", #" << p.execOrder;
            else oss << ", CULLED";
        }
        oss << ")\"";
        if (impl->compiled && !p.live) oss << ", style=dashed, color=gray";
        oss << "];\n";
    }
    for (const auto& [u, v] : impl->compiledEdges) {
        const bool crossQueue = (impl->passes[u].desc.queue != impl->passes[v].desc.queue);
        oss << "  p" << u << " -> p" << v;
        if (crossQueue) oss << " [color=red, label=\"timeline\"]";
        oss << ";\n";
    }
    oss << "}\n";
    return oss.str();
}

std::uint64_t detail::allocateDeviceGeneration() noexcept {
    static GenerationCounter counter;
    return counter.next();
}
std::string_view buildFingerprint() noexcept { return GFX_SOURCE_SHA256; }
#ifdef GFX_HAS_METAL
Result<std::unique_ptr<Device>> createMetalDevice(DeviceDesc);
Result<std::vector<AdapterInfo>> enumerateMetalAdapters();
#endif
#ifdef GFX_HAS_D3D12
Result<std::unique_ptr<Device>> createD3D12Device(DeviceDesc);
Result<std::vector<AdapterInfo>> enumerateD3D12Adapters();
#endif
#ifdef GFX_HAS_VULKAN
Result<std::unique_ptr<Device>> createVulkanDevice(DeviceDesc);
Result<std::vector<AdapterInfo>> enumerateVulkanAdapters();
#endif
bool backendCompiled(Backend backend) noexcept {
#ifdef GFX_HAS_METAL
    if (backend == Backend::Metal) return true;
#endif
#ifdef GFX_HAS_D3D12
    if (backend == Backend::D3D12) return true;
#endif
#ifdef GFX_HAS_VULKAN
    if (backend == Backend::Vulkan) return true;
#endif
    (void)backend; return false;
}
Result<std::unique_ptr<Device>> createDevice(DeviceDesc desc) {
    if (desc.adapter && desc.adapter->backend!=desc.backend)
        return std::unexpected(Error{ErrorCode::InvalidArgument,"Adapter identity belongs to a different backend"});
#ifdef GFX_HAS_METAL
    if (desc.backend == Backend::Metal) return createMetalDevice(desc);
#endif
#ifdef GFX_HAS_D3D12
    if (desc.backend == Backend::D3D12) return createD3D12Device(desc);
#endif
#ifdef GFX_HAS_VULKAN
    if (desc.backend == Backend::Vulkan) return createVulkanDevice(desc);
#endif
    return std::unexpected(Error{ErrorCode::Unsupported, "Requested graphics backend is not compiled"});
}
Result<std::vector<AdapterInfo>> enumerateAdapters(Backend backend) {
#ifdef GFX_HAS_METAL
    if (backend == Backend::Metal) return enumerateMetalAdapters();
#endif
#ifdef GFX_HAS_D3D12
    if (backend==Backend::D3D12) return enumerateD3D12Adapters();
#else
    (void)backend;
#endif
#ifdef GFX_HAS_VULKAN
    if (backend == Backend::Vulkan) return enumerateVulkanAdapters();
#endif
    return std::unexpected(Error{ErrorCode::Unsupported,"Requested graphics backend is not compiled"});
}
}
