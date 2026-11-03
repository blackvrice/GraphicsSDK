// Private Objective-C++ implementation; CMake explicitly selects OBJCXX for this unit.
#include <graphics/Graphics.hpp>
#include "../../foundation/DeviceGeneration.hpp"
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <algorithm>
#include <deque>
#include <cstring>
#include <limits>
#include <thread>

namespace gfx {
namespace {
auto fail(ErrorCode code, std::string text, std::int64_t native = 0) {
    return std::unexpected(Error{code, std::move(text), native});
}
struct Pending {
    std::uint64_t serial;
    id<MTLCommandBuffer> buffer;
    std::vector<std::shared_ptr<struct Allocation>> allocations;
};
struct Allocation {
    id<MTLBuffer> buffer;
    BufferDesc desc;
    ResourceState state{ResourceState::Common};
    std::uint64_t version{}, lastUse{}, bytes{};
};
struct Use {
    std::shared_ptr<Allocation> allocation;
    std::uint64_t version;
    ResourceState shadow;
};
struct Operation {
    std::shared_ptr<Allocation> source, destination;
    std::uint64_t bytes{};
    std::string marker;
};
struct NativeRecording { id<MTLCommandBuffer> buffer; };
struct State {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    const std::thread::id thread = std::this_thread::get_id();
    std::uint64_t generation{}, last{}, completed{};
    bool retired{};
    std::optional<Error> failure;
    std::deque<Pending> pending;
    std::vector<std::weak_ptr<Allocation>> allocations;
    std::vector<std::weak_ptr<int>> objects;
    id<MTLComputePipelineState> tailPipeline;
    std::vector<std::weak_ptr<NativeRecording>> recordings;
    void retire() {
        if (retired) return;
        @autoreleasepool {
            for (auto& p : pending) [p.buffer waitUntilCompleted];
            pending.clear();
            for (auto& w : recordings) if (auto r = w.lock()) r->buffer = nil;
            for (auto& w : allocations) if (auto a = w.lock()) a->buffer = nil;
            tailPipeline = nil; queue = nil; device = nil;
        }
        retired = true;
    }
    ~State() { retire(); }
    Result<void> check() const {
        if (std::this_thread::get_id() != thread) return fail(ErrorCode::InvalidState, "Metal access requires the creating thread");
        if (retired) return fail(ErrorCode::DeviceLost, "Metal device generation retired");
        if (failure) return std::unexpected(*failure);
        return {};
    }
    Result<void> poll() {
        if (auto ok = check(); !ok) return ok;
        @autoreleasepool {
            while (!pending.empty()) {
                auto& p = pending.front();
                const auto status = p.buffer.status;
                if (status == MTLCommandBufferStatusError) {
                    NSError* error = p.buffer.error;
                    const bool metal = [error.domain isEqualToString:MTLCommandBufferErrorDomain];
                    const auto code = error ? error.code : 0;
                    const auto category = metal && (code == MTLCommandBufferErrorDeviceRemoved || code == MTLCommandBufferErrorAccessRevoked)
                        ? ErrorCode::DeviceLost : ErrorCode::BackendFailure;
                    const char* message = error.localizedDescription.UTF8String;
                    failure = Error{category, message ? message : "Metal command buffer failed", code};
                    return std::unexpected(*failure);
                }
                if (status != MTLCommandBufferStatusCompleted) break;
                completed = p.serial;
                pending.pop_front();
            }
        }
        return {};
    }
    Result<void> prepareTail() {
        if (tailPipeline) return {};
        @autoreleasepool {
            // macOS blits require a four-byte size. One thread copies only the logical tail,
            // preserving all destination bytes beyond the requested copy (including partial copies).
            NSString* source = @"#include <metal_stdlib>\nusing namespace metal;\n"
                "kernel void gfx_copy_tail(const device uchar* src [[buffer(0)]], device uchar* dst [[buffer(1)]],"
                "constant ulong& offset [[buffer(2)]], constant uint& count [[buffer(3)]]) {"
                "for (uint i=0; i<count; ++i) dst[offset+i]=src[offset+i]; }";
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
    // Later slices implement textures, drawing, presentation and native validation.
    auto unsupported() const {
        if (auto ok = check(); !ok) return std::unexpected(ok.error());
        return fail(ErrorCode::Unsupported, "Not implemented by the Metal submission backend");
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
        if (a.lastUse > owner->completed || a.version == UINT64_MAX)
            return fail(ErrorCode::InvalidState, "Metal upload busy or version exhausted");
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
        if (a.lastUse > owner->completed) return fail(ErrorCode::InvalidState, "Metal readback busy");
        std::vector<std::byte> result(static_cast<std::size_t>(bytes));
        @autoreleasepool {
            if (bytes) std::memcpy(result.data(), static_cast<const std::byte*>(a.buffer.contents)+offset, result.size());
        }
        return result;
    }
};
struct MetalCommands final : CommandList {
    std::shared_ptr<State> owner;
    std::shared_ptr<NativeRecording> native;
    bool open{}, ended{};
    std::uint64_t submitted{};
    std::vector<Use> uses;
    std::vector<Operation> operations;
    explicit MetalCommands(std::shared_ptr<State> state) : owner(std::move(state)) {}
    Result<void> begin() override {
        if (auto ok = owner->poll(); !ok) return ok;
        if (open || submitted > owner->completed) return fail(ErrorCode::InvalidState, "Metal command list is open or in flight");
        auto next = std::make_shared<NativeRecording>();
        std::erase_if(owner->recordings, [](auto& w) { return w.expired(); });
        owner->recordings.push_back(next); native = std::move(next);
        open = true; ended = false; submitted = 0;
        operations.clear(); uses.clear();
        return {};
    }
    Result<void> end() override {
        if (auto ok = owner->check(); !ok) return ok;
        if (!open) return fail(ErrorCode::InvalidState, "Metal command list is not recording");
        // Finalize into a fresh native buffer. An encoder/compiler failure discards that
        // uncommitted buffer and leaves the accepted CPU recording open and retryable.
        @autoreleasepool {
            if (std::any_of(operations.begin(), operations.end(), [](const auto& op) { return (op.bytes & 3) != 0; }))
                if (auto ok = owner->prepareTail(); !ok) return ok;
            id<MTLCommandBuffer> next = [owner->queue commandBuffer];
            if (!next) return fail(ErrorCode::OutOfMemory, "Metal command buffer creation failed");
            for (const auto& op : operations) {
                if (!op.bytes) {
                    id<MTLBlitCommandEncoder> encoder = [next blitCommandEncoder];
                    if (!encoder) return fail(ErrorCode::OutOfMemory, "Metal marker encoder creation failed");
                    NSString* label = [[NSString alloc] initWithBytes:op.marker.data() length:op.marker.size() encoding:NSUTF8StringEncoding];
                    [encoder insertDebugSignpost:label]; [encoder endEncoding];
                    continue;
                }
                const auto aligned = op.bytes & ~std::uint64_t{3};
                if (aligned) {
                    id<MTLBlitCommandEncoder> encoder = [next blitCommandEncoder];
                    if (!encoder) return fail(ErrorCode::OutOfMemory, "Metal blit encoder creation failed");
                    [encoder copyFromBuffer:op.source->buffer sourceOffset:0 toBuffer:op.destination->buffer destinationOffset:0 size:aligned];
                    [encoder endEncoding];
                }
                if (op.bytes != aligned) {
                    id<MTLComputeCommandEncoder> encoder = [next computeCommandEncoder];
                    if (!encoder) return fail(ErrorCode::OutOfMemory, "Metal tail encoder creation failed");
                    [encoder setComputePipelineState:owner->tailPipeline];
                    [encoder setBuffer:op.source->buffer offset:0 atIndex:0];
                    [encoder setBuffer:op.destination->buffer offset:0 atIndex:1];
                    [encoder setBytes:&aligned length:sizeof(aligned) atIndex:2];
                    const auto count = static_cast<std::uint32_t>(op.bytes-aligned);
                    [encoder setBytes:&count length:sizeof(count) atIndex:3];
                    [encoder dispatchThreads:MTLSizeMake(1,1,1) threadsPerThreadgroup:MTLSizeMake(1,1,1)];
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
        open = false; ended = true;
        return {};
    }
    Result<void> debugMarker(std::string_view text) override {
        if (auto ok = owner->check(); !ok) return ok;
        if (!open) return fail(ErrorCode::InvalidState, "Metal marker requires recording");
        @autoreleasepool {
            NSString* label = [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
            if (!label || text.empty() || text.find('\0') != std::string_view::npos)
                return fail(ErrorCode::InvalidArgument, "Metal marker requires nonempty UTF-8 without NUL");
            operations.push_back(Operation{{},{},0,std::string(text)});
        }
        return {};
    }
    Result<void> ready() const {
        if (auto ok = owner->check(); !ok) return ok;
        if (!open) return fail(ErrorCode::InvalidState, "Metal command list is not recording");
        return {};
    }
    Result<std::shared_ptr<Allocation>> allocation(const BufferPtr& value) const {
        auto buffer = std::dynamic_pointer_cast<MetalBuffer>(value);
        if (!buffer) return fail(value ? ErrorCode::WrongDevice : ErrorCode::InvalidArgument, "Expected a Metal buffer");
        if (buffer->owner != owner) return fail(ErrorCode::WrongDevice, "Metal buffer belongs to another generation");
        return buffer->allocation;
    }
    Use* find(const std::shared_ptr<Allocation>& a) {
        auto it = std::find_if(uses.begin(), uses.end(), [&](auto& u) { return u.allocation == a; });
        return it == uses.end() ? nullptr : &*it;
    }
    ResourceState shadow(const std::shared_ptr<Allocation>& a) { auto u = find(a); return u ? u->shadow : a->state; }
    Use& retain(const std::shared_ptr<Allocation>& a) {
        if (auto u = find(a)) return *u;
        return uses.emplace_back(Use{a,a->version,a->state});
    }
    Result<void> transition(const BufferPtr& value, ResourceState target) override {
        if (auto ok = ready(); !ok) return ok;
        auto a = allocation(value); if (!a) return std::unexpected(a.error());
        if (target != ResourceState::Common && target != ResourceState::CopySource && target != ResourceState::CopyDestination)
            return fail(ErrorCode::Unsupported, "Metal buffer state is outside the transfer slice");
        if (((*a)->desc.memory == Memory::Upload && target != ResourceState::CopySource) ||
            ((*a)->desc.memory == Memory::Readback && target != ResourceState::CopyDestination))
            return fail(ErrorCode::InvalidArgument, "Metal host buffer state is fixed");
        uses.reserve(uses.size()+1); retain(*a).shadow = target;
        // Explicitly tracked native resources synchronize accesses between encoders on this queue.
        return {};
    }
    Result<void> transition(const TexturePtr&, ResourceState) override { return owner->unsupported(); }
    Result<void> copyBuffer(const BufferPtr& source, const BufferPtr& destination, std::uint64_t bytes) override {
        if (auto ok = ready(); !ok) return ok;
        auto src = allocation(source); if (!src) return std::unexpected(src.error());
        auto dst = allocation(destination); if (!dst) return std::unexpected(dst.error());
        if (!bytes || bytes > (*src)->desc.size || bytes > (*dst)->desc.size || *src == *dst)
            return fail(ErrorCode::InvalidArgument, "Metal copy range or alias invalid");
        if (shadow(*src) != ResourceState::CopySource || shadow(*dst) != ResourceState::CopyDestination)
            return fail(ErrorCode::InvalidState, "Metal copy requires source and destination states");
        uses.reserve(uses.size()+2); operations.reserve(operations.size()+1);
        retain(*src); retain(*dst); operations.push_back(Operation{*src,*dst,bytes,{}});
        return {};
    }
    Result<void> copyBufferToTexture(const BufferPtr&, const TexturePtr&) override { return owner->unsupported(); }
    Result<void> copyTextureToBuffer(const TexturePtr&, const BufferPtr&) override { return owner->unsupported(); }
    Result<void> beginRenderPass(const TexturePtr&, Color) override { return owner->unsupported(); }
    Result<void> beginRenderPass(const RenderPassDesc&) override { return owner->unsupported(); }
    Result<void> endRenderPass() override { return owner->unsupported(); }
    Result<void> setPipeline(const PipelinePtr&) override { return owner->unsupported(); }
    Result<void> bindTexture(const TexturePtr&) override { return owner->unsupported(); }
    Result<void> bindResources(const ResourceBindings&) override { return owner->unsupported(); }
    Result<void> bindVertexBuffer(std::uint32_t, const BufferPtr&, std::uint32_t, std::uint64_t, std::uint64_t) override { return owner->unsupported(); }
    Result<void> bindIndexBuffer(const BufferPtr&, IndexFormat, std::uint64_t, std::uint64_t) override { return owner->unsupported(); }
    Result<void> setScissor(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override { return owner->unsupported(); }
    Result<void> draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override { return owner->unsupported(); }
    Result<void> drawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override { return owner->unsupported(); }
    Result<void> writeTimestamp(std::uint32_t) override { return owner->unsupported(); }
    Result<void> resolveTimestamps() override { return owner->unsupported(); }
    Result<double> elapsedGpuMilliseconds() override { return owner->unsupported(); }
};
struct MetalQueue final : CommandQueue {
    std::shared_ptr<State> owner;
    explicit MetalQueue(std::shared_ptr<State> state) : owner(std::move(state)) {}
    Result<Fence> submit(CommandList& value) override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        auto* commands = dynamic_cast<MetalCommands*>(&value);
        if (!commands || commands->owner != owner) return fail(ErrorCode::WrongDevice, "Metal command list belongs to another generation");
        if (!commands->ended || commands->open || commands->submitted) return fail(ErrorCode::InvalidState, "Metal submission requires an ended, unsubmitted command list");
        if (owner->last == UINT64_MAX) return fail(ErrorCode::InvalidState, "Metal fence sequence exhausted");
        for (const auto& use : commands->uses)
            if (use.version != use.allocation->version || use.allocation->version == UINT64_MAX)
                return fail(ErrorCode::InvalidState, "Stale Metal buffer observation");
        // Allocate the retention node before commit; retain even if the caller destroys its list.
        std::vector<std::shared_ptr<Allocation>> allocations;
        allocations.reserve(commands->uses.size());
        for (auto& use : commands->uses) allocations.push_back(use.allocation);
        owner->pending.push_back({owner->last + 1, commands->native->buffer, std::move(allocations)});
        @autoreleasepool { [commands->native->buffer commit]; }
        commands->submitted = ++owner->last;
        for (auto& use : commands->uses) {
            use.allocation->state = use.shadow; use.allocation->lastUse = owner->last; ++use.allocation->version;
        }
        return Fence{owner->last, owner->generation};
    }
    Result<void> wait(Fence fence, std::chrono::milliseconds timeout) override {
        if (auto ok = owner->check(); !ok) return ok;
        if (timeout.count() < 0 || timeout.count() > 4294967294ll) return fail(ErrorCode::InvalidArgument, "Metal fence timeout outside finite range");
        if (fence.device != owner->generation) return fail(ErrorCode::WrongDevice, "Metal fence belongs to another generation");
        if (!fence.value || fence.value > owner->last) return fail(ErrorCode::InvalidArgument, "Metal fence value was not submitted");
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        for (;;) {
            if (auto ok = owner->poll(); !ok) return ok;
            if (owner->completed >= fence.value) return {};
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) return fail(ErrorCode::Timeout, "Metal fence wait timed out");
            std::this_thread::sleep_for(std::min(deadline - now, std::chrono::steady_clock::duration(std::chrono::milliseconds(1))));
        }
    }
    Result<void> waitIdle() override {
        if (auto ok = owner->check(); !ok) return ok;
        @autoreleasepool {
            if (!owner->pending.empty()) [owner->pending.back().buffer waitUntilCompleted];
        }
        return owner->poll();
    }
    Result<std::uint64_t> completedValue() override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        return owner->completed;
    }
};
struct MetalDevice final : Device {
    std::shared_ptr<State> owner;
    DeviceCaps capabilities;
    MetalQueue commands;
    MetalDevice(std::shared_ptr<State> state, DeviceCaps caps) : owner(std::move(state)), capabilities(std::move(caps)), commands(owner) {}
    ~MetalDevice() override { owner->retire(); }
    const DeviceCaps& caps() const override { return capabilities; }
    Result<void> checkStatus() override { return owner->poll(); }
    Result<ResourceStatistics> resourceStatistics() override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        ResourceStatistics result;
        result.pendingSubmissions = owner->pending.size();
        std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
        std::erase_if(owner->allocations, [](auto& w) { return w.expired(); });
        result.trackedObjects = owner->objects.size();
        for (const auto& weak : owner->allocations) if (auto a = weak.lock()) { ++result.uniqueResources; result.resourceAllocationBytes += a->bytes; }
        for (const auto& p : owner->pending) result.pendingObjectReferences += p.allocations.size();
        return result;
    }
    Result<MemoryBudget> queryMemoryBudget(MemorySegment) override { return owner->unsupported(); }
    Result<std::unique_ptr<Device>> recover() override { return owner->unsupported(); }
    Result<void> removeForTesting() override { return owner->unsupported(); }
    Result<BufferPtr> createBuffer(BufferDesc desc) override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        if (!desc.size || desc.size > NSUIntegerMax || (desc.memory != Memory::Upload && desc.memory != Memory::Readback && desc.memory != Memory::Device))
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
    Result<TexturePtr> createTexture(TextureDesc) override { return owner->unsupported(); }
    Result<PipelinePtr> createGraphicsPipeline(const GraphicsPipelineDesc&) override { return owner->unsupported(); }
    Result<std::unique_ptr<CommandList>> createCommandList() override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        return std::make_unique<MetalCommands>(owner);
    }
    Result<void> checkSurfaceSupport(Surface) override { return owner->unsupported(); }
    Result<std::unique_ptr<SwapChain>> createSwapChain(Surface, SwapChainDesc) override { return owner->unsupported(); }
    CommandQueue& queue() override { return commands; }
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
        id<MTLCommandQueue> queue = [selected newCommandQueue];
        if (!queue) return fail(ErrorCode::OutOfMemory, "Metal command queue creation failed");
        auto state = std::make_shared<State>();
        const auto generation = detail::allocateDeviceGeneration();
        if (!generation) return fail(ErrorCode::InvalidState, "Device generation sequence exhausted");
        state->device = selected; state->queue = queue; state->generation = generation;
        DeviceCaps caps;
        caps.backend = Backend::Metal; caps.adapterName = name; caps.adapterId = {Backend::Metal, selected.registryID};
        return std::make_unique<MetalDevice>(std::move(state), std::move(caps));
    }
}
}
