#pragma once
// Independent, non-intrusive RHI compatibility adapter built solely on GraphicsSDK public APIs.
// Preserves complete SDK independence: requires no Sandbox headers, source reuse, or build dependency.

#include <graphics/Graphics.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gfx::sandbox_adapter {

template <class Tag>
struct GenerationalHandle {
    std::uint32_t index{};
    std::uint32_t generation{}; // 0 means invalid/null handle.
    [[nodiscard]] bool valid() const noexcept { return generation != 0u; }
    explicit operator bool() const noexcept { return valid(); }
    bool operator==(const GenerationalHandle&) const noexcept = default;
};

struct BufferTag {};
struct TextureTag {};
struct PipelineTag {};

using AdapterBuffer = GenerationalHandle<BufferTag>;
using AdapterTexture = GenerationalHandle<TextureTag>;
using AdapterPipeline = GenerationalHandle<PipelineTag>;

template <class Tag, class T>
class HandlePool {
public:
    using Handle = GenerationalHandle<Tag>;

    [[nodiscard]] Handle insert(T value) {
        std::uint32_t idx{};
        if (!freeList.empty()) {
            idx = freeList.back();
            freeList.pop_back();
        } else {
            idx = static_cast<std::uint32_t>(slots.size());
            slots.push_back(Slot{});
        }
        Slot& slot = slots[idx];
        slot.value = std::move(value);
        ++liveCount;
        return Handle{idx, slot.generation};
    }

    [[nodiscard]] T* get(Handle h) noexcept {
        if (!h.valid() || h.index >= slots.size()) return nullptr;
        Slot& slot = slots[h.index];
        return (slot.value && slot.generation == h.generation) ? &*slot.value : nullptr;
    }

    [[nodiscard]] const T* get(Handle h) const noexcept {
        return const_cast<HandlePool*>(this)->get(h);
    }

    [[nodiscard]] std::optional<T> take(Handle h) {
        if (!get(h)) return std::nullopt;
        Slot& slot = slots[h.index];
        std::optional<T> out = std::move(slot.value);
        slot.value.reset();
        // Advance non-zero generation so stale handles are permanently rejected.
        ++slot.generation;
        if (slot.generation == 0u) slot.generation = 1u;
        freeList.push_back(h.index);
        --liveCount;
        return out;
    }

    void clear() {
        slots.clear();
        freeList.clear();
        liveCount = 0;
    }

    [[nodiscard]] std::size_t size() const noexcept { return liveCount; }

private:
    struct Slot {
        std::optional<T> value;
        std::uint32_t generation{1u};
    };
    std::vector<Slot> slots;
    std::vector<std::uint32_t> freeList;
    std::size_t liveCount{};
};

class DeferredDestructionQueue {
public:
    void pushBuffer(BufferPtr buffer, std::uint64_t fenceValue) {
        if (buffer) items.push_back(Item{fenceValue, std::move(buffer), {}, {}});
    }
    void pushTexture(TexturePtr texture, std::uint64_t fenceValue) {
        if (texture) items.push_back(Item{fenceValue, {}, std::move(texture), {}});
    }
    void pushPipeline(PipelinePtr pipeline, std::uint64_t fenceValue) {
        if (pipeline) items.push_back(Item{fenceValue, {}, {}, std::move(pipeline)});
    }

    std::size_t collect(std::uint64_t completedFenceValue) {
        std::size_t released = 0;
        while (!items.empty() && items.front().fenceValue <= completedFenceValue) {
            items.pop_front();
            ++released;
        }
        totalReleased += released;
        return released;
    }

    std::size_t clear() {
        const std::size_t n = items.size();
        items.clear();
        totalReleased += n;
        return n;
    }

    [[nodiscard]] std::size_t pending() const noexcept { return items.size(); }
    [[nodiscard]] std::uint64_t releasedTotal() const noexcept { return totalReleased; }

private:
    struct Item {
        std::uint64_t fenceValue{};
        BufferPtr buffer;
        TexturePtr texture;
        PipelinePtr pipeline;
    };
    std::deque<Item> items;
    std::uint64_t totalReleased{};
};

