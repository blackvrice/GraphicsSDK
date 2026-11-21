// Private Objective-C++ implementation; CMake explicitly selects OBJCXX for this unit.
#include <graphics/Graphics.hpp>
#include "../../foundation/DeviceGeneration.hpp"
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <objc/runtime.h>
#include <algorithm>
#include <atomic>
#include <deque>
#include <cstring>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <array>

namespace gfx {
namespace {
auto fail(ErrorCode code, std::string text, std::int64_t native = 0) {
    return std::unexpected(Error{code, std::move(text), native});
}
bool validQueueType(QueueType t) { return t == QueueType::Graphics || t == QueueType::Compute || t == QueueType::Copy; }
std::size_t queueIndex(QueueType t) { return static_cast<std::size_t>(t); }
struct NativeTimelineEvent {
    id<MTLSharedEvent> event;
    std::uint64_t lastSignaled{};
    void release() { event = nil; }
    ~NativeTimelineEvent() { release(); }
};
struct Pending {
    std::uint64_t serial;
    id<MTLCommandBuffer> buffer;
    std::vector<std::shared_ptr<struct Allocation>> allocations;
    std::vector<std::shared_ptr<struct NativePipeline>> pipelines;
    id<CAMetalDrawable> drawable{};
    std::vector<std::shared_ptr<struct NativeTextureView>> textureViews;
    std::vector<id<MTLCommandBuffer>> extraBuffers;
    std::vector<std::shared_ptr<NativeTimelineEvent>> timelineEvents;
    std::vector<std::pair<std::shared_ptr<NativeTimelineEvent>, std::uint64_t>> signaledTimelines;
};
struct NativeTextureView {
    std::shared_ptr<struct Allocation> allocation;
    id<MTLTexture> texture;
    TextureViewDesc desc;
    void release() { texture = nil; allocation.reset(); }
    ~NativeTextureView() { release(); }
};
struct Allocation;
struct NativeMemoryHeap {
    struct PlacedEntry {
        std::weak_ptr<Allocation> allocation;
        std::uint64_t offset{};
        std::uint64_t size{};
    };
    id<MTLHeap> heap;
    MemoryHeapDesc desc{};
    mutable std::vector<PlacedEntry> entries;
    void release() { heap = nil; }
    ~NativeMemoryHeap() { release(); }
};
struct Allocation {
    std::shared_ptr<NativeMemoryHeap> heap;
    id<MTLBuffer> buffer;
    id<MTLTexture> texture;
    BufferDesc desc;
    TextureDesc textureDesc;
    TextureFootprint footprint;
    bool initialized{};
    bool backbuffer{};
    ResourceState state{ResourceState::Common};
    std::uint64_t version{}, bytes{};
    std::array<std::uint64_t, 3> lastUse{};
};
char layerLeaseKey;
struct DrawableScope {
    CAMetalLayer* layer;
    id<CAMetalDrawable> drawable;
    std::shared_ptr<Allocation> current;

