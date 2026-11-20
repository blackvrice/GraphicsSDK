#include <graphics/Graphics.hpp>
#include "DeviceGeneration.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <deque>
#include <limits>
#include <list>
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