// Compatibility surface mirroring Sandbox RHI value types with explicit gap boundaries.
enum class AdapterFormat {
    Unknown = 0,
    RGBA8Unorm,
    BGRA8Unorm,
    RGBA16Float,
    D32Float,
    // Declared to verify honest Unsupported rejection for formats not yet in GraphicsSDK Core:
    R8Unorm,
    RG8Unorm,
    RGBA8Srgb,
    D24UnormS8Uint,
    R16Uint
};

enum class AdapterMemoryType {
    GpuOnly = 0,
    Upload,
    Readback
};

struct AdapterBufferDesc {
    std::uint64_t size{};
    AdapterMemoryType memory{AdapterMemoryType::GpuOnly};
    bool storage{};
    std::string debugName;
};

struct AdapterTextureDesc {
    std::uint32_t width{1};
    std::uint32_t height{1};
    AdapterFormat format{AdapterFormat::RGBA8Unorm};
    std::uint32_t mips{1};
    std::uint32_t layers{1};
    std::uint32_t sampleCount{1};
    bool renderTarget{};
    bool depthStencil{};
    bool storage{};
    std::string debugName;
};

struct AdapterDeviceDesc {
    Backend backend{Backend::D3D12};
    bool validation{};
    bool softwareAdapter{};
    bool enableDeviceLossTesting{};
    std::optional<AdapterId> adapter;
    std::uint32_t framesInFlight{2}; // 2 or 3
    std::uint64_t uploadRingBytes{1u << 20};
    std::uint32_t uploadRingAlignment{256};
};

struct AdapterCaps {
    Backend backend{Backend::D3D12};
    std::string adapterName;
    bool softwareAdapter{};
    std::uint64_t dedicatedVideoMemory{};
    bool timestampQueries{};
    bool compute{};
    std::uint32_t maxTextureDimension2D{};
    std::uint32_t constantBufferAlignment{};
    std::uint32_t textureCopyRowAlignment{};
    std::uint32_t textureCopyOffsetAlignment{};
    bool tearingSupported{}; // Tracked in P10-PRES-01; reported honestly as false here.
};

struct AdapterStats {
    std::uint64_t liveBuffers{};
    std::uint64_t liveTextures{};
    std::uint64_t livePipelines{};
    std::uint64_t pendingDestructions{};
    std::uint64_t releasedObjects{};
    std::uint64_t uploadRingUsedBytes{};
    std::uint64_t uploadRingDeferred{};
    std::uint64_t frameNumber{};
    std::uint64_t validationErrors{};
    std::uint64_t sdkUniqueResources{};
};

struct CompletedTimestamps {
    std::uint64_t frameNumber{};
    std::vector<TimestampScopeResult> scopes;
    [[nodiscard]] bool valid() const noexcept { return frameNumber != 0u && !scopes.empty(); }
};

struct AdapterBufferCopy {
    AdapterBuffer src;
    std::uint64_t srcOffset{};
    AdapterBuffer dst;
    std::uint64_t dstOffset{};
    std::uint64_t size{};
};

struct AdapterBufferTextureCopy {
    AdapterBuffer buffer;
    std::uint64_t bufferOffset{};
    std::uint32_t bufferRowPitch{};
    AdapterTexture texture;
    std::uint32_t mip{};
    std::uint32_t layer{};
    std::uint32_t x{};
    std::uint32_t y{};
    std::uint32_t width{};
    std::uint32_t height{};
};

[[nodiscard]] inline Result<Format> mapFormat(AdapterFormat f) {
    switch (f) {
    case AdapterFormat::RGBA8Unorm: return Format::RGBA8UNorm;
    case AdapterFormat::BGRA8Unorm: return Format::BGRA8UNorm;
    case AdapterFormat::RGBA16Float: return Format::RGBA16Float;
    case AdapterFormat::D32Float: return Format::D32Float;
    case AdapterFormat::R8Unorm:
    case AdapterFormat::RG8Unorm:
    case AdapterFormat::RGBA8Srgb:
    case AdapterFormat::D24UnormS8Uint:
    case AdapterFormat::R16Uint:
        return std::unexpected(Error{ErrorCode::Unsupported,
            "AdapterFormat is not yet supported by GraphicsSDK Core (tracked in P8 format expansion)"});
    case AdapterFormat::Unknown:
    default:
        return std::unexpected(Error{ErrorCode::InvalidArgument, "Unknown AdapterFormat"});
    }
}