    void discard() {
        if (current) current->texture = nil;
        current.reset();
        drawable = nil;
    }
    void release() {
        discard();
        if (layer) objc_setAssociatedObject(layer, &layerLeaseKey, nil, OBJC_ASSOCIATION_RETAIN);
        layer = nil;
    }
    ~DrawableScope() { release(); }
};
struct Use {
    std::shared_ptr<Allocation> allocation;
    std::uint64_t version;
    ResourceState shadow;
    bool initialized;
    bool mutated{};
};
struct NativePipeline {
    id<MTLRenderPipelineState> pipeline;
    id<MTLComputePipelineState> computePipeline;
    id<MTLDepthStencilState> depth;
    std::vector<id<MTLSamplerState>> samplers;
    VertexLayout vertices;
    BindingLayout bindings;
    RenderTargetLayout targets;
    DepthState depthState;
    std::vector<std::uint64_t> minimumBytes;
    std::uint32_t threadgroupSizeX{1}, threadgroupSizeY{1}, threadgroupSizeZ{1};
    bool sampled{};
    bool isCompute{};
    void release() { pipeline = nil; computePipeline = nil; depth = nil; samplers.clear(); }
};
struct BufferView {
    std::shared_ptr<Allocation> allocation;
    std::uint64_t offset{},bytes{};
    std::uint32_t stride{};
};
struct ResourceView { std::shared_ptr<Allocation> allocation; std::uint64_t offset{},size{}; std::shared_ptr<NativeTextureView> textureView; ResourceState expectedState{ResourceState::Common}; };
struct ComputeDispatchOp {
    std::shared_ptr<NativePipeline> pipeline;
    std::shared_ptr<const std::vector<ResourceView>> bindings;
    std::uint32_t groupX{}, groupY{}, groupZ{};
    bool indirect{};
    std::shared_ptr<Allocation> indirectBuffer;
    std::uint64_t indirectOffset{};
};
struct Draw {
    std::shared_ptr<NativePipeline> pipeline;
    std::shared_ptr<const std::vector<ResourceView>> bindings;
    std::array<BufferView,15> vertices;
    BufferView indices;
    std::uint32_t count{},instances{},first{},firstInstance{};
    std::int32_t baseVertex{};
    bool indexed{};
    MTLViewport viewport{};
    MTLScissorRect scissor{};
    bool indirect{};
    BufferView indirectArgs;
    std::uint32_t indirectDrawCount{};
    std::uint32_t indirectStride{};
};
struct Attachment {
    std::shared_ptr<Allocation> allocation;
    LoadOp load;
    Color clear;
    float clearDepth{};
    bool readOnly{};
    std::shared_ptr<NativeTextureView> textureView;
};
enum class OperationKind { Buffer, UploadTexture, ReadTexture, Pass, Compute, Resolve };
struct Operation {
    std::shared_ptr<Allocation> source, destination;
    std::uint64_t bytes{};
    std::string marker;
    std::uint64_t sourceOffset{}, destinationOffset{};
    TextureCopyRegion textureRegion{};
    std::uint32_t textureRowPitch{};
    std::uint64_t textureImageBytes{};
    OperationKind kind{OperationKind::Buffer};
    std::vector<Attachment> attachments;
    std::vector<std::string> markers;
    std::vector<Draw> draws;
    std::vector<std::pair<bool,std::size_t>> order;
    ComputeDispatchOp compute;
};
struct NativeRecording { id<MTLCommandBuffer> buffer; };
struct QueueSlot {
    id<MTLCommandQueue> queue;
    std::uint64_t id{};
    std::uint64_t last{};
    std::atomic<std::uint64_t> completed{0};
    std::deque<Pending> pending;
};
struct State {
    id<MTLDevice> device;
    std::array<QueueSlot, 3> queues;
    id<MTLCommandQueue>& queue{queues[0].queue};
    std::uint64_t& last{queues[0].last};
    std::deque<Pending>& pending{queues[0].pending};
    const std::thread::id thread = std::this_thread::get_id();
    std::uint64_t generation{};
    std::shared_ptr<detail::ProfilingTracker> profiler;
    std::atomic<bool> retired{false};
    std::atomic<bool> hasFailure{false};
    std::optional<Error> failure;
    std::mutex recordingsMutex;
    std::mutex tailMutex;
    std::vector<std::weak_ptr<Allocation>> allocations;
    std::vector<std::weak_ptr<NativeMemoryHeap>> memoryHeaps;
    std::vector<std::weak_ptr<NativeTextureView>> imageViews;
    std::vector<std::weak_ptr<int>> objects;
    id<MTLComputePipelineState> tailPipeline;
    std::vector<std::weak_ptr<NativeRecording>> recordings;
    std::vector<std::weak_ptr<NativePipeline>> pipelines;
    std::vector<std::weak_ptr<DrawableScope>> surfaces;
    std::vector<std::weak_ptr<NativeTimelineEvent>> timelineEvents;
    void retire() {
        if (retired.load(std::memory_order_acquire)) return;
        @autoreleasepool {
            for (auto& q : queues) {
                for (auto& p : q.pending) {
                    for (id<MTLCommandBuffer> extra : p.extraBuffers) [extra waitUntilCompleted];
                    [p.buffer waitUntilCompleted];
                }
                q.pending.clear();
            }
            {
                std::scoped_lock lock(recordingsMutex);
                for (auto& w : recordings) if (auto r = w.lock()) r->buffer = nil;
                recordings.clear();
            }
            for (auto& w : surfaces) if (auto surface = w.lock()) surface->release();
            for (auto& w : imageViews) if (auto v = w.lock()) v->release();
            for (auto& w : allocations) if (auto a = w.lock()) { a->buffer = nil; a->texture = nil; a->heap.reset(); }
            for (auto& w : memoryHeaps) if (auto h = w.lock()) h->release();
            memoryHeaps.clear();
            for (auto& w : pipelines) if (auto p = w.lock()) p->release();
            for (auto& w : timelineEvents) if (auto e = w.lock()) e->release();
            timelineEvents.clear();
            tailPipeline = nil;
            for (auto& q : queues) q.queue = nil;
            device = nil;
        }
        retired.store(true, std::memory_order_release);
    }
    ~State() { retire(); }
    Result<void> checkAliveForWorker() const {
        if (retired.load(std::memory_order_acquire)) return fail(ErrorCode::DeviceLost, "Metal device generation retired");
        if (hasFailure.load(std::memory_order_acquire)) return fail(ErrorCode::BackendFailure, "Metal device generation encountered a previous failure");
        return {};
    }
    Result<void> check() const {
        if (std::this_thread::get_id() != thread) return fail(ErrorCode::InvalidState, "Metal access requires the creating thread");
        if (retired.load(std::memory_order_acquire)) return fail(ErrorCode::DeviceLost, "Metal device generation retired");
        if (failure) return std::unexpected(*failure);
        return {};
    }
    Result<void> pollQueue(QueueType qType) {
        if (auto ok = check(); !ok) return ok;
        auto& q = queues[queueIndex(qType)];
        @autoreleasepool {
            while (!q.pending.empty()) {
                auto& p = q.pending.front();
                auto checkBuffer = [&](id<MTLCommandBuffer> buf) -> Result<bool> {
                    const auto status = buf.status;
                    if (status == MTLCommandBufferStatusError) {
                        NSError* error = buf.error;
                        const bool metal = [error.domain isEqualToString:MTLCommandBufferErrorDomain];
                        const auto code = error ? error.code : 0;
                        const auto category = metal && (code == MTLCommandBufferErrorDeviceRemoved || code == MTLCommandBufferErrorAccessRevoked)
                            ? ErrorCode::DeviceLost : ErrorCode::BackendFailure;
                        const char* message = error.localizedDescription.UTF8String;
                        failure = Error{category, message ? message : "Metal command buffer failed", code};
                        hasFailure.store(true, std::memory_order_release);
                        return std::unexpected(*failure);
                    }
                    return status == MTLCommandBufferStatusCompleted;
                };
                bool allDone = true;
                for (id<MTLCommandBuffer> extra : p.extraBuffers) {
                    auto done = checkBuffer(extra);
                    if (!done) return std::unexpected(done.error());
                    if (!*done) { allDone = false; break; }
                }
                if (allDone) {
                    auto done = checkBuffer(p.buffer);
                    if (!done) return std::unexpected(done.error());
                    allDone = *done;
                }
                if (!allDone) break;
                q.completed.store(p.serial, std::memory_order_release);
                q.pending.pop_front();
            }
        }
        return {};
    }
    Result<void> poll() {
        for (QueueType t : {QueueType::Graphics, QueueType::Compute, QueueType::Copy}) {
            if (auto ok = pollQueue(t); !ok) return ok;
        }
        return {};
    }
    Result<void> prepareTail() {
        std::scoped_lock lock(tailMutex);
        if (tailPipeline) return {};
        @autoreleasepool {
            // macOS blits require four-byte aligned offsets and sizes. This compute kernel
            // copies any unaligned byte range while preserving all surrounding destination bytes.
            NSString* source = @"#include <metal_stdlib>\nusing namespace metal;\n"
                "kernel void gfx_copy_tail(const device uchar* src [[buffer(0)]], device uchar* dst [[buffer(1)]],"
                "constant ulong& srcOffset [[buffer(2)]], constant ulong& dstOffset [[buffer(3)]],"
                "constant ulong& count [[buffer(4)]], uint gid [[thread_position_in_grid]], uint threads [[threads_per_grid]]) {"
                "for (ulong i=gid; i<count; i+=threads) dst[dstOffset+i]=src[srcOffset+i]; }";
            NSError* error = nil;
            id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
            if (!library) {
                const char* message = error.localizedDescription.UTF8String;
                return fail(ErrorCode::BackendFailure, message ? message : "Metal tail library failed", error.code);
            }
            id<MTLFunction> function = [library newFunctionWithName:@"gfx_copy_tail"];
            if (!function) return fail(ErrorCode::BackendFailure, "Metal tail function unavailable");
            tailPipeline = [device newComputePipelineStateWithFunction:function error:&error];
            if (!tailPipeline) {
                const char* message = error.localizedDescription.UTF8String;
                return fail(ErrorCode::BackendFailure, message ? message : "Metal tail pipeline failed", error.code);
            }
        }
        return {};
    }
    // Timestamps and native validation remain unsupported.
    auto unsupported() const {
        if (auto ok = check(); !ok) return std::unexpected(ok.error());
        return fail(ErrorCode::Unsupported, "Not implemented by the Metal submission backend");
    }
};
struct MetalTimelineFence final : TimelineFence {
    std::shared_ptr<State> owner;
    std::shared_ptr<NativeTimelineEvent> native;
    std::shared_ptr<int> token;
    Result<std::uint64_t> completedValue() override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        return native->event.signaledValue;
    }
    Result<void> signal(std::uint64_t value) override {
        auto current = completedValue(); if (!current) return std::unexpected(current.error());
        if (value <= *current || value <= native->lastSignaled || value == UINT64_MAX) {
            return fail(ErrorCode::InvalidArgument, "Metal TimelineFence signal value must strictly advance");
        }
        native->event.signaledValue = value;
        native->lastSignaled = value;
        return {};
    }
    Result<void> wait(std::uint64_t value, std::chrono::milliseconds timeout) override {
        if (auto ok = owner->check(); !ok) return ok;
        if (timeout.count() < 0 || timeout.count() > 4294967294ll) {
            return fail(ErrorCode::InvalidArgument, "Metal TimelineFence timeout outside finite range");
        }
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        for (;;) {
            if (auto ok = owner->poll(); !ok) return ok;
            if (native->event.signaledValue >= value) break;
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) return fail(ErrorCode::Timeout, "Metal TimelineFence wait timed out");
            std::this_thread::sleep_for(std::min(deadline - now, std::chrono::steady_clock::duration(std::chrono::milliseconds(1))));
        }
        @autoreleasepool {
            for (auto& q : owner->queues) {
                for (auto& p : q.pending) {
                    bool signaled = false;
                    for (const auto& [sigEv, sigVal] : p.signaledTimelines) {
                        if (sigEv == native && sigVal <= value) { signaled = true; break; }
                    }
                    if (signaled) {
                        for (id<MTLCommandBuffer> extra : p.extraBuffers) [extra waitUntilCompleted];
                        [p.buffer waitUntilCompleted];
                    }
                }
            }
        }
        return owner->poll();
    }
};
struct MetalMemoryHeap final : MemoryHeap {
    std::shared_ptr<State> owner;
    std::shared_ptr<NativeMemoryHeap> heap;
    std::shared_ptr<int> token;
    MemoryHeapDesc description() const override { return heap->desc; }
    Result<HeapFragmentationStats> fragmentationStats() const override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        std::erase_if(heap->entries, [](const NativeMemoryHeap::PlacedEntry& e) { return e.allocation.expired(); });
        std::vector<HeapAllocationRange> ranges;
        ranges.reserve(heap->entries.size());
        for (const auto& e : heap->entries) ranges.push_back({e.offset, e.size});
        return computeHeapFragmentationStats(heap->desc.size, ranges, true);
    }
    Result<void> setResident(bool) override {
        if (auto ok = owner->check(); !ok) return ok;
        return fail(ErrorCode::Unsupported, "Metal backend does not provide explicit per-heap residency eviction");
    }
    Result<bool> isResident() const override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        return true;
    }
};
struct MetalBuffer final : Buffer {
    std::shared_ptr<State> owner;
    std::shared_ptr<Allocation> allocation;
    std::shared_ptr<int> token;
    BufferDesc description() const override { return allocation->desc; }
    Result<void> write(std::uint64_t offset, std::span<const std::byte> data) override {
        if (auto ok = owner->poll(); !ok) return ok;
        auto& a = *allocation;
        if (a.desc.memory != Memory::Upload || offset > a.desc.size || data.size() > a.desc.size-offset)
            return fail(ErrorCode::InvalidArgument, "Metal upload write memory or range invalid");
        for (std::size_t qi = 0; qi < 3; ++qi) {
            if (a.lastUse[qi] > owner->queues[qi].completed.load(std::memory_order_acquire))
                return fail(ErrorCode::InvalidState, "Metal upload busy");
        }
        if (a.version == UINT64_MAX) return fail(ErrorCode::InvalidState, "Metal upload version exhausted");
        @autoreleasepool {
            if (!data.empty()) {
                std::memcpy(static_cast<std::byte*>(a.buffer.contents)+offset, data.data(), data.size());
                if (a.buffer.storageMode == MTLStorageModeManaged) [a.buffer didModifyRange:NSMakeRange(offset, data.size())];
            }
        }
        ++a.version; // Even a successful empty write invalidates an older recording.
        return {};
    }
    Result<std::vector<std::byte>> read(std::uint64_t offset, std::uint64_t bytes) override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        const auto& a = *allocation;
        if (a.desc.memory != Memory::Readback || offset > a.desc.size || bytes > a.desc.size-offset || bytes > SIZE_MAX)
            return fail(ErrorCode::InvalidArgument, "Metal readback read memory or range invalid");
        for (std::size_t qi = 0; qi < 3; ++qi) {
            if (a.lastUse[qi] > owner->queues[qi].completed.load(std::memory_order_acquire))
                return fail(ErrorCode::InvalidState, "Metal readback busy");
        }
        std::vector<std::byte> result(static_cast<std::size_t>(bytes));
        @autoreleasepool {
            if (bytes) std::memcpy(result.data(), static_cast<const std::byte*>(a.buffer.contents)+offset, result.size());
        }
        return result;
    }
};
struct MetalTexture final : Texture {
    std::shared_ptr<State> owner;
    std::shared_ptr<Allocation> allocation;
    std::shared_ptr<int> token;
    TextureDesc description() const override { return allocation->textureDesc; }
    TextureFootprint footprint() const override { return allocation->footprint; }
};
struct MetalTextureView final : TextureView {
    std::shared_ptr<State> owner;
    std::shared_ptr<MetalTexture> parent;
    std::shared_ptr<NativeTextureView> native;
    std::shared_ptr<int> token;
    TextureViewDesc description() const override { return native->desc; }
    TexturePtr texture() const override { return parent; }
};
struct MetalPipeline final : Pipeline {
    std::shared_ptr<State> owner;
    std::shared_ptr<NativePipeline> native;
    std::shared_ptr<int> token;
};
bool vertexState(ResourceState s) { return s == ResourceState::VertexBuffer || s == ResourceState::VertexAndIndexBuffer; }
bool indexState(ResourceState s) { return s == ResourceState::IndexBuffer || s == ResourceState::VertexAndIndexBuffer; }
bool visible(ShaderVisibility v,unsigned stage) { return v == ShaderVisibility::All || unsigned(v) == stage; }
bool isSampledTextureBinding(BindingType type) {
    return type == BindingType::Texture2D || type == BindingType::Texture1D ||
           type == BindingType::Texture3D || type == BindingType::Texture2DArray ||
           type == BindingType::TextureCube;
}
BindingType bindingTypeForDimension(TextureDimension dim) {
    switch (dim) {
    case TextureDimension::Texture1D: return BindingType::Texture1D;
    case TextureDimension::Texture3D: return BindingType::Texture3D;
    case TextureDimension::Texture2DArray: return BindingType::Texture2DArray;
    case TextureDimension::TextureCube: return BindingType::TextureCube;
    case TextureDimension::Texture2D: default: return BindingType::Texture2D;
    }
}
MTLTextureType metalTextureType(TextureDimension dim, std::uint32_t sampleCount) {
    if (sampleCount > 1u) return MTLTextureType2DMultisample;
    switch (dim) {
    case TextureDimension::Texture1D: return MTLTextureType1D;
    case TextureDimension::Texture3D: return MTLTextureType3D;
    case TextureDimension::Texture2DArray: return MTLTextureType2DArray;
    case TextureDimension::TextureCube: return MTLTextureTypeCube;
    case TextureDimension::Texture2D: default: return MTLTextureType2D;
    }
}
MTLPixelFormat pixelFormat(Format f) {
    switch (f) {
    case Format::RGBA8UNorm: return MTLPixelFormatRGBA8Unorm;
    case Format::BGRA8UNorm: return MTLPixelFormatBGRA8Unorm;
    case Format::RGBA16Float: return MTLPixelFormatRGBA16Float;
    case Format::D32Float: return MTLPixelFormatDepth32Float;
    case Format::BC1UNorm: return MTLPixelFormatBC1_RGBA;
    case Format::BC2UNorm: return MTLPixelFormatBC2_RGBA;
    case Format::BC3UNorm: return MTLPixelFormatBC3_RGBA;
    case Format::BC4UNorm: return MTLPixelFormatBC4_RUnorm;
    case Format::BC5UNorm: return MTLPixelFormatBC5_RGUnorm;
    case Format::BC6HUFloat: return MTLPixelFormatBC6H_RGBUfloat;
    case Format::BC7UNorm: return MTLPixelFormatBC7_RGBAUnorm;
    case Format::ETC2RGB8UNorm: return MTLPixelFormatETC2_RGB8;
    case Format::ETC2RGBA8UNorm: return MTLPixelFormatEAC_RGBA8;
    case Format::EACR11UNorm: return MTLPixelFormatEAC_R11Unorm;
    case Format::EACRG11UNorm: return MTLPixelFormatEAC_RG11Unorm;
    case Format::ASTC4x4UNorm: return MTLPixelFormatASTC_4x4_LDR;
    case Format::ASTC6x6UNorm: return MTLPixelFormatASTC_6x6_LDR;
    case Format::ASTC8x8UNorm: return MTLPixelFormatASTC_8x8_LDR;
    case Format::RGBA8SRGB: return MTLPixelFormatRGBA8Unorm_sRGB;
    case Format::BGRA8SRGB: return MTLPixelFormatBGRA8Unorm_sRGB;
    case Format::RGB10A2UNorm: return MTLPixelFormatRGB10A2Unorm;
    }
    return MTLPixelFormatInvalid;
}
bool metalSupportsCompressedFormat(id<MTLDevice> dev, Format f) {
    if (f >= Format::BC1UNorm && f <= Format::BC7UNorm) {
        return [dev supportsBCTextureCompression] != NO;
    }
    if (f >= Format::ETC2RGB8UNorm && f <= Format::ASTC8x8UNorm) {
        return [dev supportsFamily:MTLGPUFamilyApple2] != NO;
    }
    return true;
}
#include "MetalGraphics.hpp"
struct MetalCommands;
struct NativeCommandPool {
    std::mutex mutex;
    QueueType qType{QueueType::Graphics};
    std::atomic<std::thread::id> recordingThread{};
    std::uint64_t epoch{1};
    std::uint64_t lastSubmitted{};
    std::uint32_t openCount{};
    bool ownerAlive{true};
    std::vector<MetalCommands*> liveLists;
};
struct MetalCommands final : CommandList {
    std::shared_ptr<State> owner;
    std::shared_ptr<NativeCommandPool> pool;
    QueueType qType{QueueType::Graphics};
    std::shared_ptr<int> token;
    std::shared_ptr<NativeRecording> native;
    bool open{}, ended{};
    bool inPass{};
    std::size_t passOperation{};
    std::uint64_t submitted{};
    std::uint64_t recordedEpoch{};
    std::vector<Use> uses;
    std::vector<Operation> operations;
    std::shared_ptr<NativePipeline> selected;
    std::shared_ptr<NativePipeline> selectedCompute;
    std::shared_ptr<const std::vector<ResourceView>> bindings;
    std::shared_ptr<const std::vector<ResourceView>> computeBindings;
    std::array<BufferView,15> vertices;
    BufferView indices;
    MTLViewport viewport{};
    MTLScissorRect scissor{};
    explicit MetalCommands(std::shared_ptr<State> state, std::shared_ptr<NativeCommandPool> commandPool = {}, QueueType type = QueueType::Graphics)
        : owner(std::move(state)), pool(std::move(commandPool)), qType(pool ? pool->qType : type) {}
    QueueType queueType() const override { return qType; }
    ~MetalCommands() override {
        if (!pool) return;
        std::scoped_lock lock(pool->mutex);
        std::erase(pool->liveLists, this);
        if (open && pool->openCount > 0) --pool->openCount;
        open = false;
    }
    void clearRecordingState() {
        native.reset();
        open = false;
        ended = false;
        inPass = false;
        passOperation = 0;
        submitted = 0;
        operations.clear();
        uses.clear();
        selected.reset();
        selectedCompute.reset();
        bindings.reset();
        computeBindings.reset();
        vertices = {};
        indices = {};
    }
    Result<void> begin() override {
        auto& qSlot = owner->queues[queueIndex(qType)];
        if (pool) {
            std::scoped_lock lock(pool->mutex);
            if (auto ok = owner->checkAliveForWorker(); !ok) return ok;
            if (!pool->ownerAlive) return fail(ErrorCode::InvalidState, "Owning CommandPool has been destroyed");
            if (pool->recordingThread.load(std::memory_order_acquire) != std::this_thread::get_id())
                return fail(ErrorCode::InvalidState, "Metal command recording must run on the pool's bound recording thread");
            if (open) return fail(ErrorCode::InvalidState, "Metal command list is already recording");
            if (pool->openCount > 0) return fail(ErrorCode::InvalidState, "Another command list from this CommandPool is currently recording");
            if (recordedEpoch == pool->epoch) return fail(ErrorCode::InvalidState, "Pooled command list requires CommandPool::reset() before re-recording");
            if (submitted > qSlot.completed.load(std::memory_order_acquire)) return fail(ErrorCode::InvalidState, "Metal command list is still in flight");
            auto next = std::make_shared<NativeRecording>();
            {
                std::scoped_lock recLock(owner->recordingsMutex);
                std::erase_if(owner->recordings, [](auto& w) { return w.expired(); });
                owner->recordings.push_back(next);
            }
            clearRecordingState();
            native = std::move(next);
            open = true;
            recordedEpoch = pool->epoch;
            ++pool->openCount;
            return {};
        }
        if (auto ok = owner->pollQueue(qType); !ok) return ok;
        if (open || submitted > qSlot.completed.load(std::memory_order_acquire)) return fail(ErrorCode::InvalidState, "Metal command list is open or in flight");
        auto next = std::make_shared<NativeRecording>();
        {
            std::scoped_lock recLock(owner->recordingsMutex);
            std::erase_if(owner->recordings, [](auto& w) { return w.expired(); });
            owner->recordings.push_back(next);
        }
        clearRecordingState();
        native = std::move(next);
        open = true;
        return {};
    }
    Result<void> end() override {
        if (auto ok = ready(); !ok) return ok;
        if (inPass) return fail(ErrorCode::InvalidState, "Metal render pass must be ended first");
        // A recording that may become stale must not keep a drawable texture
        // alive in an unsubmitted native command buffer.
        if (!usesDrawable()) {
            if (auto ok = encode(); !ok) return ok;
        }
        open = false;
        ended = true;
        if (pool) {
            std::scoped_lock lock(pool->mutex);
            if (pool->openCount > 0) --pool->openCount;
        }
        return {};
    }
    bool usesDrawable() const {
        return std::any_of(uses.begin(), uses.end(), [](const Use& use) { return use.allocation->backbuffer; });
    }
    Result<void> encode() {
        // Finalize into a fresh native buffer. An encoder/compiler failure discards that
        // uncommitted buffer and leaves the accepted CPU recording open and retryable.
        @autoreleasepool {
            if (std::any_of(operations.begin(), operations.end(), [](const auto& op) {
                return op.kind == OperationKind::Buffer && op.bytes &&
                    ((op.bytes & 3) || (op.sourceOffset & 3) || (op.destinationOffset & 3));
            }))
                if (auto ok = owner->prepareTail(); !ok) return ok;
            id<MTLCommandBuffer> next = [owner->queues[queueIndex(qType)].queue commandBuffer];
            if (!next) return fail(ErrorCode::OutOfMemory, "Metal command buffer creation failed");
            for (const auto& op : operations) {
                if (op.kind == OperationKind::Compute) {
                    const auto& c = op.compute;
                    const auto& p = *c.pipeline;
                    id<MTLComputeCommandEncoder> encoder = [next computeCommandEncoder];
                    if (!encoder) return fail(ErrorCode::BackendFailure, "Metal compute encoder creation failed");
                    [encoder setComputePipelineState:p.computePipeline];
                    std::size_t offset = 0;
                    for (const auto& entry : p.bindings.entries) {
                        for (unsigned element = 0; element < entry.count; ++element) {
                            const auto& view = (*c.bindings)[offset++];
                            if (isSampledTextureBinding(entry.type) || entry.type == BindingType::StorageTexture2D) {
                                id<MTLTexture> tex = view.textureView ? view.textureView->texture : view.allocation->texture;
                                [encoder setTexture:tex atIndex:entry.slot + element];
                            } else {
                                [encoder setBuffer:view.allocation->buffer offset:view.offset atIndex:entry.slot + element];
                            }
                        }
                    }
                    for (std::size_t i = 0; i < p.bindings.samplers.size(); ++i) {
                        [encoder setSamplerState:p.samplers[i] atIndex:p.bindings.samplers[i].slot];
                    }
                    if (c.indirect) {
                        [encoder dispatchThreadgroupsWithIndirectBuffer:c.indirectBuffer->buffer
                                                   indirectBufferOffset:static_cast<NSUInteger>(c.indirectOffset)
                                                  threadsPerThreadgroup:MTLSizeMake(p.threadgroupSizeX, p.threadgroupSizeY, p.threadgroupSizeZ)];
                    } else {
                        [encoder dispatchThreadgroups:MTLSizeMake(c.groupX, c.groupY, c.groupZ)
                                threadsPerThreadgroup:MTLSizeMake(p.threadgroupSizeX, p.threadgroupSizeY, p.threadgroupSizeZ)];
                    }
                    [encoder endEncoding];
                    continue;
                }
                if (op.kind == OperationKind::Resolve) {
                    MTLRenderPassDescriptor* descriptor = [MTLRenderPassDescriptor renderPassDescriptor];
                    if (!descriptor) return fail(ErrorCode::OutOfMemory, "Metal resolve pass descriptor allocation failed");
                    descriptor.colorAttachments[0].texture = op.source->texture;
                    descriptor.colorAttachments[0].loadAction = MTLLoadActionLoad;
                    descriptor.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
                    descriptor.colorAttachments[0].resolveTexture = op.destination->texture;
                    id<MTLRenderCommandEncoder> encoder = [next renderCommandEncoderWithDescriptor:descriptor];
                    if (!encoder) return fail(ErrorCode::BackendFailure, "Metal resolve encoder creation failed");
                    [encoder endEncoding];
                    continue;
                }
                if (op.kind == OperationKind::Pass) {
                    MTLRenderPassDescriptor* descriptor = [MTLRenderPassDescriptor renderPassDescriptor];
                    if (!descriptor) return fail(ErrorCode::OutOfMemory, "Metal pass descriptor allocation failed");
                    unsigned index = 0;
                    for (const auto& a : op.attachments) {
                        id<MTLTexture> targetTexture = a.textureView ? a.textureView->texture : a.allocation->texture;
                        if (a.allocation->textureDesc.depthStencil) {
                            descriptor.depthAttachment.texture = targetTexture;
                            descriptor.depthAttachment.loadAction = a.load == LoadOp::Clear ? MTLLoadActionClear : MTLLoadActionLoad;
                            descriptor.depthAttachment.storeAction = MTLStoreActionStore;
                            if (a.load == LoadOp::Clear) descriptor.depthAttachment.clearDepth = a.clearDepth;
                        } else {
                            auto color = descriptor.colorAttachments[index++];
                            color.texture = targetTexture;
                            color.loadAction = a.load == LoadOp::Clear ? MTLLoadActionClear : MTLLoadActionLoad;
                            color.storeAction = MTLStoreActionStore;
                            if (a.load == LoadOp::Clear) color.clearColor = MTLClearColorMake(a.clear.r,a.clear.g,a.clear.b,a.clear.a);
                        }
                    }
                    std::vector<NSString*> labels;
                    for (const auto& text : op.markers) {
                        NSString* label = [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
                        if (!label) return fail(ErrorCode::OutOfMemory, "Metal pass label allocation failed");
                        labels.push_back(label);
                    }
                    id<MTLRenderCommandEncoder> encoder = [next renderCommandEncoderWithDescriptor:descriptor];
                    if (!encoder) return fail(ErrorCode::BackendFailure, "Metal render encoder creation failed");
                    const auto& firstAtt = op.attachments.front();
                    const auto& extent = firstAtt.allocation->textureDesc;
                    const std::uint32_t firstMip = firstAtt.textureView ? firstAtt.textureView->desc.baseMipLevel : 0u;
                    const std::uint32_t passW = std::max(1u, extent.width >> firstMip);
                    const std::uint32_t passH = std::max(1u, extent.height >> firstMip);
                    [encoder setViewport:MTLViewport{0,0,double(passW),double(passH),0,1}];
                    [encoder setCullMode:MTLCullModeNone];
                    for (const auto& item : op.order) {
                        if (!item.first) { [encoder insertDebugSignpost:labels[item.second]]; continue; }
                        const auto& draw = op.draws[item.second]; const auto& p = *draw.pipeline;
                        [encoder setRenderPipelineState:p.pipeline]; [encoder setDepthStencilState:p.depth];
                        [encoder setViewport:draw.viewport]; [encoder setScissorRect:draw.scissor];
                        for (const auto& slot : p.vertices.buffers) {
                            const auto& view = draw.vertices[slot.slot];
                            [encoder setVertexBuffer:view.allocation->buffer offset:view.offset atIndex:16+slot.slot];
                        }
                        std::size_t offset = 0;
                        for (const auto& entry : p.bindings.entries) for (unsigned element=0;element<entry.count;++element) {
                            const auto& view = (*draw.bindings)[offset++];
                            for (unsigned stage=0;stage<2;++stage) if (visible(entry.visibility,stage)) {
                                if (isSampledTextureBinding(entry.type)) {
                                    id<MTLTexture> sampledTexture = view.textureView ? view.textureView->texture : view.allocation->texture;
                                    if (stage==0) [encoder setVertexTexture:sampledTexture atIndex:entry.slot+element];
                                    else [encoder setFragmentTexture:sampledTexture atIndex:entry.slot+element];
                                } else {
                                    if (stage==0) [encoder setVertexBuffer:view.allocation->buffer offset:view.offset atIndex:entry.slot+element];
                                    else [encoder setFragmentBuffer:view.allocation->buffer offset:view.offset atIndex:entry.slot+element];
                                }
                            }
                        }
                        for (std::size_t i=0;i<p.bindings.samplers.size();++i) {
                            const auto& sampler = p.bindings.samplers[i];
                            if (visible(sampler.visibility,0)) [encoder setVertexSamplerState:p.samplers[i] atIndex:sampler.slot];
                            if (visible(sampler.visibility,1)) [encoder setFragmentSamplerState:p.samplers[i] atIndex:sampler.slot];
                        }
                        if (draw.indirect) {
                            for (std::uint32_t d = 0; d < draw.indirectDrawCount; ++d) {
                                const NSUInteger argOff = static_cast<NSUInteger>(draw.indirectArgs.offset + std::uint64_t(d) * draw.indirectStride);
                                if (draw.indexed) {
                                    [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                                         indexType:draw.indices.stride == 2 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32
                                                       indexBuffer:draw.indices.allocation->buffer
                                                 indexBufferOffset:static_cast<NSUInteger>(draw.indices.offset)
                                                    indirectBuffer:draw.indirectArgs.allocation->buffer
                                              indirectBufferOffset:argOff];
                                } else {
                                    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                                             indirectBuffer:draw.indirectArgs.allocation->buffer
                                       indirectBufferOffset:argOff];
                                }
                            }
                        } else if (draw.indexed) [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:draw.count
                            indexType:draw.indices.stride==2?MTLIndexTypeUInt16:MTLIndexTypeUInt32 indexBuffer:draw.indices.allocation->buffer
                            indexBufferOffset:draw.indices.offset+std::uint64_t(draw.first)*draw.indices.stride instanceCount:draw.instances
                            baseVertex:draw.baseVertex baseInstance:draw.firstInstance];
                        else [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:draw.first vertexCount:draw.count instanceCount:draw.instances baseInstance:draw.firstInstance];
                    }
                    [encoder endEncoding];
                    continue;
                }
                if (op.kind == OperationKind::UploadTexture || op.kind == OperationKind::ReadTexture) {
                    const bool upload = op.kind == OperationKind::UploadTexture;
                    auto image = upload ? op.destination : op.source;
                    auto host = upload ? op.source : op.destination;
                    const auto& r = op.textureRegion;
                    id<MTLBlitCommandEncoder> encoder = [next blitCommandEncoder];
                    if (!encoder) return fail(ErrorCode::OutOfMemory, "Metal texture blit encoder creation failed");
                    if (upload) {
                        [encoder copyFromBuffer:host->buffer sourceOffset:r.bufferOffset
                            sourceBytesPerRow:op.textureRowPitch sourceBytesPerImage:op.textureImageBytes
                            sourceSize:MTLSizeMake(r.width,r.height,r.depth) toTexture:image->texture
                            destinationSlice:r.arrayLayer destinationLevel:r.mipLevel destinationOrigin:MTLOriginMake(r.x,r.y,r.z)];
                    } else {
                        [encoder copyFromTexture:image->texture sourceSlice:r.arrayLayer sourceLevel:r.mipLevel
                            sourceOrigin:MTLOriginMake(r.x,r.y,r.z) sourceSize:MTLSizeMake(r.width,r.height,r.depth)
                            toBuffer:host->buffer destinationOffset:r.bufferOffset
                            destinationBytesPerRow:op.textureRowPitch destinationBytesPerImage:op.textureImageBytes];
                        if (host->buffer.storageMode == MTLStorageModeManaged) [encoder synchronizeResource:host->buffer];
                    }
                    [encoder endEncoding];
                    continue;
                }
                if (!op.bytes) {
                    NSString* label = [[NSString alloc] initWithBytes:op.marker.data() length:op.marker.size() encoding:NSUTF8StringEncoding];
                    if (!label) return fail(ErrorCode::OutOfMemory, "Metal marker label allocation failed");
                    id<MTLBlitCommandEncoder> encoder = [next blitCommandEncoder];
                    if (!encoder) return fail(ErrorCode::OutOfMemory, "Metal marker encoder creation failed");
                    [encoder insertDebugSignpost:label]; [encoder endEncoding];
                    continue;
                }
                const bool offsetsAligned = ((op.sourceOffset & 3) == 0) && ((op.destinationOffset & 3) == 0);
                const auto aligned = offsetsAligned ? (op.bytes & ~std::uint64_t{3}) : std::uint64_t{0};
                if (aligned) {
                    id<MTLBlitCommandEncoder> encoder = [next blitCommandEncoder];
                    if (!encoder) return fail(ErrorCode::OutOfMemory, "Metal blit encoder creation failed");
                    [encoder copyFromBuffer:op.source->buffer sourceOffset:op.sourceOffset toBuffer:op.destination->buffer destinationOffset:op.destinationOffset size:aligned];
                    [encoder endEncoding];
                }
                if (op.bytes != aligned) {
                    id<MTLComputeCommandEncoder> encoder = [next computeCommandEncoder];
                    if (!encoder) return fail(ErrorCode::OutOfMemory, "Metal tail encoder creation failed");
                    [encoder setComputePipelineState:owner->tailPipeline];
                    [encoder setBuffer:op.source->buffer offset:0 atIndex:0];
                    [encoder setBuffer:op.destination->buffer offset:0 atIndex:1];
                    const auto srcOff = op.sourceOffset + aligned;
                    const auto dstOff = op.destinationOffset + aligned;
                    const auto count = op.bytes - aligned;
                    [encoder setBytes:&srcOff length:sizeof(srcOff) atIndex:2];
                    [encoder setBytes:&dstOff length:sizeof(dstOff) atIndex:3];
                    [encoder setBytes:&count length:sizeof(count) atIndex:4];
                    const auto maxThreads = std::max<NSUInteger>(1, std::min<NSUInteger>(256, owner->tailPipeline.maxTotalThreadsPerThreadgroup));
                    const auto width = static_cast<NSUInteger>(std::min<std::uint64_t>(count, maxThreads));
                    [encoder dispatchThreadgroups:MTLSizeMake(1,1,1) threadsPerThreadgroup:MTLSizeMake(width,1,1)];
                    [encoder endEncoding];
                }
                if (op.destination->buffer.storageMode == MTLStorageModeManaged) {
                    id<MTLBlitCommandEncoder> encoder = [next blitCommandEncoder];
                    if (!encoder) return fail(ErrorCode::OutOfMemory, "Metal synchronization encoder creation failed");
                    [encoder synchronizeResource:op.destination->buffer]; [encoder endEncoding];
                }
            }
            native->buffer = next;
        }
        return {};
    }
    Result<void> debugMarker(std::string_view text) override {
        if (auto ok = ready(); !ok) return ok;
        @autoreleasepool {
            NSString* label = [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
            if (!label || text.empty() || text.find('\0') != std::string_view::npos)
                return fail(ErrorCode::InvalidArgument, "Metal marker requires nonempty UTF-8 without NUL");
            if (inPass) {
                auto& op = operations[passOperation]; op.markers.reserve(op.markers.size()+1);op.order.reserve(op.order.size()+1);
                op.markers.emplace_back(text);op.order.emplace_back(false,op.markers.size()-1);
            }
            else operations.push_back(Operation{{},{},0,std::string(text)});
        }
        return {};
    }
    Result<void> ready() const {
        if (pool) {
            if (pool->recordingThread.load(std::memory_order_acquire) != std::this_thread::get_id())
                return fail(ErrorCode::InvalidState, "Metal command recording must run on the pool's bound recording thread");
            if (auto ok = owner->checkAliveForWorker(); !ok) return ok;
            if (!pool->ownerAlive) return fail(ErrorCode::InvalidState, "Owning CommandPool has been destroyed");
        } else {
            if (auto ok = owner->check(); !ok) return ok;
        }
        if (!open) return fail(ErrorCode::InvalidState, "Metal command list is not recording");
        return {};
    }
    Result<void> outsidePass() const {
        if (auto ok = ready(); !ok) return ok;
        if (inPass) return fail(ErrorCode::InvalidState, "Metal operation requires no active render pass");
        return {};
    }
    Result<std::shared_ptr<Allocation>> allocation(const BufferPtr& value) const {
        auto buffer = std::dynamic_pointer_cast<MetalBuffer>(value);
        if (!buffer) return fail(value ? ErrorCode::WrongDevice : ErrorCode::InvalidArgument, "Expected a Metal buffer");
        if (buffer->owner != owner) return fail(ErrorCode::WrongDevice, "Metal buffer belongs to another generation");
        return buffer->allocation;
    }
    Result<std::shared_ptr<Allocation>> allocation(const TexturePtr& value) const;
    Result<std::shared_ptr<NativeTextureView>> textureView(const TextureViewPtr& value) const {
        if (!value) return fail(ErrorCode::InvalidArgument, "Null Metal texture view");
        auto view = std::dynamic_pointer_cast<MetalTextureView>(value);
        if (!view || view->owner != owner) return fail(ErrorCode::WrongDevice, "Metal texture view belongs to another generation");
        if (!view->native || !view->native->allocation || (view->native->allocation->backbuffer && !view->native->allocation->texture))
            return fail(ErrorCode::InvalidState, "Metal drawable is no longer current");
        return view->native;
    }
    Use* find(const std::shared_ptr<Allocation>& a) {
        auto it = std::find_if(uses.begin(), uses.end(), [&](auto& u) { return u.allocation == a; });
        return it == uses.end() ? nullptr : &*it;
    }
    ResourceState shadow(const std::shared_ptr<Allocation>& a) { auto u = find(a); return u ? u->shadow : a->state; }
    Use& retain(const std::shared_ptr<Allocation>& a, bool markMutated = false) {
        if (auto u = find(a)) {
            if (markMutated) u->mutated = true;
            return *u;
        }
        return uses.emplace_back(Use{a,a->version,a->state,a->initialized,markMutated});
    }
    Result<void> transition(const BufferPtr& value, ResourceState target) override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (qType == QueueType::Copy && target != ResourceState::Common && target != ResourceState::CopySource && target != ResourceState::CopyDestination)
            return fail(ErrorCode::InvalidArgument, "Metal Copy command list only supports Common, CopySource, and CopyDestination buffer states");
        if (qType == QueueType::Compute && (vertexState(target) || indexState(target)))
            return fail(ErrorCode::InvalidArgument, "Metal Compute command list does not support vertex/index buffer states");
        auto a = allocation(value); if (!a) return std::unexpected(a.error());
        if (target != ResourceState::Common && target != ResourceState::CopySource && target != ResourceState::CopyDestination &&
            !vertexState(target) && !indexState(target) && target != ResourceState::ConstantBuffer && target != ResourceState::UnorderedAccess && target != ResourceState::IndirectArgument)
            return fail(ErrorCode::Unsupported, "Metal buffer state is outside the transfer/compute slice");
        if (target == ResourceState::UnorderedAccess && !(*a)->desc.storage)
            return fail(ErrorCode::InvalidArgument, "Metal UnorderedAccess requires a storage buffer");
        if (((*a)->desc.memory == Memory::Upload && target != ResourceState::CopySource && !vertexState(target) && !indexState(target) && target != ResourceState::ConstantBuffer && target != ResourceState::IndirectArgument) ||
            ((*a)->desc.memory == Memory::Readback && target != ResourceState::CopyDestination))
            return fail(ErrorCode::InvalidArgument, "Metal host buffer state is fixed");
        const auto before = shadow(*a);
        if (qType == QueueType::Copy && (*a)->desc.memory == Memory::Device &&
            before != ResourceState::Common && before != ResourceState::CopySource && before != ResourceState::CopyDestination)
            return fail(ErrorCode::InvalidState, "Metal Copy command list cannot transition buffer from a non-copy state");
        if (qType == QueueType::Compute && (vertexState(before) || indexState(before)))
            return fail(ErrorCode::InvalidState, "Metal Compute command list cannot transition buffer from a vertex/index state");
        uses.reserve(uses.size()+1);
        auto& u = retain(*a);
        if (u.shadow != target) u.mutated = true;
        u.shadow = target;
        // Explicitly tracked native resources synchronize accesses between encoders on this queue.
        return {};
    }
    Result<void> transition(const TexturePtr& value, ResourceState target) override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (qType == QueueType::Copy && target != ResourceState::Common && target != ResourceState::CopySource && target != ResourceState::CopyDestination)
            return fail(ErrorCode::InvalidArgument, "Metal Copy command list only supports Common, CopySource, and CopyDestination texture states");
        if (qType == QueueType::Compute && (target == ResourceState::RenderTarget || target == ResourceState::DepthWrite || target == ResourceState::DepthRead || target == ResourceState::Present || target == ResourceState::ResolveSource || target == ResourceState::ResolveDestination))
            return fail(ErrorCode::InvalidArgument, "Metal Compute command list does not support graphics/present/resolve texture states");
        auto a = allocation(value); if (!a) return std::unexpected(a.error());
        const bool multisampled = (*a)->textureDesc.sampleCount > 1u;
        if (target == ResourceState::IndirectArgument)
            return fail(ErrorCode::InvalidArgument, "IndirectArgument state is only valid for buffers");
        if ((*a)->backbuffer) {
            if (target != ResourceState::RenderTarget && target != ResourceState::CopySource && target != ResourceState::Present)
                return fail(ErrorCode::Unsupported, "Metal drawable supports RenderTarget, CopySource and Present");
        } else if (target != ResourceState::Common && target != ResourceState::CopySource && target != ResourceState::CopyDestination &&
            target != ResourceState::RenderTarget && target != ResourceState::DepthWrite && target != ResourceState::DepthRead && target != ResourceState::ShaderRead && target != ResourceState::UnorderedAccess &&
            target != ResourceState::ResolveSource && target != ResourceState::ResolveDestination)
            return fail(ErrorCode::Unsupported, "Metal texture state is outside the transfer/pass/compute slice");
        if ((target == ResourceState::ResolveSource || target == ResourceState::ResolveDestination) && ((*a)->textureDesc.depthStencil || (*a)->backbuffer))
            return fail(ErrorCode::Unsupported, "Metal depth or drawable resolve transition is unsupported");
        if (target == ResourceState::ResolveDestination && multisampled)
            return fail(ErrorCode::InvalidArgument, "Metal multisampled texture cannot be a resolve destination");
        if ((target == ResourceState::CopySource || target == ResourceState::CopyDestination) && multisampled)
            return fail(ErrorCode::InvalidArgument, "Metal multisampled texture does not support direct copy states; use ResolveSource");
        if (target == ResourceState::ShaderRead && multisampled)
            return fail(ErrorCode::Unsupported, "Metal multisampled texture sampling unsupported");
        if (target == ResourceState::UnorderedAccess && !(*a)->textureDesc.storage)
            return fail(ErrorCode::InvalidArgument, "Metal UnorderedAccess requires a storage texture");
        if (target == ResourceState::RenderTarget && !(*a)->textureDesc.renderTarget)
            return fail(ErrorCode::InvalidArgument, "Metal color attachment usage required");
        if ((target == ResourceState::DepthWrite || target == ResourceState::DepthRead) && !(*a)->textureDesc.depthStencil)
            return fail(ErrorCode::InvalidArgument, "Metal depth attachment usage required");
        if (target == ResourceState::ShaderRead && (*a)->textureDesc.depthStencil) return fail(ErrorCode::Unsupported,"Metal depth sampling unsupported");
        const auto before = shadow(*a);
        if (qType == QueueType::Copy &&
            before != ResourceState::Common && before != ResourceState::CopySource && before != ResourceState::CopyDestination)
            return fail(ErrorCode::InvalidState, "Metal Copy command list cannot transition texture from a non-copy state");
        if (qType == QueueType::Compute &&
            (before == ResourceState::RenderTarget || before == ResourceState::DepthWrite || before == ResourceState::DepthRead ||
             before == ResourceState::Present || before == ResourceState::ResolveSource || before == ResourceState::ResolveDestination))
            return fail(ErrorCode::InvalidState, "Metal Compute command list cannot transition texture from a graphics/present/resolve state");
        uses.reserve(uses.size()+1);
        auto& u = retain(*a);
        if (u.shadow != target) u.mutated = true;
        u.shadow = target;
        return {};
    }
    Result<void> aliasingBarrier(const BufferPtr& before, const BufferPtr& after) override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (!after) return fail(ErrorCode::InvalidArgument, "Aliasing barrier requires a non-null destination buffer");
        auto dst = allocation(after); if (!dst) return std::unexpected(dst.error());
        if (!(*dst)->heap) return fail(ErrorCode::InvalidArgument, "Aliasing barrier requires placed resources");
        std::shared_ptr<Allocation> src;
        if (before) {
            auto checked = allocation(before); if (!checked) return std::unexpected(checked.error());
            src = std::move(*checked);
            if (!src->heap) return fail(ErrorCode::InvalidArgument, "Aliasing barrier requires placed resources");
            if (src == *dst) return fail(ErrorCode::InvalidArgument, "Aliasing barrier requires distinct before and after resources");
        }
        uses.reserve(uses.size() + 2);
        if (src) retain(src);
        retain(*dst, true);
        if (owner->profiler) owner->profiler->recordBarrier();
        return {};
    }
    Result<void> aliasingBarrier(const TexturePtr& before, const TexturePtr& after) override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (!after) return fail(ErrorCode::InvalidArgument, "Aliasing barrier requires a non-null destination texture");
        auto dst = allocation(after); if (!dst) return std::unexpected(dst.error());
        if (!(*dst)->heap) return fail(ErrorCode::InvalidArgument, "Aliasing barrier requires placed resources");
        std::shared_ptr<Allocation> src;
        if (before) {
            auto checked = allocation(before); if (!checked) return std::unexpected(checked.error());
            src = std::move(*checked);
            if (!src->heap) return fail(ErrorCode::InvalidArgument, "Aliasing barrier requires placed resources");
            if (src == *dst) return fail(ErrorCode::InvalidArgument, "Aliasing barrier requires distinct before and after resources");
        }
        uses.reserve(uses.size() + 2);
        if (src) retain(src);
        auto& dstUse = retain(*dst, true);
        dstUse.initialized = false;
        dstUse.shadow = ResourceState::Common;
        if (owner->profiler) owner->profiler->recordBarrier();
        return {};
    }
    Result<void> copyBuffer(const BufferPtr& source, const BufferPtr& destination, std::uint64_t bytes) override {
        return copyBufferRegion(source, 0, destination, 0, bytes);
    }
    Result<void> copyBufferRegion(const BufferPtr& source, std::uint64_t sourceOffset, const BufferPtr& destination, std::uint64_t destinationOffset, std::uint64_t bytes) override {
        if (auto ok = outsidePass(); !ok) return ok;
        auto src = allocation(source); if (!src) return std::unexpected(src.error());
        auto dst = allocation(destination); if (!dst) return std::unexpected(dst.error());
        if (!bytes ||
            sourceOffset > (*src)->desc.size || bytes > (*src)->desc.size - sourceOffset ||
            destinationOffset > (*dst)->desc.size || bytes > (*dst)->desc.size - destinationOffset ||
            *src == *dst ||
            (*src)->desc.memory == Memory::Readback || (*dst)->desc.memory == Memory::Upload)
            return fail(ErrorCode::InvalidArgument, "Metal copy range or alias invalid");
        if (shadow(*src) != ResourceState::CopySource || shadow(*dst) != ResourceState::CopyDestination)
            return fail(ErrorCode::InvalidState, "Metal copy requires source and destination states");
        uses.reserve(uses.size()+2); operations.reserve(operations.size()+1);
        retain(*src); retain(*dst, true);
        operations.push_back(Operation{*src,*dst,bytes,{},sourceOffset,destinationOffset});
        return {};
    }
    bool initialized(const std::shared_ptr<Allocation>& a) { auto u = find(a); return u ? u->initialized : a->initialized; }
    Result<void> textureCopy(const std::shared_ptr<Allocation>& image, const std::shared_ptr<Allocation>& host, bool upload) {
        if (image->textureDesc.sampleCount > 1u)
            return fail(ErrorCode::InvalidArgument, "Metal direct buffer copy is invalid for multisampled textures; resolve first");
        if (host->desc.size < image->footprint.size || (upload ? host->desc.memory == Memory::Readback : host->desc.memory == Memory::Upload))
            return fail(ErrorCode::InvalidArgument, "Metal texture copy footprint/memory invalid");
        if (shadow(image) != (upload ? ResourceState::CopyDestination : ResourceState::CopySource) ||
            shadow(host) != (upload ? ResourceState::CopySource : ResourceState::CopyDestination))
            return fail(ErrorCode::InvalidState, "Metal texture copy requires explicit transfer states");
        if (!upload && !initialized(image)) return fail(ErrorCode::InvalidState, "Metal texture contents uninitialized");
        uses.reserve(uses.size()+2); operations.reserve(operations.size()+1);
        retain(image, upload); retain(host, !upload);
        Operation op; op.source = upload ? host : image; op.destination = upload ? image : host;
        const std::uint32_t sliceH = image->textureDesc.dimension == TextureDimension::Texture1D ? 1u : image->textureDesc.height;
        const std::uint32_t depth0 = image->textureDesc.dimension == TextureDimension::Texture3D ? image->textureDesc.depth : 1u;
        op.textureRegion = {0, image->footprint.rowPitch, 0, 0, image->textureDesc.width, sliceH, 0, 0, 0, depth0};
        op.textureRowPitch = image->footprint.rowPitch;
        op.textureImageBytes = static_cast<std::uint64_t>(image->footprint.rowPitch) * sliceH;
        op.kind = upload ? OperationKind::UploadTexture : OperationKind::ReadTexture;
        operations.push_back(std::move(op));
        if (upload) find(image)->initialized = true;
        return {};
    }
    Result<void> textureCopyRegion(const std::shared_ptr<Allocation>& image, const std::shared_ptr<Allocation>& host, const TextureCopyRegion& region, bool upload) {
        const auto& desc = image->textureDesc;
        if (desc.sampleCount > 1u)
            return fail(ErrorCode::InvalidArgument, "Metal direct buffer copy is invalid for multisampled textures; resolve first");
        if (region.mipLevel >= desc.mipLevels || region.arrayLayer >= desc.arrayLayers || !region.depth)
            return fail(ErrorCode::InvalidArgument, "Metal texture copy region subresource or depth invalid");
        const std::uint32_t mipW = std::max(1u, desc.width >> region.mipLevel);
        const std::uint32_t mipH = desc.dimension == TextureDimension::Texture1D ? 1u : std::max(1u, desc.height >> region.mipLevel);
        const std::uint32_t mipD = desc.dimension == TextureDimension::Texture3D ? std::max(1u, desc.depth >> region.mipLevel) : 1u;
        const auto blockInfo = queryFormatBlockInfo(desc.format);
        if (!blockInfo) return std::unexpected(blockInfo.error());
        auto footprint = computeTextureRegionFootprint(desc.format, region.width, region.height, region.bufferRowPitch);
        if (!footprint) return std::unexpected(footprint.error());
        if (region.x > mipW || region.width > mipW - region.x ||
            region.y > mipH || region.height > mipH - region.y ||
            region.z > mipD || region.depth > mipD - region.z)
            return fail(ErrorCode::InvalidArgument, "Metal texture copy region exceeds subresource dimensions");
        if (blockInfo->compressed) {
            if ((region.x % blockInfo->blockWidth) != 0u || (region.y % blockInfo->blockHeight) != 0u)
                return fail(ErrorCode::InvalidArgument, "Metal compressed texture copy origin must be block-aligned");
            const bool alignedW = (region.width % blockInfo->blockWidth) == 0u || (region.x + region.width == mipW);
            const bool alignedH = (region.height % blockInfo->blockHeight) == 0u || (region.y + region.height == mipH);
            if (!alignedW || !alignedH)
                return fail(ErrorCode::InvalidArgument, "Metal compressed texture copy extent must be block-aligned or reach the subresource edge");
        }
        if (footprint->size > UINT64_MAX / region.depth)
            return fail(ErrorCode::InvalidArgument, "Metal texture copy region byte size overflow");
        const std::uint64_t totalBytes = footprint->size * region.depth;
        const bool fullSlice = region.x == 0 && region.y == 0 && region.width == mipW && region.height == mipH;
        const bool fullExtent = fullSlice && region.z == 0 && region.depth == mipD;
        if (desc.depthStencil && !fullExtent)
            return fail(ErrorCode::Unsupported, "Partial sub-region copy is unsupported for depth textures");
        if ((region.bufferOffset % 512u) != 0 ||
            region.bufferOffset > host->desc.size || totalBytes > host->desc.size - region.bufferOffset ||
            (upload ? host->desc.memory == Memory::Readback : host->desc.memory == Memory::Upload))
            return fail(ErrorCode::InvalidArgument, "Metal texture region copy buffer alignment, bounds or memory kind invalid");
        if (shadow(image) != (upload ? ResourceState::CopyDestination : ResourceState::CopySource) ||
            shadow(host) != (upload ? ResourceState::CopySource : ResourceState::CopyDestination))
            return fail(ErrorCode::InvalidState, "Metal texture region copy requires explicit transfer states");
        if (!upload && !initialized(image)) return fail(ErrorCode::InvalidState, "Metal texture contents uninitialized");
        uses.reserve(uses.size()+2); operations.reserve(operations.size()+1);
        retain(image, upload); retain(host, !upload);
        Operation op; op.source = upload ? host : image; op.destination = upload ? image : host;
        op.textureRegion = region;
        op.textureRowPitch = footprint->rowPitch;
        op.textureImageBytes = footprint->size;
        op.kind = upload ? OperationKind::UploadTexture : OperationKind::ReadTexture;
        operations.push_back(std::move(op));
        if (upload && fullSlice) find(image)->initialized = true;
        return {};
    }
    Result<void> copyBufferToTexture(const BufferPtr& source, const TexturePtr& destination) override {
        if (auto ok = outsidePass(); !ok) return ok;
        auto src = allocation(source); if (!src) return std::unexpected(src.error());
        auto dst = allocation(destination); if (!dst) return std::unexpected(dst.error());
        return textureCopy(*dst,*src,true);
    }
    Result<void> copyTextureToBuffer(const TexturePtr& source, const BufferPtr& destination) override {
        if (auto ok = outsidePass(); !ok) return ok;
        auto src = allocation(source); if (!src) return std::unexpected(src.error());
        auto dst = allocation(destination); if (!dst) return std::unexpected(dst.error());
        return textureCopy(*src,*dst,false);
    }
    Result<void> copyBufferToTextureRegion(const BufferPtr& source, const TexturePtr& destination, const TextureCopyRegion& region) override {
        if (auto ok = outsidePass(); !ok) return ok;
        auto src = allocation(source); if (!src) return std::unexpected(src.error());
        auto dst = allocation(destination); if (!dst) return std::unexpected(dst.error());
        return textureCopyRegion(*dst, *src, region, true);
    }
    Result<void> copyTextureToBufferRegion(const TexturePtr& source, const BufferPtr& destination, const TextureCopyRegion& region) override {
        if (auto ok = outsidePass(); !ok) return ok;
        auto src = allocation(source); if (!src) return std::unexpected(src.error());
        auto dst = allocation(destination); if (!dst) return std::unexpected(dst.error());
        return textureCopyRegion(*src, *dst, region, false);
    }
    Result<void> resolveTexture(const TexturePtr& source, const TexturePtr& destination) override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (qType != QueueType::Graphics) return fail(ErrorCode::InvalidArgument, "Metal resolveTexture requires a Graphics command list");
        auto src = allocation(source); if (!src) return std::unexpected(src.error());
        auto dst = allocation(destination); if (!dst) return std::unexpected(dst.error());
        if (*src == *dst) return fail(ErrorCode::InvalidArgument, "Metal resolve source and destination must be distinct textures");
        if ((*src)->textureDesc.depthStencil || (*dst)->textureDesc.depthStencil)
            return fail(ErrorCode::Unsupported, "Metal depth texture resolve is unsupported");
        if ((*src)->backbuffer || (*dst)->backbuffer)
            return fail(ErrorCode::Unsupported, "Metal drawable resolve is unsupported");
        if ((*src)->textureDesc.sampleCount <= 1u || (*dst)->textureDesc.sampleCount != 1u)
            return fail(ErrorCode::InvalidArgument, "Metal resolveTexture requires multisampled source and single-sampled destination");
        if ((*src)->textureDesc.width != (*dst)->textureDesc.width ||
            (*src)->textureDesc.height != (*dst)->textureDesc.height ||
            (*src)->textureDesc.format != (*dst)->textureDesc.format)
            return fail(ErrorCode::InvalidArgument, "Metal resolve source and destination extent/format must match");
        if (shadow(*src) != ResourceState::ResolveSource || shadow(*dst) != ResourceState::ResolveDestination)
            return fail(ErrorCode::InvalidState, "Metal resolveTexture requires ResolveSource and ResolveDestination states");
        if (!initialized(*src)) return fail(ErrorCode::InvalidState, "Metal resolve source texture contents uninitialized");
        uses.reserve(uses.size() + 2);
        operations.reserve(operations.size() + 1);
        retain(*src);
        retain(*dst, true).initialized = true;
        Operation op;
        op.kind = OperationKind::Resolve;
        op.source = *src;
        op.destination = *dst;
        operations.push_back(std::move(op));
        return {};
    }
    Result<void> beginRenderPass(const TexturePtr& value, Color clear) override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (qType != QueueType::Graphics) return fail(ErrorCode::InvalidArgument, "Metal beginRenderPass requires a Graphics command list");
        return beginRenderPass(RenderPassDesc{{{value,LoadOp::Clear,clear}},{}});
    }
    Result<void> beginRenderPass(const RenderPassDesc& desc) override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (qType != QueueType::Graphics) return fail(ErrorCode::InvalidArgument, "Metal beginRenderPass requires a Graphics command list");
        if (auto ok = validateRenderPass(desc); !ok) return ok;
        Operation op; op.kind = OperationKind::Pass;
        for (const auto& color : desc.colors) {
            std::shared_ptr<Allocation> a;
            std::shared_ptr<NativeTextureView> v;
            if (color.view) {
                auto found = textureView(color.view); if (!found) return std::unexpected(found.error());
                if ((*found)->desc.usage != TextureViewUsage::RenderTarget)
                    return fail(ErrorCode::InvalidArgument, "Metal color attachment view requires RenderTarget usage");
                v = *found; a = v->allocation;
            } else {
                auto found = allocation(color.texture); if (!found) return std::unexpected(found.error());
                a = *found;
            }
            if (shadow(a) != ResourceState::RenderTarget || (color.load == LoadOp::Load && !initialized(a)))
                return fail(ErrorCode::InvalidState, "Metal color pass requires state and initialized Load contents");
            op.attachments.push_back(Attachment{a,color.load,color.clear,0,false,v});
        }
        if (desc.depth.texture || desc.depth.view) {
            std::shared_ptr<Allocation> a;
            std::shared_ptr<NativeTextureView> v;
            if (desc.depth.view) {
                auto found = textureView(desc.depth.view); if (!found) return std::unexpected(found.error());
                if ((*found)->desc.usage != TextureViewUsage::DepthStencil)
                    return fail(ErrorCode::InvalidArgument, "Metal depth attachment view requires DepthStencil usage");
                v = *found; a = v->allocation;
            } else {
                auto found = allocation(desc.depth.texture); if (!found) return std::unexpected(found.error());
                a = *found;
            }
            if (shadow(a) != (desc.depth.readOnly ? ResourceState::DepthRead : ResourceState::DepthWrite) ||
                (desc.depth.load == LoadOp::Load && !initialized(a)))
                return fail(ErrorCode::InvalidState, "Metal depth pass requires state and initialized Load contents");
            op.attachments.push_back(Attachment{a,desc.depth.load,{},desc.depth.clear,desc.depth.readOnly,v});
        }
        uses.reserve(uses.size()+op.attachments.size()); operations.reserve(operations.size()+1);
        for (const auto& a : op.attachments) retain(a.allocation, !a.readOnly).initialized = true;
        operations.push_back(std::move(op)); inPass = true; passOperation = operations.size()-1;
        selected.reset();bindings.reset();
        const auto& firstAtt = operations.back().attachments.front();
        const auto& extent = firstAtt.allocation->textureDesc;
        const std::uint32_t firstMip = firstAtt.textureView ? firstAtt.textureView->desc.baseMipLevel : 0u;
        const std::uint32_t passW = std::max(1u, extent.width >> firstMip);
        const std::uint32_t passH = std::max(1u, extent.height >> firstMip);
        viewport={0,0,double(passW),double(passH),0.0,1.0};
        scissor={0,0,passW,passH};
        return {};
    }
    Result<void> endRenderPass() override {
        if (auto ok = ready(); !ok) return ok;
        if (!inPass) return fail(ErrorCode::InvalidState, "Metal render pass is not open");
        inPass = false; return {};
    }
    Result<void> setPipeline(const PipelinePtr& value) override {
        if(auto ok=ready();!ok)return ok;
        if(qType!=QueueType::Graphics)return fail(ErrorCode::InvalidArgument,"Metal setPipeline requires a Graphics command list");
        if(!inPass)return fail(ErrorCode::InvalidState,"Metal pipeline requires active pass");
        auto p=std::dynamic_pointer_cast<MetalPipeline>(value);if(!p)return fail(value?ErrorCode::WrongDevice:ErrorCode::InvalidArgument,"Expected Metal pipeline");
        if(p->owner!=owner)return fail(ErrorCode::WrongDevice,"Metal pipeline belongs to another generation");
        if(p->native->isCompute)return fail(ErrorCode::InvalidArgument,"Metal graphics setPipeline rejects a compute pipeline");
        std::vector<Format> colors;std::optional<Format> depth;
        for(const auto& a:operations[passOperation].attachments){if(a.allocation->textureDesc.depthStencil)depth=a.allocation->textureDesc.format;else colors.push_back(a.allocation->textureDesc.format);}
        if(p->native->targets.colors!=colors||p->native->targets.depth!=depth)return fail(ErrorCode::InvalidArgument,"Metal pipeline/pass attachment formats differ");
        if(p->native->targets.sampleCount!=operations[passOperation].attachments.front().allocation->textureDesc.sampleCount)return fail(ErrorCode::InvalidArgument,"Metal pipeline/pass sample counts differ");
        selected=p->native;bindings.reset();return {};
    }
    Result<void> bindTexture(const TexturePtr& value) override {
        if(auto ok=ready();!ok)return ok;
        if(!inPass||!selected||!selected->sampled)return fail(ErrorCode::InvalidState,"Metal bindTexture requires sampledTexture pipeline");
        ResourceBindings data;data.textures={{0,0,value}};return bindResources(data);
    }
    Result<void> bindTextureView(const TextureViewPtr& value) override {
        if(auto ok=ready();!ok)return ok;
        if(!inPass||!selected||!selected->sampled)return fail(ErrorCode::InvalidState,"Metal bindTextureView requires sampledTexture pipeline");
        ResourceBindings data;data.textures={{0,0,nullptr,value}};return bindResources(data);
    }
    Result<void> bindResources(const ResourceBindings& data) override {
        if(auto ok=ready();!ok)return ok;
        if(!inPass||!selected)return fail(ErrorCode::InvalidState,"Metal bindings require pass/pipeline");
        if(!data.storageBuffers.empty()||!data.storageTextures.empty())return fail(ErrorCode::InvalidArgument,"Metal storage bindings require bindComputeResources");
        std::size_t count=0;for(const auto& e:selected->bindings.entries)count+=e.count;
        auto next=std::make_shared<std::vector<ResourceView>>(count);
        auto slot=[&](unsigned index,unsigned element,BindingType type)->Result<std::size_t>{
            if(index>=selected->bindings.entries.size())return fail(ErrorCode::InvalidArgument,"Unknown Metal binding entry");
            const auto& e=selected->bindings.entries[index];if(e.type!=type||element>=e.count)return fail(ErrorCode::InvalidArgument,"Metal binding type/array mismatch");
            std::size_t at=element;for(unsigned i=0;i<index;++i)at+=selected->bindings.entries[i].count;
            if((*next)[at].allocation)return fail(ErrorCode::InvalidArgument,"Duplicate Metal binding element");return at;};
        for(const auto& view:data.textures){
            if(view.texture&&view.view)return fail(ErrorCode::InvalidArgument,"Specify either texture or view in TextureBinding, not both");
            std::shared_ptr<Allocation> a;
            std::shared_ptr<NativeTextureView> v;
            TextureDimension resolvedDim{TextureDimension::Texture2D};
            if(view.view){
                auto found=textureView(view.view);if(!found)return std::unexpected(found.error());
                if((*found)->desc.usage!=TextureViewUsage::ShaderRead)return fail(ErrorCode::InvalidArgument,"Metal texture binding view requires ShaderRead usage");
                v=*found;a=v->allocation;resolvedDim=v->desc.dimension.value_or(a->textureDesc.dimension);
            }else{
                auto found=allocation(view.texture);if(!found)return std::unexpected(found.error());
                a=*found;resolvedDim=a->textureDesc.dimension;
            }
            auto at=slot(view.binding,view.element,bindingTypeForDimension(resolvedDim));if(!at)return std::unexpected(at.error());
            if(a->backbuffer)return fail(ErrorCode::InvalidArgument,"Metal backbuffer sampling unsupported");
            if(a->textureDesc.depthStencil)return fail(ErrorCode::Unsupported,"Metal depth sampling unsupported");
            if(a->textureDesc.sampleCount>1u)return fail(ErrorCode::Unsupported,"Metal multisampled texture sampling unsupported");
            if(shadow(a)!=ResourceState::ShaderRead||!initialized(a))return fail(ErrorCode::InvalidState,"Metal sampling requires initialized ShaderRead texture");
            (*next)[*at].allocation=a;(*next)[*at].textureView=v;
        }
        for(const auto& view:data.constants){auto at=slot(view.binding,view.element,BindingType::ConstantBuffer);if(!at)return std::unexpected(at.error());auto a=allocation(view.buffer);if(!a)return std::unexpected(a.error());
            if((*a)->desc.memory==Memory::Readback||view.offset%256||!view.size||view.size%256||view.size>65536||view.offset>(*a)->desc.size||view.size>(*a)->desc.size-view.offset||view.size<selected->minimumBytes[view.binding])return fail(ErrorCode::InvalidArgument,"Metal constant view range/alignment/size invalid");
            if(shadow(*a)!=ResourceState::ConstantBuffer)return fail(ErrorCode::InvalidState,"Metal constants require ConstantBuffer state");(*next)[*at]={*a,view.offset,view.size};}
        for(const auto& view:*next)if(!view.allocation)return fail(ErrorCode::InvalidArgument,"Incomplete Metal resource snapshot");
        uses.reserve(uses.size()+next->size());for(const auto& view:*next)retain(view.allocation);bindings=std::move(next);return {};
    }
    Result<void> bindVertexBuffer(std::uint32_t slot,const BufferPtr& value,std::uint32_t stride,std::uint64_t offset,std::uint64_t size) override {
        if(auto ok=ready();!ok)return ok;
        if(qType!=QueueType::Graphics)return fail(ErrorCode::InvalidArgument,"Metal bindVertexBuffer requires a Graphics command list");
        auto a=allocation(value);if(!a)return std::unexpected(a.error());
        if(slot>=15)return fail(ErrorCode::Unsupported,"Metal vertex stream slots are 0..14");
        if(!stride||stride>2048||(*a)->desc.memory==Memory::Readback||offset>(*a)->desc.size||size>(*a)->desc.size-offset)return fail(ErrorCode::InvalidArgument,"Invalid Metal vertex view");
        const auto bytes=size?size:(*a)->desc.size-offset;if(!bytes||bytes>UINT32_MAX)return fail(ErrorCode::InvalidArgument,"Invalid Metal vertex view size");
        if(!vertexState(shadow(*a)))return fail(ErrorCode::InvalidState,"Metal vertex buffer state required");
        uses.reserve(uses.size()+1);retain(*a);vertices[slot]={*a,offset,bytes,stride};return {};
    }
    Result<void> bindIndexBuffer(const BufferPtr& value,IndexFormat format,std::uint64_t offset,std::uint64_t size) override {
        if(auto ok=ready();!ok)return ok;
        if(qType!=QueueType::Graphics)return fail(ErrorCode::InvalidArgument,"Metal bindIndexBuffer requires a Graphics command list");
        auto a=allocation(value);if(!a)return std::unexpected(a.error());
        if(format!=IndexFormat::UInt16&&format!=IndexFormat::UInt32)return fail(ErrorCode::InvalidArgument,"Invalid Metal index format");
        const auto element=format==IndexFormat::UInt16?2u:4u;
        if((*a)->desc.memory==Memory::Readback||offset%element||offset>(*a)->desc.size||size>(*a)->desc.size-offset)return fail(ErrorCode::InvalidArgument,"Invalid Metal index view");
        const auto bytes=size?size:(*a)->desc.size-offset;if(!bytes||bytes>UINT32_MAX||bytes%element)return fail(ErrorCode::InvalidArgument,"Invalid Metal index view size");
        if(!indexState(shadow(*a)))return fail(ErrorCode::InvalidState,"Metal index buffer state required");
        uses.reserve(uses.size()+1);retain(*a);indices={*a,offset,bytes,element};return {};
    }
    Result<void> setViewport(const Viewport& value) override {
        if (auto ok = ready(); !ok) return ok;
        if (!inPass) return fail(ErrorCode::InvalidState, "Metal viewport requires pass");
        const auto& firstAtt = operations[passOperation].attachments.front();
        const auto d = firstAtt.allocation->textureDesc;
        const std::uint32_t mip = firstAtt.textureView ? firstAtt.textureView->desc.baseMipLevel : 0u;
        if (auto ok = validateViewport(value, std::max(1u, d.width >> mip), std::max(1u, d.height >> mip)); !ok) return ok;
        viewport = MTLViewport{value.x, value.y, value.width, value.height, value.minDepth, value.maxDepth};
        return {};
    }
    Result<void> setScissor(std::uint32_t x,std::uint32_t y,std::uint32_t width,std::uint32_t height) override {
        if(auto ok=ready();!ok)return ok;if(!inPass)return fail(ErrorCode::InvalidState,"Metal scissor requires pass");
        const auto& firstAtt = operations[passOperation].attachments.front();
        const auto d = firstAtt.allocation->textureDesc;
        const std::uint32_t mip = firstAtt.textureView ? firstAtt.textureView->desc.baseMipLevel : 0u;
        const std::uint32_t passW = std::max(1u, d.width >> mip);
        const std::uint32_t passH = std::max(1u, d.height >> mip);
        if(!width||!height||x>passW||width>passW-x||y>passH||height>passH-y)return fail(ErrorCode::InvalidArgument,"Metal scissor outside attachment");
        scissor={x,y,width,height};return {};
    }
    Result<void> recordDraw(std::uint32_t count,std::uint32_t instances,std::uint32_t first,std::int32_t base,std::uint32_t firstInstance,bool indexed) {
        if(auto ok=ready();!ok)return ok;
        if(qType!=QueueType::Graphics)return fail(ErrorCode::InvalidArgument,"Metal draw requires a Graphics command list");
        if(!inPass||!selected)return fail(ErrorCode::InvalidState,"Metal draw requires pass and pipeline");
        if(!count||!instances||instances>UINT32_MAX-firstInstance||(!indexed&&count>UINT32_MAX-first))return fail(ErrorCode::InvalidArgument,"Invalid Metal draw range");
        if(indexed&&(!indices.allocation||!indexState(shadow(indices.allocation))))return fail(ErrorCode::InvalidState,"Metal index binding missing/stale");
        if(indexed&&std::uint64_t(first)+count>indices.bytes/indices.stride)return fail(ErrorCode::InvalidArgument,"Metal index range exceeds view");
        for(const auto& input:selected->vertices.buffers){const auto& view=vertices[input.slot];unsigned extent=0,alignment=1;
            for(const auto& a:selected->vertices.attributes)if(a.slot==input.slot){extent=std::max(extent,a.offset+vertexBytes(a.format));alignment=std::max(alignment,a.format==VertexFormat::UNorm8x4?1u:4u);}
            if(!view.allocation||view.stride!=input.stride||!vertexState(shadow(view.allocation)))return fail(ErrorCode::InvalidState,"Metal vertex binding missing/mismatched");
            if(view.offset%alignment||view.bytes<extent)return fail(ErrorCode::InvalidArgument,"Metal vertex alignment/extent mismatch");
            auto last=(view.bytes-extent)/view.stride;
            if((input.rate==VertexInputRate::Instance&&std::uint64_t(firstInstance)+instances-1>last)||
                (input.rate==VertexInputRate::Vertex&&((!indexed&&std::uint64_t(first)+count-1>last)||(indexed&&base>0&&std::uint32_t(base)>last))))return fail(ErrorCode::InvalidArgument,"Metal input exceeds vertex/instance view");}
        if(!selected->bindings.entries.empty()&&!bindings)return fail(ErrorCode::InvalidState,"Metal draw requires complete bindings");
        if(bindings)for(const auto& view:*bindings)if(shadow(view.allocation)!=(view.allocation->texture?ResourceState::ShaderRead:ResourceState::ConstantBuffer))return fail(ErrorCode::InvalidState,"Metal binding read state changed");
        for(const auto& a:operations[passOperation].attachments)if(a.readOnly&&selected->depthState.writeEnabled)return fail(ErrorCode::InvalidState,"Read-only Metal depth rejects writing pipeline");
        auto& op=operations[passOperation];op.draws.reserve(op.draws.size()+1);op.order.reserve(op.order.size()+1);
        op.draws.push_back(Draw{selected,bindings,vertices,indices,count,instances,first,firstInstance,base,indexed,viewport,scissor});op.order.emplace_back(true,op.draws.size()-1);return {};
    }
    Result<void> draw(std::uint32_t n,std::uint32_t instances,std::uint32_t first,std::uint32_t firstInstance) override { return recordDraw(n,instances,first,0,firstInstance,false); }
    Result<void> drawIndexed(std::uint32_t n,std::uint32_t instances,std::uint32_t first,std::int32_t base,std::uint32_t firstInstance) override { return recordDraw(n,instances,first,base,firstInstance,true); }
    Result<void> recordIndirectDraw(const BufferPtr& argsBuffer, std::uint64_t argsOffset, std::uint32_t drawCount, std::uint32_t stride, bool indexed) {
        if (auto ok = ready(); !ok) return ok;
        if (qType != QueueType::Graphics) return fail(ErrorCode::InvalidArgument, "Metal drawIndirect requires a Graphics command list");
        if (!inPass || !selected) return fail(ErrorCode::InvalidState, "Metal drawIndirect requires an active render pass and pipeline");
        const std::uint32_t minStride = indexed ? static_cast<std::uint32_t>(sizeof(DrawIndexedIndirectCommand)) : static_cast<std::uint32_t>(sizeof(DrawIndirectCommand));
        if (!drawCount || drawCount > 65536u) return fail(ErrorCode::InvalidArgument, "Metal drawIndirect drawCount out of range");
        if (stride < minStride || (stride % 4u) != 0u || (argsOffset % 4u) != 0u)
            return fail(ErrorCode::InvalidArgument, "Metal drawIndirect offset/stride must be 4-byte aligned");
        if (indexed && (!indices.allocation || !indexState(shadow(indices.allocation))))
            return fail(ErrorCode::InvalidState, "Metal drawIndexedIndirect requires a valid index buffer");
        auto argBuf = allocation(argsBuffer); if (!argBuf) return std::unexpected(argBuf.error());
        if ((*argBuf)->desc.memory == Memory::Readback)
            return fail(ErrorCode::InvalidArgument, "Readback buffer cannot be used as an indirect argument buffer");
        if (shadow(*argBuf) != ResourceState::IndirectArgument)
            return fail(ErrorCode::InvalidState, "Metal drawIndirect requires ResourceState::IndirectArgument");
        const std::uint64_t spanBytes = static_cast<std::uint64_t>(drawCount - 1u) * stride + minStride;
        if (argsOffset > (*argBuf)->desc.size || spanBytes > (*argBuf)->desc.size - argsOffset)
            return fail(ErrorCode::InvalidArgument, "Metal drawIndirect argument range exceeds buffer size");
        for (const auto& input : selected->vertices.buffers) {
            const auto& view = vertices[input.slot];
            if (!view.allocation || view.stride != input.stride || !vertexState(shadow(view.allocation)))
                return fail(ErrorCode::InvalidState, "Missing/mismatched/stale Metal vertex binding");
        }
        if (!selected->bindings.entries.empty() && !bindings) return fail(ErrorCode::InvalidState, "Metal drawIndirect requires complete bindings");
        if (bindings) for (const auto& view : *bindings) if (shadow(view.allocation) != (view.allocation->texture ? ResourceState::ShaderRead : ResourceState::ConstantBuffer))
            return fail(ErrorCode::InvalidState, "Metal binding read state changed");
        for (const auto& a : operations[passOperation].attachments) if (a.readOnly && selected->depthState.writeEnabled)
            return fail(ErrorCode::InvalidState, "Read-only Metal depth rejects writing pipeline");
        uses.reserve(uses.size() + 1);
        retain(*argBuf);
        auto& op = operations[passOperation];
        op.draws.reserve(op.draws.size() + 1);
        op.order.reserve(op.order.size() + 1);
        Draw draw{};
        draw.pipeline = selected;
        draw.bindings = bindings;
        draw.vertices = vertices;
        draw.indices = indices;
        draw.indexed = indexed;
        draw.viewport = viewport;
        draw.scissor = scissor;
        draw.indirect = true;
        draw.indirectArgs = BufferView{*argBuf, argsOffset, spanBytes, stride};
        draw.indirectDrawCount = drawCount;
        draw.indirectStride = stride;
        op.draws.push_back(std::move(draw));
        op.order.emplace_back(true, op.draws.size() - 1);
        if (owner->profiler) owner->profiler->recordDraw(indexed, drawCount);
        return {};
    }
    Result<void> drawIndirect(const BufferPtr& argsBuffer, std::uint64_t argsOffset, std::uint32_t drawCount, std::uint32_t stride) override {
        return recordIndirectDraw(argsBuffer, argsOffset, drawCount, stride, false);
    }
    Result<void> drawIndexedIndirect(const BufferPtr& argsBuffer, std::uint64_t argsOffset, std::uint32_t drawCount, std::uint32_t stride) override {
        return recordIndirectDraw(argsBuffer, argsOffset, drawCount, stride, true);
    }
    Result<void> setComputePipeline(const PipelinePtr& value) override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (qType == QueueType::Copy) return fail(ErrorCode::InvalidArgument, "Metal compute operations are not supported on a Copy command list");
        auto p = std::dynamic_pointer_cast<MetalPipeline>(value);
        if (!p) return fail(value ? ErrorCode::WrongDevice : ErrorCode::InvalidArgument, "Expected Metal compute pipeline");
        if (p->owner != owner) return fail(ErrorCode::WrongDevice, "Metal compute pipeline belongs to another generation");
        if (!p->native->isCompute) return fail(ErrorCode::InvalidArgument, "Metal setComputePipeline requires a compute pipeline");
        selectedCompute = p->native;
        computeBindings.reset();
        return {};
    }
    Result<void> bindComputeResources(const ResourceBindings& data) override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (qType == QueueType::Copy) return fail(ErrorCode::InvalidArgument, "Metal compute operations are not supported on a Copy command list");
        if (!selectedCompute) return fail(ErrorCode::InvalidState, "Metal compute bindings require a compute pipeline");
        std::size_t count = 0;
        for (const auto& e : selectedCompute->bindings.entries) count += e.count;
        const std::size_t provided = data.textures.size() + data.constants.size() + data.storageBuffers.size() + data.storageTextures.size();
        if (provided != count) return fail(ErrorCode::InvalidArgument, "Every Metal compute resource element must be bound exactly once");
        auto next = std::make_shared<std::vector<ResourceView>>(count);
        std::vector<std::shared_ptr<Allocation>> storageImagesToInit;
        auto slot = [&](unsigned index, unsigned element, BindingType type) -> Result<std::size_t> {
            if (index >= selectedCompute->bindings.entries.size()) return fail(ErrorCode::InvalidArgument, "Unknown Metal compute binding entry");
            const auto& e = selectedCompute->bindings.entries[index];
            if (e.type != type || element >= e.count) return fail(ErrorCode::InvalidArgument, "Metal compute binding type/array mismatch");
            std::size_t at = element;
            for (unsigned i = 0; i < index; ++i) at += selectedCompute->bindings.entries[i].count;
            if ((*next)[at].allocation) return fail(ErrorCode::InvalidArgument, "Duplicate Metal compute binding element");
            return at;
        };
        for (const auto& view : data.textures) {
            if (view.texture && view.view) return fail(ErrorCode::InvalidArgument, "Specify either texture or view in TextureBinding, not both");
            std::shared_ptr<Allocation> a;
            std::shared_ptr<NativeTextureView> v;
            TextureDimension resolvedDim{TextureDimension::Texture2D};
            if (view.view) {
                auto found = textureView(view.view); if (!found) return std::unexpected(found.error());
                if ((*found)->desc.usage != TextureViewUsage::ShaderRead) return fail(ErrorCode::InvalidArgument, "Metal texture view requires ShaderRead usage");
                v = *found; a = v->allocation;
                resolvedDim = v->desc.dimension.value_or(a->textureDesc.dimension);
            } else {
                auto found = allocation(view.texture); if (!found) return std::unexpected(found.error());
                a = *found;
                resolvedDim = a->textureDesc.dimension;
            }
            auto at = slot(view.binding, view.element, bindingTypeForDimension(resolvedDim)); if (!at) return std::unexpected(at.error());
            if (a->backbuffer || a->textureDesc.depthStencil) return fail(ErrorCode::InvalidArgument, "Metal compute sampled texture invalid");
            if (shadow(a) != ResourceState::ShaderRead || !initialized(a)) return fail(ErrorCode::InvalidState, "Metal compute sampling requires initialized ShaderRead texture");
            (*next)[*at] = {a, 0, 0, v, ResourceState::ShaderRead};
        }
        for (const auto& view : data.constants) {
            auto at = slot(view.binding, view.element, BindingType::ConstantBuffer); if (!at) return std::unexpected(at.error());
            auto a = allocation(view.buffer); if (!a) return std::unexpected(a.error());
            if ((*a)->desc.memory == Memory::Readback || view.offset % 256 || !view.size || view.size % 256 || view.size > 65536 ||
                view.offset > (*a)->desc.size || view.size > (*a)->desc.size - view.offset || view.size < selectedCompute->minimumBytes[view.binding])
                return fail(ErrorCode::InvalidArgument, "Metal compute constant buffer view invalid");
            if (shadow(*a) != ResourceState::ConstantBuffer) return fail(ErrorCode::InvalidState, "Metal compute constants require ConstantBuffer state");
            (*next)[*at] = {*a, view.offset, view.size, {}, ResourceState::ConstantBuffer};
        }
        for (const auto& view : data.storageBuffers) {
            auto at = slot(view.binding, view.element, BindingType::StorageBuffer); if (!at) return std::unexpected(at.error());
            auto a = allocation(view.buffer); if (!a) return std::unexpected(a.error());
            if (!(*a)->desc.storage || (*a)->desc.memory != Memory::Device) return fail(ErrorCode::InvalidArgument, "Metal StorageBuffer requires device storage buffer");
            if (view.offset % 256 || view.offset > (*a)->desc.size) return fail(ErrorCode::InvalidArgument, "Metal StorageBuffer offset invalid");
            const std::uint64_t size = view.size ? view.size : (*a)->desc.size - view.offset;
            if (!size || size % 4 || size > (*a)->desc.size - view.offset) return fail(ErrorCode::InvalidArgument, "Metal StorageBuffer size invalid");
            if (shadow(*a) != ResourceState::UnorderedAccess) return fail(ErrorCode::InvalidState, "Metal StorageBuffer requires UnorderedAccess state");
            (*next)[*at] = {*a, view.offset, size, {}, ResourceState::UnorderedAccess};
        }
        for (const auto& view : data.storageTextures) {
            auto at = slot(view.binding, view.element, BindingType::StorageTexture2D); if (!at) return std::unexpected(at.error());
            if (view.texture && view.view) return fail(ErrorCode::InvalidArgument, "Specify either texture or view in StorageTextureBinding, not both");
            std::shared_ptr<Allocation> a;
            std::shared_ptr<NativeTextureView> v;
            if (view.view) {
                auto found = textureView(view.view); if (!found) return std::unexpected(found.error());
                if ((*found)->desc.usage != TextureViewUsage::Storage) return fail(ErrorCode::InvalidArgument, "Metal storage texture view requires Storage usage");
                v = *found; a = v->allocation;
            } else {
                auto found = allocation(view.texture); if (!found) return std::unexpected(found.error());
                a = *found;
            }
            if (!a->textureDesc.storage || a->textureDesc.depthStencil) return fail(ErrorCode::InvalidArgument, "Metal StorageTextureBinding requires storage color texture");
            if (shadow(a) != ResourceState::UnorderedAccess) return fail(ErrorCode::InvalidState, "Metal StorageTextureBinding requires UnorderedAccess state");
            storageImagesToInit.push_back(a);
            (*next)[*at] = {a, 0, 0, v, ResourceState::UnorderedAccess};
        }
        for (const auto& view : *next) if (!view.allocation) return fail(ErrorCode::InvalidArgument, "Incomplete Metal compute resource snapshot");
        uses.reserve(uses.size() + next->size());
        for (const auto& view : *next) retain(view.allocation, view.expectedState == ResourceState::UnorderedAccess);
        for (const auto& a : storageImagesToInit) retain(a, true).initialized = true;
        computeBindings = std::move(next);
        return {};
    }
    Result<void> dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ) override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (qType == QueueType::Copy) return fail(ErrorCode::InvalidArgument, "Metal compute operations are not supported on a Copy command list");
        if (!selectedCompute) return fail(ErrorCode::InvalidState, "Metal dispatch requires an active compute pipeline");
        if (!groupCountX || !groupCountY || !groupCountZ) return fail(ErrorCode::InvalidArgument, "Metal dispatch group counts must be positive");
        if (!selectedCompute->bindings.entries.empty() && !computeBindings) return fail(ErrorCode::InvalidState, "Metal dispatch requires complete compute bindings");
        if (computeBindings) {
            for (const auto& view : *computeBindings) {
                if (shadow(view.allocation) != view.expectedState) return fail(ErrorCode::InvalidState, "Metal bound compute resource left required state");
            }
        }
        operations.reserve(operations.size() + 1);
        Operation op;
        op.kind = OperationKind::Compute;
        op.compute = ComputeDispatchOp{selectedCompute, computeBindings, groupCountX, groupCountY, groupCountZ};
        operations.push_back(std::move(op));
        if (owner->profiler) owner->profiler->recordDispatch();
        return {};
    }
    Result<void> dispatchIndirect(const BufferPtr& argsBuffer, std::uint64_t argsOffset) override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (qType == QueueType::Copy) return fail(ErrorCode::InvalidArgument, "Metal compute operations are not supported on a Copy command list");
        if (!selectedCompute) return fail(ErrorCode::InvalidState, "Metal dispatchIndirect requires an active compute pipeline");
        if ((argsOffset % 4u) != 0u) return fail(ErrorCode::InvalidArgument, "Metal dispatchIndirect offset must be 4-byte aligned");
        if (!selectedCompute->bindings.entries.empty() && !computeBindings) return fail(ErrorCode::InvalidState, "Metal dispatchIndirect requires complete compute bindings");
        if (computeBindings) {
            for (const auto& view : *computeBindings) {
                if (shadow(view.allocation) != view.expectedState) return fail(ErrorCode::InvalidState, "Metal bound compute resource left required state");
            }
        }
        auto argBuf = allocation(argsBuffer); if (!argBuf) return std::unexpected(argBuf.error());
        if ((*argBuf)->desc.memory == Memory::Readback)
            return fail(ErrorCode::InvalidArgument, "Readback buffer cannot be used as an indirect argument buffer");
        if (shadow(*argBuf) != ResourceState::IndirectArgument)
            return fail(ErrorCode::InvalidState, "Metal dispatchIndirect requires ResourceState::IndirectArgument");
        if (argsOffset > (*argBuf)->desc.size || sizeof(DispatchIndirectCommand) > (*argBuf)->desc.size - argsOffset)
            return fail(ErrorCode::InvalidArgument, "Metal dispatchIndirect argument range exceeds buffer size");
        uses.reserve(uses.size() + 1);
        retain(*argBuf);
        operations.reserve(operations.size() + 1);
        Operation op;
        op.kind = OperationKind::Compute;
        op.compute = ComputeDispatchOp{selectedCompute, computeBindings, 0, 0, 0, true, *argBuf, argsOffset};
        operations.push_back(std::move(op));
        if (owner->profiler) owner->profiler->recordDispatch();
        return {};
    }
    Result<void> uavBarrier(const BufferPtr& value) override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (qType == QueueType::Copy) return fail(ErrorCode::InvalidArgument, "Metal uavBarrier is not supported on a Copy command list");
        auto a = allocation(value); if (!a) return std::unexpected(a.error());
        if (!(*a)->desc.storage) return fail(ErrorCode::InvalidArgument, "Metal uavBarrier requires a storage buffer");
        if (shadow(*a) != ResourceState::UnorderedAccess) return fail(ErrorCode::InvalidState, "Metal uavBarrier requires UnorderedAccess state");
        uses.reserve(uses.size() + 1);
        retain(*a, true);
        if (owner->profiler) owner->profiler->recordUavBarrier();
        return {};
    }
    Result<void> uavBarrier(const TexturePtr& value) override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (qType == QueueType::Copy) return fail(ErrorCode::InvalidArgument, "Metal uavBarrier is not supported on a Copy command list");
        auto a = allocation(value); if (!a) return std::unexpected(a.error());
        if (!(*a)->textureDesc.storage) return fail(ErrorCode::InvalidArgument, "Metal uavBarrier requires a storage texture");
        if (shadow(*a) != ResourceState::UnorderedAccess) return fail(ErrorCode::InvalidState, "Metal uavBarrier requires UnorderedAccess state");
        uses.reserve(uses.size() + 1);
        retain(*a, true);
        if (owner->profiler) owner->profiler->recordUavBarrier();
        return {};
    }
    Result<void> writeTimestamp(std::uint32_t) override { return owner->unsupported(); }
    Result<void> resolveTimestamps() override { return owner->unsupported(); }
    Result<double> elapsedGpuMilliseconds() override { return owner->unsupported(); }
    Result<void> beginTimestampScope(std::string_view) override { return owner->unsupported(); }
    Result<void> endTimestampScope() override { return owner->unsupported(); }
    Result<void> resolveTimestampScopes() override { return owner->unsupported(); }
    Result<std::vector<TimestampScopeResult>> timestampScopes() override { return owner->unsupported(); }
};
Result<std::shared_ptr<Allocation>> MetalCommands::allocation(const TexturePtr& value) const {
    auto texture = std::dynamic_pointer_cast<MetalTexture>(value);
    if (!texture) return fail(value ? ErrorCode::WrongDevice : ErrorCode::InvalidArgument, "Expected a Metal texture");
    if (texture->owner != owner) return fail(ErrorCode::WrongDevice, "Metal texture belongs to another generation");
    if (texture->allocation->backbuffer && !texture->allocation->texture)
        return fail(ErrorCode::InvalidState, "Metal drawable is no longer current");
    return texture->allocation;
}
struct MetalCommandPool final : CommandPool {
    std::shared_ptr<State> owner;
    std::shared_ptr<NativeCommandPool> pool;
    std::shared_ptr<int> token;
    ~MetalCommandPool() override {
        if (pool) {
            std::scoped_lock lock(pool->mutex);
            pool->ownerAlive = false;
        }
    }
    QueueType queueType() const override { return pool->qType; }
    Result<std::unique_ptr<CommandList>> createCommandList() override {
        if (auto ok = owner->pollQueue(pool->qType); !ok) return std::unexpected(ok.error());
        std::scoped_lock lock(pool->mutex);
        if (pool->openCount > 0) {
            return fail(ErrorCode::InvalidState, "Cannot allocate from a CommandPool while a command list from it is recording");
        }
        auto value = std::make_unique<MetalCommands>(owner, pool, pool->qType);
        value->token = std::make_shared<int>();
        std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
        owner->objects.push_back(value->token);
        pool->liveLists.push_back(value.get());
        return std::unique_ptr<CommandList>(std::move(value));
    }
    Result<void> reset() override {
        if (auto ok = owner->pollQueue(pool->qType); !ok) return ok;
        std::scoped_lock lock(pool->mutex);
        if (pool->openCount > 0) {
            return fail(ErrorCode::InvalidState, "Command pool cannot reset while a command list is recording");
        }
        if (pool->lastSubmitted > owner->queues[queueIndex(pool->qType)].completed.load(std::memory_order_acquire)) {
            return fail(ErrorCode::InvalidState, "Command pool has in-flight submissions");
        }
        for (MetalCommands* cmd : pool->liveLists) {
            cmd->clearRecordingState();
            cmd->recordedEpoch = 0;
        }
        pool->lastSubmitted = 0;
        ++pool->epoch;
        return {};
    }
    Result<void> bindRecordingThread() override {
        std::scoped_lock lock(pool->mutex);
        if (auto ok = owner->checkAliveForWorker(); !ok) return ok;
        if (!pool->ownerAlive) return fail(ErrorCode::InvalidState, "Owning CommandPool has been destroyed");
        if (pool->openCount > 0) {
            return fail(ErrorCode::InvalidState, "Command pool cannot rebind recording thread while a command list is recording");
        }
        if (pool->lastSubmitted > owner->queues[queueIndex(pool->qType)].completed.load(std::memory_order_acquire)) {
            return fail(ErrorCode::InvalidState, "Command pool cannot rebind recording thread while submissions are in flight");
        }
        pool->recordingThread.store(std::this_thread::get_id(), std::memory_order_release);
        return {};
    }
};
struct MetalQueue final : CommandQueue {
    std::shared_ptr<State> owner;
    QueueType qType{QueueType::Graphics};
    explicit MetalQueue(std::shared_ptr<State> state, QueueType type = QueueType::Graphics)
        : owner(std::move(state)), qType(type) {}
    QueueType type() const override { return qType; }
    Result<Fence> submit(CommandList& value) override {
        CommandList* single = &value;
        return submit(std::span<CommandList* const>{&single, 1});
    }
    Result<Fence> submit(std::span<CommandList* const> lists) override {
        SubmitDesc desc{};
        desc.commandLists = lists;
        return submit(desc);
    }
    Result<Fence> submit(const SubmitDesc& desc) override {
        if (auto ok = owner->pollQueue(qType); !ok) return std::unexpected(ok.error());
        if (desc.commandLists.empty()) return fail(ErrorCode::InvalidArgument, "Metal batch submit requires at least one command list");
        const std::size_t qi = queueIndex(qType);
        auto& qSlot = owner->queues[qi];
        if (qSlot.last == UINT64_MAX) return fail(ErrorCode::InvalidState, "Metal fence sequence exhausted");
        std::vector<MetalCommands*> validated;
        validated.reserve(desc.commandLists.size());
        std::unordered_set<MetalCommands*> seenLists;
        std::unordered_set<Allocation*> seenRead;
        std::unordered_set<Allocation*> seenMutated;
        for (CommandList* item : desc.commandLists) {
            if (!item) return fail(ErrorCode::InvalidArgument, "Null command list in Metal batch submit");
            auto* commands = dynamic_cast<MetalCommands*>(item);
            if (!commands || commands->owner != owner) return fail(ErrorCode::WrongDevice, "Metal command list belongs to another generation");
            if (commands->qType != qType) return fail(ErrorCode::InvalidArgument, "Metal command list queue type does not match target queue");
            if (!seenLists.insert(commands).second) return fail(ErrorCode::InvalidArgument, "Duplicate command list in Metal batch submit");
            if (commands->pool) {
                std::scoped_lock lock(commands->pool->mutex);
                if (!commands->pool->ownerAlive) return fail(ErrorCode::InvalidState, "Owning CommandPool has been destroyed");
                if (commands->pool->openCount > 0) return fail(ErrorCode::InvalidState, "Command pool has a command list currently recording");
                if (commands->recordedEpoch != commands->pool->epoch) return fail(ErrorCode::InvalidState, "Pooled command list was reset and must be re-recorded");
            }
            if (!commands->ended || commands->open || commands->submitted) return fail(ErrorCode::InvalidState, "Metal submission requires an ended, unsubmitted command list");
            for (const auto& use : commands->uses) {
                if (use.version != use.allocation->version || use.allocation->version == UINT64_MAX ||
                    (use.allocation->backbuffer && !use.allocation->texture))
                    return fail(ErrorCode::InvalidState, "Stale Metal resource observation");
                Allocation* key = use.allocation.get();
                if (use.mutated) {
                    if (seenMutated.contains(key) || seenRead.contains(key))
                        return fail(ErrorCode::InvalidState, "Metal batch submit has conflicting resource mutations across command lists");
                    seenMutated.insert(key);
                } else {
                    if (seenMutated.contains(key))
                        return fail(ErrorCode::InvalidState, "Metal batch submit has conflicting resource mutation and read across command lists");
                    seenRead.insert(key);
                }
            }
            validated.push_back(commands);
        }
        std::vector<std::pair<std::shared_ptr<NativeTimelineEvent>, std::uint64_t>> waitEvents;
        std::vector<std::shared_ptr<NativeTimelineEvent>> retainedEvents;
        for (const auto& w : desc.waits) {
            if (!w.fence) return fail(ErrorCode::InvalidArgument, "Null timeline fence in Metal submit waits");
            auto mtlFence = std::dynamic_pointer_cast<MetalTimelineFence>(w.fence);
            if (!mtlFence || mtlFence->owner != owner) return fail(ErrorCode::WrongDevice, "Metal timeline fence belongs to another generation");
            waitEvents.emplace_back(mtlFence->native, w.value);
            retainedEvents.push_back(mtlFence->native);
        }
        std::vector<std::pair<std::shared_ptr<NativeTimelineEvent>, std::uint64_t>> signalEvents;
        std::unordered_map<NativeTimelineEvent*, std::uint64_t> pendingSignalMax;
        for (const auto& s : desc.signals) {
            if (!s.fence) return fail(ErrorCode::InvalidArgument, "Null timeline fence in Metal submit signals");
            auto mtlFence = std::dynamic_pointer_cast<MetalTimelineFence>(s.fence);
            if (!mtlFence || mtlFence->owner != owner) return fail(ErrorCode::WrongDevice, "Metal timeline fence belongs to another generation");
            const auto it = pendingSignalMax.find(mtlFence->native.get());
            const std::uint64_t prev = it != pendingSignalMax.end()
                ? it->second
                : std::max(mtlFence->native->lastSignaled, mtlFence->native->event.signaledValue);
            if (s.value <= prev || s.value == UINT64_MAX) {
                return fail(ErrorCode::InvalidArgument, "Metal TimelineFence signal value must strictly advance");
            }
            pendingSignalMax[mtlFence->native.get()] = s.value;
            signalEvents.emplace_back(mtlFence->native, s.value);
            retainedEvents.push_back(mtlFence->native);
        }
        // Allocate the retention node before commit; retain even if the caller destroys its list.
        std::vector<std::shared_ptr<Allocation>> allocations;
        std::vector<std::shared_ptr<NativePipeline>> pipelines;
        std::vector<std::shared_ptr<NativeTextureView>> textureViews;
        std::vector<id<MTLCommandBuffer>> commandBuffers;
        commandBuffers.reserve(validated.size() + (waitEvents.empty() ? 0 : 1));
        @autoreleasepool {
            if (!waitEvents.empty()) {
                id<MTLCommandBuffer> waitBuf = [qSlot.queue commandBuffer];
                if (!waitBuf) return fail(ErrorCode::OutOfMemory, "Metal timeline wait command buffer creation failed");
                for (const auto& [ev, val] : waitEvents) {
                    [waitBuf encodeWaitForEvent:ev->event value:val];
                }
                commandBuffers.push_back(waitBuf);
            }
            for (MetalCommands* commands : validated) {
                for (auto& use : commands->uses) allocations.push_back(use.allocation);
                for(const auto& op:commands->operations){
                    if(op.kind==OperationKind::Compute){
                        if(op.compute.pipeline&&std::find(pipelines.begin(),pipelines.end(),op.compute.pipeline)==pipelines.end())pipelines.push_back(op.compute.pipeline);
                        if(op.compute.bindings)for(const auto& rv:*op.compute.bindings)if(rv.textureView&&std::find(textureViews.begin(),textureViews.end(),rv.textureView)==textureViews.end())textureViews.push_back(rv.textureView);
                        continue;
                    }
                    for(const auto& a:op.attachments)if(a.textureView&&std::find(textureViews.begin(),textureViews.end(),a.textureView)==textureViews.end())textureViews.push_back(a.textureView);
                    for(const auto& draw:op.draws){
                        if(std::find(pipelines.begin(),pipelines.end(),draw.pipeline)==pipelines.end())pipelines.push_back(draw.pipeline);
                        if(draw.bindings)for(const auto& rv:*draw.bindings)if(rv.textureView&&std::find(textureViews.begin(),textureViews.end(),rv.textureView)==textureViews.end())textureViews.push_back(rv.textureView);
                    }
                }
                const bool drawable = commands->usesDrawable();
                if (drawable) {
                    if (auto ok = commands->encode(); !ok) return std::unexpected(ok.error());
                }
                commandBuffers.push_back(commands->native->buffer);
                if (drawable) commands->native->buffer = nil;
            }
            id<MTLCommandBuffer> finalBuffer = commandBuffers.back();
            for (const auto& [ev, val] : signalEvents) {
                [finalBuffer encodeSignalEvent:ev->event value:val];
            }
            std::vector<id<MTLCommandBuffer>> extras;
            if (commandBuffers.size() > 1) {
                extras.assign(commandBuffers.begin(), commandBuffers.end() - 1);
            }
            qSlot.pending.push_back({qSlot.last + 1, finalBuffer, std::move(allocations), std::move(pipelines), nil, std::move(textureViews), std::move(extras), std::move(retainedEvents), signalEvents});
            for (id<MTLCommandBuffer> cmdBuf : commandBuffers) {
                [cmdBuf commit];
            }
        }
        ++qSlot.last;
        for (const auto& [ev, val] : signalEvents) ev->lastSignaled = val;
        std::unordered_set<Allocation*> versionBumped;
        for (MetalCommands* commands : validated) {
            commands->submitted = qSlot.last;
            if (commands->pool) {
                std::scoped_lock lock(commands->pool->mutex);
                commands->pool->lastSubmitted = std::max(commands->pool->lastSubmitted, qSlot.last);
            }
            for (auto& use : commands->uses) {
                if (use.mutated) {
                    use.allocation->state = use.shadow;
                    use.allocation->initialized = use.initialized;
                }
                use.allocation->lastUse[qi] = qSlot.last;
                if (versionBumped.insert(use.allocation.get()).second) ++use.allocation->version;
            }
        }
        if (owner->profiler) owner->profiler->recordSubmit();
        return Fence{qSlot.last, qSlot.id};
    }
    Result<void> wait(Fence fence, std::chrono::milliseconds timeout) override {
        if (auto ok = owner->check(); !ok) return ok;
        if (timeout.count() < 0 || timeout.count() > 4294967294ll) return fail(ErrorCode::InvalidArgument, "Metal fence timeout outside finite range");
        auto& qSlot = owner->queues[queueIndex(qType)];
        if (fence.device != qSlot.id) return fail(ErrorCode::WrongDevice, "Metal fence belongs to another generation or queue");
        if (!fence.value || fence.value > qSlot.last) return fail(ErrorCode::InvalidArgument, "Metal fence value was not submitted");
        const bool track = owner->profiler && owner->profiler->isEnabled();
        const auto t0 = track ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        for (;;) {
            if (auto ok = owner->pollQueue(qType); !ok) return ok;
            if (qSlot.completed.load(std::memory_order_acquire) >= fence.value) {
                if (track) {
                    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
                    owner->profiler->recordQueueWait(ns > 0 ? static_cast<std::uint64_t>(ns) : 0u);
                }
                return {};
            }
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) return fail(ErrorCode::Timeout, "Metal fence wait timed out");
            std::this_thread::sleep_for(std::min(deadline - now, std::chrono::steady_clock::duration(std::chrono::milliseconds(1))));
        }
    }
    Result<void> waitIdle() override {
        if (auto ok = owner->check(); !ok) return ok;
        auto& qSlot = owner->queues[queueIndex(qType)];
        @autoreleasepool {
            if (!qSlot.pending.empty()) [qSlot.pending.back().buffer waitUntilCompleted];
        }
        return owner->pollQueue(qType);
    }
    Result<std::uint64_t> completedValue() override {
        if (auto ok = owner->pollQueue(qType); !ok) return std::unexpected(ok.error());
        return owner->queues[queueIndex(qType)].completed.load(std::memory_order_acquire);
    }
};
#include "MetalPresentation.hpp"
struct MetalDevice final : Device {
    std::shared_ptr<State> owner;
    DeviceCaps capabilities;
    std::array<MetalQueue, 3> queues;
    MetalDevice(std::shared_ptr<State> state, DeviceCaps caps)
        : owner(std::move(state)),
          capabilities(std::move(caps)),
          queues{MetalQueue(owner, QueueType::Graphics),
                 MetalQueue(owner, QueueType::Compute),
                 MetalQueue(owner, QueueType::Copy)} {
        owner->profiler = profilingTracker();
    }
    ~MetalDevice() override { owner->retire(); }
    const DeviceCaps& caps() const override { return capabilities; }
    Result<FormatSupport> queryFormatSupport(Format format) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        const auto blockInfo = queryFormatBlockInfo(format);
        if (!blockInfo) {
            return fail(ErrorCode::InvalidArgument, "Unknown texture format");
        }
        FormatSupport result{};
        result.format = format;
        if (blockInfo->compressed && !metalSupportsCompressedFormat(owner->device, format)) {
            result.sampleCounts = 0u;
            return result;
        }
        const bool isDepth = format == Format::D32Float;
        const bool isCompressed = blockInfo->compressed;
        const bool isSrgb = format == Format::RGBA8SRGB || format == Format::BGRA8SRGB;
        result.texture2D = true;
        result.renderTarget = !isDepth && !isCompressed;
        result.depthStencil = isDepth;
        result.sampled = !isDepth;
        result.linearFilter = !isDepth;
        result.blendable = !isDepth && !isCompressed;
        result.copySource = true;
        result.copyDestination = true;
        result.partialRegionCopy = !isDepth;
        result.storage = !isDepth && !isCompressed && !isSrgb;
        result.sampleCounts = 1u;
        if (!isCompressed) {
            for (const std::uint32_t count : {2u, 4u, 8u}) {
                if ([owner->device supportsTextureSampleCount:count]) {
                    result.sampleCounts |= count;
                }
            }
        }
        return result;
    }
    Result<void> checkStatus() override { return owner->poll(); }
    Result<ResourceStatistics> resourceStatistics() override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        ResourceStatistics result;
        std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
        std::erase_if(owner->allocations, [](auto& w) { return w.expired(); });
        std::erase_if(owner->memoryHeaps, [](auto& w) { return w.expired(); });
        result.trackedObjects = owner->objects.size();
        for (const auto& weak : owner->allocations) if (auto a = weak.lock(); a && !a->heap && (a->buffer || a->texture)) { ++result.uniqueResources; result.resourceAllocationBytes += a->bytes; }
        for (const auto& weak : owner->memoryHeaps) if (auto h = weak.lock(); h && h->heap) { ++result.uniqueResources; result.resourceAllocationBytes += h->desc.size; }
        for (const auto& q : owner->queues) {
            result.pendingSubmissions += q.pending.size();
            for (const auto& p : q.pending) result.pendingObjectReferences += p.allocations.size() + p.timelineEvents.size();
        }
        return result;
    }
    Result<MemoryBudget> queryMemoryBudget(MemorySegment) override { return owner->unsupported(); }
    Result<std::unique_ptr<Device>> recover() override { return owner->unsupported(); }
    Result<void> removeForTesting() override { return owner->unsupported(); }
    Result<BufferPtr> createBuffer(BufferDesc desc) override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        if (!desc.size || desc.size > NSUIntegerMax || (desc.memory != Memory::Upload && desc.memory != Memory::Readback && desc.memory != Memory::Device) ||
            (desc.storage && desc.memory != Memory::Device))
            return fail(ErrorCode::InvalidArgument, "Invalid Metal buffer description");
        if (desc.size > owner->device.maxBufferLength) return fail(ErrorCode::Unsupported, "Metal buffer exceeds device limit");
        @autoreleasepool {
            auto a = std::make_shared<Allocation>(); a->desc = desc;
            MTLResourceOptions options = MTLResourceHazardTrackingModeTracked;
            if (desc.memory == Memory::Device) options |= MTLResourceStorageModePrivate;
            else options |= owner->device.hasUnifiedMemory ? MTLResourceStorageModeShared : MTLResourceStorageModeManaged;
            if (desc.memory == Memory::Upload) options |= MTLResourceCPUCacheModeWriteCombined;
            a->buffer = [owner->device newBufferWithLength:desc.size options:options];
            if (!a->buffer) return fail(ErrorCode::OutOfMemory, "Metal buffer allocation failed");
            const auto mode = desc.memory == Memory::Device ? MTLStorageModePrivate : owner->device.hasUnifiedMemory ? MTLStorageModeShared : MTLStorageModeManaged;
            a->bytes = a->buffer.allocatedSize;
            if (a->bytes < desc.size || a->buffer.storageMode != mode || a->buffer.hazardTrackingMode != MTLHazardTrackingModeTracked ||
                (desc.memory != Memory::Device && !a->buffer.contents))
                return fail(ErrorCode::BackendFailure, "Metal buffer allocation lacks required size or mapping");
            a->state = desc.memory == Memory::Upload ? ResourceState::CopySource : desc.memory == Memory::Readback ? ResourceState::CopyDestination : ResourceState::Common;
            auto value = std::make_shared<MetalBuffer>(); value->owner = owner; value->allocation = a; value->token = std::make_shared<int>();
            std::erase_if(owner->allocations, [](auto& w) { return w.expired(); });
            std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
            owner->allocations.push_back(a); owner->objects.push_back(value->token);
            return BufferPtr(value);
        }
    }
    Result<TexturePtr> createTexture(TextureDesc desc) override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        if (auto ok = validateTextureDescription(desc); !ok) return std::unexpected(ok.error());
        if (!metalSupportsCompressedFormat(owner->device, desc.format)) {
            return fail(ErrorCode::Unsupported, "Metal device does not support requested compressed texture format");
        }
        if (desc.sampleCount > 1u && ![owner->device supportsTextureSampleCount:desc.sampleCount]) {
            return fail(ErrorCode::Unsupported, "Metal device does not support requested MSAA sample count");
        }
        @autoreleasepool {
            const auto format = pixelFormat(desc.format);
            if (format == MTLPixelFormatInvalid) {
                return fail(ErrorCode::Unsupported, "Metal pixel format unavailable");
            }
            const std::uint32_t sliceH = desc.dimension == TextureDimension::Texture1D ? 1u : desc.height;
            const std::uint32_t texDepth = desc.dimension == TextureDimension::Texture3D ? desc.depth : 1u;
            MTLTextureDescriptor* descriptor = [[MTLTextureDescriptor alloc] init];
            if (!descriptor) return fail(ErrorCode::OutOfMemory, "Metal texture descriptor creation failed");
            descriptor.textureType = metalTextureType(desc.dimension, desc.sampleCount);
            descriptor.pixelFormat = format;
            descriptor.width = desc.width;
            descriptor.height = sliceH;
            descriptor.depth = texDepth;
            descriptor.mipmapLevelCount = desc.mipLevels;
            descriptor.arrayLength = desc.dimension == TextureDimension::Texture2DArray ? desc.arrayLayers : 1u;
            descriptor.resourceOptions = MTLResourceStorageModePrivate | MTLResourceHazardTrackingModeTracked;
            if (desc.sampleCount > 1u) {
                descriptor.sampleCount = desc.sampleCount;
                descriptor.usage = MTLTextureUsageRenderTarget;
            } else {
                MTLTextureUsage usage = desc.depthStencil ? MTLTextureUsageRenderTarget : MTLTextureUsageShaderRead;
                if (desc.renderTarget) usage |= MTLTextureUsageRenderTarget;
                if (desc.storage) usage |= MTLTextureUsageShaderWrite;
                descriptor.usage = usage;
            }
            auto a = std::make_shared<Allocation>(); a->textureDesc = desc;
            a->texture = [owner->device newTextureWithDescriptor:descriptor];
            if (!a->texture) return fail(ErrorCode::OutOfMemory, "Metal texture allocation failed");
            a->bytes = a->texture.allocatedSize;
            if (!a->bytes || a->texture.storageMode != MTLStorageModePrivate || a->texture.hazardTrackingMode != MTLHazardTrackingModeTracked ||
                a->texture.pixelFormat != format || a->texture.width != desc.width || a->texture.height != sliceH || a->texture.depth != texDepth)
                return fail(ErrorCode::BackendFailure, "Metal texture descriptor/storage mismatch");
            if (desc.sampleCount == 1u) {
                const auto fp = computeTextureRegionFootprint(desc.format, desc.width, sliceH);
                if (!fp) return std::unexpected(fp.error());
                a->footprint = {fp->size * texDepth, fp->rowPitch, fp->rowBytes, fp->height};
            } else {
                a->footprint = {};
            }
            auto value = std::make_shared<MetalTexture>(); value->owner = owner; value->allocation = a; value->token = std::make_shared<int>();
            std::erase_if(owner->allocations, [](auto& w) { return w.expired(); });
            std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
            owner->allocations.push_back(a); owner->objects.push_back(value->token);
            return TexturePtr(value);
        }
    }
    Result<ResourceMemoryRequirements> queryBufferMemoryRequirements(BufferDesc desc) override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        if (!desc.size || desc.size > NSUIntegerMax || desc.memory != Memory::Device) {
            return fail(ErrorCode::InvalidArgument, "Placed Metal buffer requires non-empty Device memory");
        }
        if (desc.size > owner->device.maxBufferLength) return fail(ErrorCode::Unsupported, "Metal buffer exceeds device limit");
        @autoreleasepool {
            const MTLResourceOptions options = MTLResourceStorageModePrivate | MTLResourceHazardTrackingModeTracked;
            const MTLSizeAndAlign sa = [owner->device heapBufferSizeAndAlignWithLength:desc.size options:options];
            if (!sa.size) return fail(ErrorCode::BackendFailure, "Metal heap buffer size query failed");
            return ResourceMemoryRequirements{
                static_cast<std::uint64_t>(sa.size),
                std::max<std::uint64_t>(1u, static_cast<std::uint64_t>(sa.align)),
                MemoryHeapType::Buffers
            };
        }
    }
    Result<ResourceMemoryRequirements> queryTextureMemoryRequirements(TextureDesc desc) override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        if (auto ok = validateTextureDescription(desc); !ok) return std::unexpected(ok.error());
        if (!metalSupportsCompressedFormat(owner->device, desc.format)) {
            return fail(ErrorCode::Unsupported, "Metal device does not support requested compressed texture format");
        }
        if (desc.sampleCount > 1u && ![owner->device supportsTextureSampleCount:desc.sampleCount]) {
            return fail(ErrorCode::Unsupported, "Metal device does not support requested MSAA sample count");
        }
        @autoreleasepool {
            const auto format = pixelFormat(desc.format);
            if (format == MTLPixelFormatInvalid) return fail(ErrorCode::Unsupported, "Metal pixel format unavailable");
            const std::uint32_t sliceH = desc.dimension == TextureDimension::Texture1D ? 1u : desc.height;
            const std::uint32_t texDepth = desc.dimension == TextureDimension::Texture3D ? desc.depth : 1u;
            MTLTextureDescriptor* descriptor = [[MTLTextureDescriptor alloc] init];
            descriptor.textureType = metalTextureType(desc.dimension, desc.sampleCount);
            descriptor.pixelFormat = format;
            descriptor.width = desc.width;
            descriptor.height = sliceH;
            descriptor.depth = texDepth;
            descriptor.mipmapLevelCount = desc.mipLevels;
            descriptor.arrayLength = desc.dimension == TextureDimension::Texture2DArray ? desc.arrayLayers : 1u;
            descriptor.resourceOptions = MTLResourceStorageModePrivate | MTLResourceHazardTrackingModeTracked;
            if (desc.sampleCount > 1u) {
                descriptor.sampleCount = desc.sampleCount;
                descriptor.usage = MTLTextureUsageRenderTarget;
            } else {
                MTLTextureUsage usage = desc.depthStencil ? MTLTextureUsageRenderTarget : MTLTextureUsageShaderRead;
                if (desc.renderTarget) usage |= MTLTextureUsageRenderTarget;
                if (desc.storage) usage |= MTLTextureUsageShaderWrite;
                descriptor.usage = usage;
            }
            const MTLSizeAndAlign sa = [owner->device heapTextureSizeAndAlignWithDescriptor:descriptor];
            if (!sa.size) return fail(ErrorCode::BackendFailure, "Metal heap texture size query failed");
            const MemoryHeapType heapType = (desc.renderTarget || desc.depthStencil)
                ? MemoryHeapType::RenderTargets
                : MemoryHeapType::Textures;
            return ResourceMemoryRequirements{
                static_cast<std::uint64_t>(sa.size),
                std::max<std::uint64_t>(1u, static_cast<std::uint64_t>(sa.align)),
                heapType
            };
        }
    }
    Result<MemoryHeapPtr> createMemoryHeap(MemoryHeapDesc desc) override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        if (auto ok = validateMemoryHeapDesc(desc); !ok) return std::unexpected(ok.error());
        @autoreleasepool {
            MTLHeapDescriptor* hd = [[MTLHeapDescriptor alloc] init];
            hd.size = static_cast<NSUInteger>(desc.size);
            hd.storageMode = MTLStorageModePrivate;
            hd.hazardTrackingMode = MTLHazardTrackingModeTracked;
            hd.type = MTLHeapTypePlacement;
            id<MTLHeap> nativeHeap = [owner->device newHeapWithDescriptor:hd];
            if (!nativeHeap) return fail(ErrorCode::OutOfMemory, "Metal heap allocation failed");
            auto h = std::make_shared<NativeMemoryHeap>();
            h->heap = nativeHeap;
            h->desc = desc;
            auto wrapper = std::make_shared<MetalMemoryHeap>();
            wrapper->owner = owner;
            wrapper->heap = h;
            wrapper->token = std::make_shared<int>();
            std::erase_if(owner->memoryHeaps, [](auto& w) { return w.expired(); });
            std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
            owner->memoryHeaps.push_back(h);
            owner->objects.push_back(wrapper->token);
            return MemoryHeapPtr(std::move(wrapper));
        }
    }
    Result<BufferPtr> createPlacedBuffer(const MemoryHeapPtr& heap, std::uint64_t heapOffset, BufferDesc desc) override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        if (!heap) return fail(ErrorCode::InvalidArgument, "Null MemoryHeap");
        auto mtlHeap = std::dynamic_pointer_cast<MetalMemoryHeap>(heap);
        if (!mtlHeap || mtlHeap->owner != owner) return fail(ErrorCode::WrongDevice, "MemoryHeap belongs to another generation");
        auto req = queryBufferMemoryRequirements(desc);
        if (!req) return std::unexpected(req.error());
        if (mtlHeap->heap->desc.type != req->heapType) {
            return fail(ErrorCode::InvalidArgument, "MemoryHeapType mismatch for placed buffer");
        }
        if (heapOffset % req->alignment != 0u) {
            return fail(ErrorCode::InvalidArgument, "Placed buffer heapOffset is not aligned");
        }
        if (heapOffset > mtlHeap->heap->desc.size || req->size > mtlHeap->heap->desc.size - heapOffset) {
            return fail(ErrorCode::InvalidArgument, "Placed buffer exceeds MemoryHeap size");
        }
        @autoreleasepool {
            const MTLResourceOptions options = MTLResourceStorageModePrivate | MTLResourceHazardTrackingModeTracked;
            id<MTLBuffer> buf = [mtlHeap->heap->heap newBufferWithLength:desc.size options:options offset:static_cast<NSUInteger>(heapOffset)];
            if (!buf) return fail(ErrorCode::OutOfMemory, "Metal placed buffer allocation failed");
            auto a = std::make_shared<Allocation>();
            a->heap = mtlHeap->heap;
            a->buffer = buf;
            a->desc = desc;
            a->state = ResourceState::Common;
            a->bytes = 0;
            mtlHeap->heap->entries.push_back({a, heapOffset, req->size});
            auto value = std::make_shared<MetalBuffer>();
            value->owner = owner;
            value->allocation = a;
            value->token = std::make_shared<int>();
            std::erase_if(owner->allocations, [](auto& w) { return w.expired(); });
            std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
            owner->allocations.push_back(a);
            owner->objects.push_back(value->token);
            return BufferPtr(std::move(value));
        }
    }
    Result<TexturePtr> createPlacedTexture(const MemoryHeapPtr& heap, std::uint64_t heapOffset, TextureDesc desc) override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        if (!heap) return fail(ErrorCode::InvalidArgument, "Null MemoryHeap");
        auto mtlHeap = std::dynamic_pointer_cast<MetalMemoryHeap>(heap);
        if (!mtlHeap || mtlHeap->owner != owner) return fail(ErrorCode::WrongDevice, "MemoryHeap belongs to another generation");
        auto req = queryTextureMemoryRequirements(desc);
        if (!req) return std::unexpected(req.error());
        if (mtlHeap->heap->desc.type != req->heapType) {
            return fail(ErrorCode::InvalidArgument, "MemoryHeapType mismatch for placed texture");
        }
        if (heapOffset % req->alignment != 0u) {
            return fail(ErrorCode::InvalidArgument, "Placed texture heapOffset is not aligned");
        }
        if (heapOffset > mtlHeap->heap->desc.size || req->size > mtlHeap->heap->desc.size - heapOffset) {
            return fail(ErrorCode::InvalidArgument, "Placed texture exceeds MemoryHeap size");
        }
        @autoreleasepool {
            const auto format = pixelFormat(desc.format);
            const std::uint32_t sliceH = desc.dimension == TextureDimension::Texture1D ? 1u : desc.height;
            const std::uint32_t texDepth = desc.dimension == TextureDimension::Texture3D ? desc.depth : 1u;
            MTLTextureDescriptor* descriptor = [[MTLTextureDescriptor alloc] init];
            descriptor.textureType = metalTextureType(desc.dimension, desc.sampleCount);
            descriptor.pixelFormat = format;
            descriptor.width = desc.width;
            descriptor.height = sliceH;
            descriptor.depth = texDepth;
            descriptor.mipmapLevelCount = desc.mipLevels;
            descriptor.arrayLength = desc.dimension == TextureDimension::Texture2DArray ? desc.arrayLayers : 1u;
            descriptor.resourceOptions = MTLResourceStorageModePrivate | MTLResourceHazardTrackingModeTracked;
            if (desc.sampleCount > 1u) {
                descriptor.sampleCount = desc.sampleCount;
                descriptor.usage = MTLTextureUsageRenderTarget;
            } else {
                MTLTextureUsage usage = desc.depthStencil ? MTLTextureUsageRenderTarget : MTLTextureUsageShaderRead;
                if (desc.renderTarget) usage |= MTLTextureUsageRenderTarget;
                if (desc.storage) usage |= MTLTextureUsageShaderWrite;
                descriptor.usage = usage;
            }
            id<MTLTexture> tex = [mtlHeap->heap->heap newTextureWithDescriptor:descriptor offset:static_cast<NSUInteger>(heapOffset)];
            if (!tex) return fail(ErrorCode::OutOfMemory, "Metal placed texture allocation failed");
            auto a = std::make_shared<Allocation>();
            a->heap = mtlHeap->heap;
            a->texture = tex;
            a->textureDesc = desc;
            a->bytes = 0;
            if (desc.sampleCount == 1u) {
                const auto fp = computeTextureRegionFootprint(desc.format, desc.width, sliceH);
                if (!fp) return std::unexpected(fp.error());
                a->footprint = {fp->size * texDepth, fp->rowPitch, fp->rowBytes, fp->height};
            }
            mtlHeap->heap->entries.push_back({a, heapOffset, req->size});
            auto value = std::make_shared<MetalTexture>();
            value->owner = owner;
            value->allocation = a;
            value->token = std::make_shared<int>();
            std::erase_if(owner->allocations, [](auto& w) { return w.expired(); });
            std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
            owner->allocations.push_back(a);
            owner->objects.push_back(value->token);
            return TexturePtr(std::move(value));
        }
    }
    Result<SparseTextureProperties> querySparseTextureProperties(TextureDesc desc) override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        if (auto ok = validateTextureDescription(desc); !ok) return std::unexpected(ok.error());
        if (!metalSupportsCompressedFormat(owner->device, desc.format)) {
            return fail(ErrorCode::Unsupported, "Metal device does not support requested compressed texture format");
        }
        SparseTextureProperties result{};
        if (!capabilities.sparseResidencyTexture2D || desc.sampleCount > 1u ||
            (desc.dimension != TextureDimension::Texture2D &&
             desc.dimension != TextureDimension::Texture2DArray &&
             desc.dimension != TextureDimension::TextureCube)) {
            return result;
        }
        @autoreleasepool {
            const auto format = pixelFormat(desc.format);
            if (format == MTLPixelFormatInvalid) return result;
            const MTLSize tileSize = [owner->device sparseTileSizeWithTextureType:metalTextureType(desc.dimension, 1u)
                                                                     pixelFormat:format
                                                                     sampleCount:1];
            if (tileSize.width == 0 || tileSize.height == 0) return result;
            const std::uint64_t tileBytes = static_cast<std::uint64_t>(owner->device.sparseTileSizeInBytes);
            if (tileBytes == 0u) return result;
            result.supported = true;
            result.tileWidth = static_cast<std::uint32_t>(tileSize.width);
            result.tileHeight = static_cast<std::uint32_t>(tileSize.height);
            result.tileDepth = static_cast<std::uint32_t>(tileSize.depth ? tileSize.depth : 1);
            result.standardTileBytes = static_cast<std::uint32_t>(tileBytes);
            std::uint32_t totalTiles = 0;
            std::uint32_t packedStart = desc.mipLevels;
            const std::uint32_t layers = std::max(1u, desc.arrayLayers);
            for (std::uint32_t mip = 0; mip < desc.mipLevels; ++mip) {
                const std::uint32_t mw = std::max(1u, desc.width >> mip);
                const std::uint32_t mh = std::max(1u, desc.height >> mip);
                if (mw < result.tileWidth || mh < result.tileHeight) {
                    if (packedStart == desc.mipLevels) packedStart = mip;
                } else {
                    const std::uint32_t tx = (mw + result.tileWidth - 1u) / result.tileWidth;
                    const std::uint32_t ty = (mh + result.tileHeight - 1u) / result.tileHeight;
                    totalTiles += tx * ty * layers;
                }
            }
            result.packedMipStart = packedStart;
            result.packedMipCount = packedStart < desc.mipLevels ? (desc.mipLevels - packedStart) : 0u;
            result.packedMipTiles = result.packedMipCount > 0u ? layers : 0u;
            result.totalTiles = totalTiles + result.packedMipTiles;
            return result;
        }
    }
    Result<TextureViewPtr> createTextureView(const TexturePtr& ptr, TextureViewDesc viewDesc) override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        if (!ptr) return fail(ErrorCode::InvalidArgument, "Null Metal texture");
        auto parent = std::dynamic_pointer_cast<MetalTexture>(ptr);
        if (!parent || parent->owner != owner) return fail(ErrorCode::WrongDevice, "Metal texture belongs to another generation");
        if (parent->allocation->backbuffer && !parent->allocation->texture) return fail(ErrorCode::InvalidState, "Metal drawable is no longer current");
        auto resolved = validateTextureView(parent->allocation->textureDesc, viewDesc);
        if (!resolved) return std::unexpected(resolved.error());
        if (resolved->usage == TextureViewUsage::ShaderRead && parent->allocation->backbuffer) {
            return fail(ErrorCode::InvalidArgument, "Metal drawable cannot create a ShaderRead view");
        }
        @autoreleasepool {
            const MTLPixelFormat format = pixelFormat(*resolved->format);
            id<MTLTexture> nativeTex = [parent->allocation->texture
                newTextureViewWithPixelFormat:format
                textureType:metalTextureType(*resolved->dimension, parent->allocation->textureDesc.sampleCount)
                levels:NSMakeRange(resolved->baseMipLevel, resolved->mipLevelCount)
                slices:NSMakeRange(resolved->baseArrayLayer, resolved->arrayLayerCount)];
            if (!nativeTex) return fail(ErrorCode::OutOfMemory, "Metal texture view creation failed");
            auto nativeView = std::make_shared<NativeTextureView>();
            nativeView->allocation = parent->allocation;
            nativeView->texture = nativeTex;
            nativeView->desc = *resolved;
            auto value = std::make_shared<MetalTextureView>();
            value->owner = owner;
            value->parent = parent;
            value->native = nativeView;
            value->token = std::make_shared<int>();
            std::erase_if(owner->imageViews, [](auto& w) { return w.expired(); });
            std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
            owner->imageViews.push_back(nativeView);
            owner->objects.push_back(value->token);
            return TextureViewPtr(value);
        }
    }
    Result<PipelinePtr> createGraphicsPipeline(const GraphicsPipelineDesc& desc) override {
        auto p=graphics(*owner,desc);if(!p)return std::unexpected(p.error());
        auto wrapper=std::make_shared<MetalPipeline>();wrapper->owner=owner;wrapper->native=*p;wrapper->token=std::make_shared<int>();
        std::erase_if(owner->pipelines,[](auto& w){return w.expired();});std::erase_if(owner->objects,[](auto& w){return w.expired();});
        owner->pipelines.push_back(*p);owner->objects.push_back(wrapper->token);return PipelinePtr(wrapper);
    }
    Result<PipelinePtr> createComputePipeline(const ComputePipelineDesc& desc) override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        if (!desc.threadgroupSizeX || !desc.threadgroupSizeY || !desc.threadgroupSizeZ) {
            return fail(ErrorCode::InvalidArgument, "Metal compute threadgroup dimensions must be positive");
        }
        if (auto ok = validateBindingLayout(desc.bindingLayout); !ok) return std::unexpected(ok.error());
        for (const auto& e : desc.bindingLayout.entries) {
            if (e.space) return fail(ErrorCode::Unsupported, "Metal compute binding profile requires space zero");
            if (e.visibility != ShaderVisibility::Compute && e.visibility != ShaderVisibility::All)
                return fail(ErrorCode::InvalidArgument, "Metal compute binding must have Compute or All visibility");
        }
        for (const auto& s : desc.bindingLayout.samplers) {
            if (s.space) return fail(ErrorCode::Unsupported, "Metal compute sampler profile requires space zero");
            if (s.visibility != ShaderVisibility::Compute && s.visibility != ShaderVisibility::All)
                return fail(ErrorCode::InvalidArgument, "Metal compute sampler must have Compute or All visibility");
        }
        @autoreleasepool {
            auto kernel = function(*owner, desc.compute, ShaderStage::Compute);
            if (!kernel) return std::unexpected(kernel.error());
            auto p = std::make_shared<NativePipeline>();
            p->isCompute = true;
            p->bindings = desc.bindingLayout;
            p->minimumBytes.resize(p->bindings.entries.size());
            p->threadgroupSizeX = desc.threadgroupSizeX;
            p->threadgroupSizeY = desc.threadgroupSizeY;
            p->threadgroupSizeZ = desc.threadgroupSizeZ;
            NSError* error = nil;
            p->computePipeline = [owner->device newComputePipelineStateWithFunction:*kernel error:&error];
            if (!p->computePipeline) {
                const char* message = error.localizedDescription.UTF8String;
                return fail(ErrorCode::BackendFailure, message ? message : "Metal compute pipeline creation failed", error.code);
            }
            const std::uint64_t threads = static_cast<std::uint64_t>(desc.threadgroupSizeX) * desc.threadgroupSizeY * desc.threadgroupSizeZ;
            if (threads > p->computePipeline.maxTotalThreadsPerThreadgroup) {
                return fail(ErrorCode::Unsupported, "Metal compute threadgroup exceeds pipeline limit");
            }
            for (const auto& s : p->bindings.samplers) {
                MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
                sd.minFilter = sd.magFilter = s.filter == SamplerFilter::Point ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
                sd.sAddressMode = sd.tAddressMode = sd.rAddressMode = s.address == SamplerAddress::Clamp ? MTLSamplerAddressModeClampToEdge : MTLSamplerAddressModeRepeat;
                id<MTLSamplerState> sampler = [owner->device newSamplerStateWithDescriptor:sd];
                if (!sampler) return fail(ErrorCode::OutOfMemory, "Metal compute sampler creation failed");
                p->samplers.push_back(sampler);
            }
            auto wrapper = std::make_shared<MetalPipeline>();
            wrapper->owner = owner;
            wrapper->native = p;
            wrapper->token = std::make_shared<int>();
            std::erase_if(owner->pipelines, [](auto& w) { return w.expired(); });
            std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
            owner->pipelines.push_back(p);
            owner->objects.push_back(wrapper->token);
            return PipelinePtr(wrapper);
        }
    }
    Result<PipelinePtr> createMeshPipeline(const MeshPipelineDesc& desc) override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        const auto targets = desc.renderTargets.value_or(RenderTargetLayout{{desc.colorFormat}, {}});
        if (auto ok = validateRenderTargets(targets, desc.depthState); !ok) return std::unexpected(ok.error());
        if (auto ok = validateBindingLayout(desc.bindingLayout); !ok) return std::unexpected(ok.error());
        if (desc.mesh.stage != ShaderStage::Mesh || desc.mesh.bytes.empty()) {
            return fail(ErrorCode::InvalidArgument, "MeshPipelineDesc requires a non-empty Mesh shader");
        }
        if (desc.task && (desc.task->stage != ShaderStage::Task || desc.task->bytes.empty())) {
            return fail(ErrorCode::InvalidArgument, "MeshPipelineDesc task shader must have Task stage and non-empty bytes");
        }
        if (!desc.fragment.bytes.empty() && desc.fragment.stage != ShaderStage::Fragment) {
            return fail(ErrorCode::InvalidArgument, "MeshPipelineDesc fragment shader must have Fragment stage");
        }
        if (!capabilities.meshShaders) {
            return fail(ErrorCode::Unsupported, "Metal device does not support Mesh/Object shaders");
        }
        return fail(ErrorCode::Unsupported, "Mesh/Task shader pipeline execution is not yet implemented in this SDK snapshot");
    }
    Result<std::unique_ptr<CommandList>> createCommandList() override {
        return createCommandList(QueueType::Graphics);
    }
    Result<std::unique_ptr<CommandList>> createCommandList(QueueType type) override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        if (!validQueueType(type)) return fail(ErrorCode::InvalidArgument, "Unknown queue type");
        return std::make_unique<MetalCommands>(owner, std::shared_ptr<NativeCommandPool>{}, type);
    }
    Result<std::unique_ptr<CommandPool>> createCommandPool() override {
        return createCommandPool(QueueType::Graphics);
    }
    Result<std::unique_ptr<CommandPool>> createCommandPool(QueueType type) override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        if (!validQueueType(type)) return fail(ErrorCode::InvalidArgument, "Unknown queue type");
        auto value = std::make_unique<MetalCommandPool>();
        value->owner = owner;
        value->pool = std::make_shared<NativeCommandPool>();
        value->pool->qType = type;
        value->pool->recordingThread.store(owner->thread, std::memory_order_release);
        value->token = std::make_shared<int>();
        std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
        owner->objects.push_back(value->token);
        return std::unique_ptr<CommandPool>(std::move(value));
    }
    Result<TimelineFencePtr> createTimelineFence(std::uint64_t initialValue) override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        if (initialValue >= UINT64_MAX - 1) return fail(ErrorCode::InvalidArgument, "Invalid initial timeline fence value");
        @autoreleasepool {
            id<MTLSharedEvent> event = [owner->device newSharedEvent];
            if (!event) return fail(ErrorCode::OutOfMemory, "Metal shared event creation failed");
            event.signaledValue = initialValue;
            auto native = std::make_shared<NativeTimelineEvent>();
            native->event = event;
            native->lastSignaled = initialValue;
            auto value = std::make_shared<MetalTimelineFence>();
            value->owner = owner;
            value->native = native;
            value->token = std::make_shared<int>();
            std::erase_if(owner->timelineEvents, [](auto& w) { return w.expired(); });
            std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
            owner->timelineEvents.push_back(native);
            owner->objects.push_back(value->token);
            return TimelineFencePtr(std::move(value));
        }
    }
    Result<void> checkSurfaceSupport(Surface surface) override {
        auto layer = checkedLayer(*owner, surface);
        if (!layer) return std::unexpected(layer.error());
        return {};
    }
    Result<SurfacePresentCapabilities> querySurfacePresentCapabilities(Surface surface) override {
        return queryMetalSurfacePresentCapabilities(*owner, surface);
    }
    Result<SurfaceColorCapabilities> querySurfaceColorCapabilities(Surface surface) override {
        return queryMetalSurfaceColorCapabilities(*owner, surface);
    }
    Result<std::unique_ptr<SwapChain>> createSwapChain(Surface surface, SwapChainDesc desc) override {
        return makeMetalSwapChain(owner, surface, desc);
    }
    CommandQueue& queue() override { return queues[0]; }
    Result<CommandQueue*> queue(QueueType type) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        if (!validQueueType(type)) return fail(ErrorCode::InvalidArgument, "Unknown queue type");
        return &queues[queueIndex(type)];
    }
    Result<std::uint64_t> validationErrorCount() override { return owner->unsupported(); }
};
}
Result<std::vector<AdapterInfo>> enumerateMetalAdapters() {
    @autoreleasepool {
        std::vector<AdapterInfo> result;
        for (id<MTLDevice> device in MTLCopyAllDevices()) {
            const char* name = device.name.UTF8String;
            if (!device.registryID || !name || !*name) return fail(ErrorCode::BackendFailure, "Metal adapter has no registry identity or name");
            AdapterInfo info;
            info.id = {Backend::Metal, device.registryID}; info.name = name;
            // Metal exposes neither a portable software/virtual classification nor PCI memory totals.
            // false is the only supported selection policy; it does not establish physical hardware.
            result.push_back(std::move(info));
        }
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.id.value < b.id.value; });
        if (std::adjacent_find(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.id == b.id; }) != result.end())
            return fail(ErrorCode::BackendFailure, "Metal registry identities are ambiguous");
        return result;
    }
}
Result<std::unique_ptr<Device>> createMetalDevice(DeviceDesc desc) {
    if (desc.validation || desc.softwareAdapter || desc.enableDeviceLossTesting)
        return fail(ErrorCode::Unsupported, "Metal submission backend does not support requested validation, software or device-loss policy");
    @autoreleasepool {
        id<MTLDevice> selected = nil;
        if (desc.adapter) {
            for (id<MTLDevice> candidate in MTLCopyAllDevices()) {
                if (candidate.registryID != desc.adapter->value) continue;
                if (selected) return fail(ErrorCode::BackendFailure, "Metal registry identity is ambiguous");
                selected = candidate;
            }
        } else selected = MTLCreateSystemDefaultDevice();
        if (!selected) return fail(ErrorCode::Unsupported, "Requested Metal device is unavailable; no fallback");
        const char* name = selected.name.UTF8String;
        if (!selected.registryID || !name || !*name) return fail(ErrorCode::BackendFailure, "Selected Metal device has no identity or name");
        auto state = std::make_shared<State>();
        const auto generation = detail::allocateDeviceGeneration();
        if (!generation) return fail(ErrorCode::InvalidState, "Device generation sequence exhausted");
        state->device = selected; state->generation = generation;
        for (std::size_t i = 0; i < 3; ++i) {
            state->queues[i].queue = [selected newCommandQueue];
            if (!state->queues[i].queue) return fail(ErrorCode::OutOfMemory, "Metal command queue creation failed");
            state->queues[i].id = (i == 0) ? generation : detail::allocateDeviceGeneration();
            if (!state->queues[i].id) return fail(ErrorCode::InvalidState, "Metal queue generation sequence exhausted");
        }
        DeviceCaps caps;
        caps.backend = Backend::Metal; caps.adapterName = name; caps.adapterId = {Backend::Metal, selected.registryID};
        caps.depthAttachment = true; caps.multipleRenderTargets = true;
        caps.compute = true;
        caps.swapChain = true;
        caps.vertexInput = true; caps.indexedDrawing = true; caps.resourceBinding = true; caps.bindingLayouts = true;
        caps.dedicatedComputeQueue = true;
        caps.dedicatedCopyQueue = true;
        caps.timelineFences = true;
        caps.resourceAliasing = true;
        caps.residencyControl = false;
        const bool sparseSupported = [selected supportsFamily:MTLGPUFamilyApple6];
        caps.sparseBinding = sparseSupported;
        caps.sparseResidencyTexture2D = sparseSupported;
        caps.indirectDraw = true;
        caps.multiDrawIndirect = true;
        caps.maxDrawIndirectCount = 65536u;
        caps.descriptorIndexing = selected.argumentBuffersSupport >= MTLArgumentBuffersTier2;
        caps.maxBindlessDescriptors = caps.descriptorIndexing ? 500000u : 0u;
        caps.meshShaders = [selected supportsFamily:MTLGPUFamilyApple7] || [selected supportsFamily:MTLGPUFamilyMac2];
        caps.maxTextureDimension2D = 16384u;
        caps.maxColorAttachments = 8u;
        caps.constantBufferAlignment = 256u;
        caps.textureCopyRowAlignment = 256u;
        caps.textureCopyOffsetAlignment = 512u;
        caps.maxVertexStreams = 15u;
        caps.maxConstantBufferSize = 65536u;
        return std::make_unique<MetalDevice>(std::move(state), std::move(caps));
    }
}
}