[[nodiscard]] inline Memory mapMemory(AdapterMemoryType m) noexcept {
    switch (m) {
    case AdapterMemoryType::Upload: return Memory::Upload;
    case AdapterMemoryType::Readback: return Memory::Readback;
    case AdapterMemoryType::GpuOnly:
    default: return Memory::Device;
    }
}

class AdapterDevice {
public:
    struct FrameSlot {
        std::unique_ptr<CommandPool> pool;
        std::unique_ptr<CommandList> cmd;
        Fence lastFence{};
        std::uint64_t recordedFrameNumber{};
        bool hasTimestampScopes{};
    };

    AdapterDevice() = default;
    ~AdapterDevice() {
        if (device) {
            (void)waitIdle();
            deferred.clear();
            buffers.clear();
            textures.clear();
            pipelines.clear();
            uploadRing.reset();
            frames.clear();
        }
    }
    AdapterDevice(const AdapterDevice&) = delete;
    AdapterDevice& operator=(const AdapterDevice&) = delete;

    [[nodiscard]] static Result<std::unique_ptr<AdapterDevice>> create(const AdapterDeviceDesc& desc) {
        if (desc.framesInFlight < 2u || desc.framesInFlight > 3u) {
            return std::unexpected(Error{ErrorCode::InvalidArgument, "framesInFlight must be 2 or 3"});
        }
        DeviceDesc sdkDesc{};
        sdkDesc.backend = desc.backend;
        sdkDesc.validation = desc.validation;
        sdkDesc.softwareAdapter = desc.softwareAdapter;
        sdkDesc.enableDeviceLossTesting = desc.enableDeviceLossTesting;
        sdkDesc.adapter = desc.adapter;

        auto dev = createDevice(sdkDesc);
        if (!dev) return std::unexpected(dev.error());

        auto adapter = std::make_unique<AdapterDevice>();
        adapter->device = std::move(*dev);
        adapter->validationEnabled = desc.validation;
        adapter->framesInFlightCount = desc.framesInFlight;

        const auto& c = adapter->device->caps();
        adapter->adapterCaps.backend = c.backend;
        adapter->adapterCaps.adapterName = c.adapterName;
        adapter->adapterCaps.softwareAdapter = c.softwareAdapter;
        adapter->adapterCaps.timestampQueries = c.timestampQueries;
        adapter->adapterCaps.compute = c.compute;
        adapter->adapterCaps.maxTextureDimension2D = c.maxTextureDimension2D;
        adapter->adapterCaps.constantBufferAlignment = c.constantBufferAlignment;
        adapter->adapterCaps.textureCopyRowAlignment = c.textureCopyRowAlignment;
        adapter->adapterCaps.textureCopyOffsetAlignment = c.textureCopyOffsetAlignment;
        adapter->adapterCaps.tearingSupported = false;

        if (auto list = enumerateAdapters(c.backend)) {
            for (const auto& info : *list) {
                if (info.id == c.adapterId) {
                    adapter->adapterCaps.dedicatedVideoMemory = info.dedicatedVideoMemory;
                    break;
                }
            }
        }

        UploadRingDesc ringDesc{};
        ringDesc.capacity = desc.uploadRingBytes;
        ringDesc.segmentCount = desc.framesInFlight;
        ringDesc.alignment = desc.uploadRingAlignment;
        ringDesc.queue = QueueType::Graphics;
        auto ring = adapter->device->createUploadRing(ringDesc);
        if (!ring) return std::unexpected(ring.error());
        adapter->uploadRing = std::move(*ring);

        adapter->frames.resize(desc.framesInFlight);
        for (std::uint32_t i = 0; i < desc.framesInFlight; ++i) {
            auto pool = adapter->device->createCommandPool(QueueType::Graphics);
            if (!pool) return std::unexpected(pool.error());
            auto cmd = (*pool)->createCommandList();
            if (!cmd) return std::unexpected(cmd.error());
            adapter->frames[i].pool = std::move(*pool);
            adapter->frames[i].cmd = std::move(*cmd);
        }

        return adapter;
    }

    [[nodiscard]] const AdapterCaps& caps() const noexcept { return adapterCaps; }
    [[nodiscard]] Device& nativeDevice() noexcept { return *device; }
    [[nodiscard]] const Device& nativeDevice() const noexcept { return *device; }
    [[nodiscard]] std::uint32_t framesInFlight() const noexcept { return framesInFlightCount; }
    [[nodiscard]] std::uint32_t frameIndex() const noexcept { return activeSlot; }
    [[nodiscard]] std::uint64_t frameNumber() const noexcept { return currentFrameNumber; }
    [[nodiscard]] const CompletedTimestamps& completedTimestamps() const noexcept { return lastCompletedTimestamps; }

    [[nodiscard]] Result<AdapterBuffer> createBuffer(const AdapterBufferDesc& desc) {
        auto buf = device->createBuffer(BufferDesc{desc.size, mapMemory(desc.memory), desc.storage});
        if (!buf) return std::unexpected(buf.error());
        return buffers.insert(BufferEntry{std::move(*buf), desc});
    }

    [[nodiscard]] Result<AdapterTexture> createTexture(const AdapterTextureDesc& desc) {
        if (desc.mips != 1u || desc.layers != 1u) {
            return std::unexpected(Error{ErrorCode::Unsupported,
                "Multi-mip and multi-layer textures are not yet supported by GraphicsSDK Core (tracked in P8-TEX-02)"});
        }
        auto fmt = mapFormat(desc.format);
        if (!fmt) return std::unexpected(fmt.error());
        TextureDesc sdkDesc{};
        sdkDesc.width = desc.width;
        sdkDesc.height = desc.height;
        sdkDesc.format = *fmt;
        sdkDesc.renderTarget = desc.renderTarget;
        sdkDesc.depthStencil = desc.depthStencil;
        sdkDesc.storage = desc.storage;
        sdkDesc.sampleCount = desc.sampleCount;
        auto tex = device->createTexture(sdkDesc);
        if (!tex) return std::unexpected(tex.error());
        return textures.insert(TextureEntry{std::move(*tex), desc});
    }

    [[nodiscard]] Result<AdapterPipeline> createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
        auto cached = device->createCachedGraphicsPipeline(desc);
        if (!cached) return std::unexpected(cached.error());
        return pipelines.insert(std::move(cached->pipeline));
    }

    void destroy(AdapterBuffer handle) {
        if (auto entry = buffers.take(handle)) {
            deferred.pushBuffer(std::move(entry->ptr), lastSubmittedFenceValue);
        }
    }

    void destroy(AdapterTexture handle) {
        if (auto entry = textures.take(handle)) {
            deferred.pushTexture(std::move(entry->ptr), lastSubmittedFenceValue);
        }
    }

    void destroy(AdapterPipeline handle) {
        if (auto ptr = pipelines.take(handle)) {
            deferred.pushPipeline(std::move(*ptr), lastSubmittedFenceValue);
        }
    }

    [[nodiscard]] bool alive(AdapterBuffer h) const noexcept { return buffers.get(h) != nullptr; }
    [[nodiscard]] bool alive(AdapterTexture h) const noexcept { return textures.get(h) != nullptr; }
    [[nodiscard]] bool alive(AdapterPipeline h) const noexcept { return pipelines.get(h) != nullptr; }

    [[nodiscard]] Result<BufferPtr> resolve(AdapterBuffer h) const {
        const auto* e = buffers.get(h);
        if (!e) return std::unexpected(Error{ErrorCode::InvalidArgument, "Invalid or stale AdapterBuffer handle"});
        return e->ptr;
    }

    [[nodiscard]] Result<TexturePtr> resolve(AdapterTexture h) const {
        const auto* e = textures.get(h);
        if (!e) return std::unexpected(Error{ErrorCode::InvalidArgument, "Invalid or stale AdapterTexture handle"});
        return e->ptr;
    }

    [[nodiscard]] Result<PipelinePtr> resolve(AdapterPipeline h) const {
        const auto* p = pipelines.get(h);
        if (!p) return std::unexpected(Error{ErrorCode::InvalidArgument, "Invalid or stale AdapterPipeline handle"});
        return *p;
    }

    [[nodiscard]] Result<void> writeBuffer(AdapterBuffer h, std::uint64_t offset, std::span<const std::byte> data) {
        auto ptr = resolve(h);
        if (!ptr) return std::unexpected(ptr.error());
        return (*ptr)->write(offset, data);
    }

    [[nodiscard]] Result<std::vector<std::byte>> readBuffer(AdapterBuffer h, std::uint64_t offset, std::uint64_t size) {
        auto ptr = resolve(h);
        if (!ptr) return std::unexpected(ptr.error());
        return (*ptr)->read(offset, size);
    }

    // Mirrors Sandbox IRenderDevice::allocateUpload: tracks ring exhaustion as uploadRingDeferred.
    [[nodiscard]] Result<UploadAllocation> allocateUpload(std::uint64_t size, std::uint64_t alignment = 256) {
        auto alloc = uploadRing->allocate(size, alignment);
        if (!alloc) {
            if (alloc.error().code == ErrorCode::OutOfMemory) {
                ++uploadRingDeferredCount;
            }
            return std::unexpected(alloc.error());
        }
        return alloc;
    }

    [[nodiscard]] Result<UploadAllocation> uploadData(std::span<const std::byte> data, std::uint64_t alignment = 256) {
        auto alloc = uploadRing->upload(data, alignment);
        if (!alloc) {
            if (alloc.error().code == ErrorCode::OutOfMemory) {
                ++uploadRingDeferredCount;
            }
            return std::unexpected(alloc.error());
        }
        return alloc;
    }

    [[nodiscard]] Result<CommandList*> beginFrame() {
        if (inFrame) {
            return std::unexpected(Error{ErrorCode::InvalidState, "beginFrame() called while already inside a frame"});
        }
        ++currentFrameNumber;
        activeSlot = static_cast<std::uint32_t>((currentFrameNumber - 1u) % framesInFlightCount);
        FrameSlot& slot = frames[activeSlot];

        if (slot.lastFence.value != 0u) {
            if (auto ok = device->queue().wait(slot.lastFence); !ok) {
                return std::unexpected(ok.error());
            }
            if (slot.hasTimestampScopes && adapterCaps.timestampQueries) {
                if (auto scopes = slot.cmd->timestampScopes(); scopes) {
                    lastCompletedTimestamps.frameNumber = slot.recordedFrameNumber;
                    lastCompletedTimestamps.scopes = std::move(*scopes);
                }
                slot.hasTimestampScopes = false;
            }
            slot.lastFence = {};
        }

        auto completed = device->queue().completedValue();
        if (!completed) return std::unexpected(completed.error());
        (void)deferred.collect(*completed);
        if (auto ok = uploadRing->reclaim(); !ok) return std::unexpected(ok.error());

        if (auto ok = slot.pool->reset(); !ok) return std::unexpected(ok.error());
        if (auto ok = slot.cmd->begin(); !ok) return std::unexpected(ok.error());
        slot.recordedFrameNumber = currentFrameNumber;
        inFrame = true;
        return slot.cmd.get();
    }

    [[nodiscard]] Result<void> recordCopyBuffer(CommandList& cmd, const AdapterBufferCopy& c) {
        auto src = resolve(c.src); if (!src) return std::unexpected(src.error());
        auto dst = resolve(c.dst); if (!dst) return std::unexpected(dst.error());
        return cmd.copyBufferRegion(*src, c.srcOffset, *dst, c.dstOffset, c.size);
    }

    [[nodiscard]] Result<void> recordCopyBufferToTexture(CommandList& cmd, const AdapterBufferTextureCopy& c) {
        if (c.mip != 0u || c.layer != 0u) {
            return std::unexpected(Error{ErrorCode::Unsupported, "Non-zero mip/layer copy is tracked in P8-TEX-02"});
        }
        auto buf = resolve(c.buffer); if (!buf) return std::unexpected(buf.error());
        auto tex = resolve(c.texture); if (!tex) return std::unexpected(tex.error());
        const TextureCopyRegion region{c.bufferOffset, c.bufferRowPitch, c.x, c.y, c.width, c.height};
        return cmd.copyBufferToTextureRegion(*buf, *tex, region);
    }

    [[nodiscard]] Result<void> recordCopyTextureToBuffer(CommandList& cmd, const AdapterBufferTextureCopy& c) {
        if (c.mip != 0u || c.layer != 0u) {
            return std::unexpected(Error{ErrorCode::Unsupported, "Non-zero mip/layer copy is tracked in P8-TEX-02"});
        }
        auto buf = resolve(c.buffer); if (!buf) return std::unexpected(buf.error());
        auto tex = resolve(c.texture); if (!tex) return std::unexpected(tex.error());
        const TextureCopyRegion region{c.bufferOffset, c.bufferRowPitch, c.x, c.y, c.width, c.height};
        return cmd.copyTextureToBufferRegion(*tex, *buf, region);
    }

    void markFrameHasTimestampScopes() noexcept {
        if (inFrame) frames[activeSlot].hasTimestampScopes = true;
    }

    [[nodiscard]] Result<Fence> endFrame() {
        if (!inFrame) {
            return std::unexpected(Error{ErrorCode::InvalidState, "endFrame() called outside a frame"});
        }
        FrameSlot& slot = frames[activeSlot];
        inFrame = false;
        if (auto ok = slot.cmd->end(); !ok) return std::unexpected(ok.error());
        auto fence = device->queue().submit(*slot.cmd);
        if (!fence) return std::unexpected(fence.error());
        slot.lastFence = *fence;
        lastSubmittedFenceValue = fence->value;
        if (auto ok = uploadRing->commit(*fence); !ok) return std::unexpected(ok.error());
        return *fence;
    }

    [[nodiscard]] Result<void> waitIdle() {
        if (auto ok = device->queue().waitIdle(); !ok) return ok;
        for (auto& slot : frames) {
            if (slot.lastFence.value != 0u) {
                if (slot.hasTimestampScopes && adapterCaps.timestampQueries) {
                    if (auto scopes = slot.cmd->timestampScopes(); scopes) {
                        lastCompletedTimestamps.frameNumber = slot.recordedFrameNumber;
                        lastCompletedTimestamps.scopes = std::move(*scopes);
                    }
                    slot.hasTimestampScopes = false;
                }
                slot.lastFence = {};
            }
            if (slot.pool && !inFrame) {
                if (auto ok = slot.pool->reset(); !ok) return ok;
            }
        }
        auto completed = device->queue().completedValue();
        if (!completed) return std::unexpected(completed.error());
        (void)deferred.collect(*completed);
        if (uploadRing) {
            if (auto ok = uploadRing->reclaim(); !ok) return std::unexpected(ok.error());
        }
        return {};
    }

    [[nodiscard]] Result<AdapterStats> stats() {
        auto sdkStats = device->resourceStatistics();
        if (!sdkStats) return std::unexpected(sdkStats.error());
        auto ringStats = uploadRing->statistics();
        if (!ringStats) return std::unexpected(ringStats.error());

        std::uint64_t valErr = 0;
        if (validationEnabled) {
            auto ve = device->validationErrorCount();
            if (!ve) return std::unexpected(ve.error());
            valErr = *ve;
        }
        return AdapterStats{
            buffers.size(),
            textures.size(),
            pipelines.size(),
            deferred.pending(),
            deferred.releasedTotal(),
            ringStats->inFlightBytes,
            uploadRingDeferredCount,
            currentFrameNumber,
            valErr,
            sdkStats->uniqueResources
        };
    }

private:
    struct BufferEntry {
        BufferPtr ptr;
        AdapterBufferDesc desc;
    };
    struct TextureEntry {
        TexturePtr ptr;
        AdapterTextureDesc desc;
    };

    std::unique_ptr<Device> device;
    AdapterCaps adapterCaps{};
    bool validationEnabled{};
    std::uint32_t framesInFlightCount{2};
    std::uint32_t activeSlot{};
    std::uint64_t currentFrameNumber{};
    std::uint64_t lastSubmittedFenceValue{};
    std::uint64_t uploadRingDeferredCount{};
    bool inFrame{};

    std::unique_ptr<UploadRing> uploadRing;
    std::vector<FrameSlot> frames;
    HandlePool<BufferTag, BufferEntry> buffers;
    HandlePool<TextureTag, TextureEntry> textures;
    HandlePool<PipelineTag, PipelinePtr> pipelines;
    DeferredDestructionQueue deferred;
    CompletedTimestamps lastCompletedTimestamps;
};

} // namespace gfx::sandbox_adapter
