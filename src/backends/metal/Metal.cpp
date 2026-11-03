// Private Objective-C++ implementation; CMake explicitly selects OBJCXX for this unit.
#include <graphics/Graphics.hpp>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <algorithm>
#include <atomic>
#include <deque>
#include <limits>
#include <thread>

namespace gfx {
namespace {
auto fail(ErrorCode code, std::string text, std::int64_t native = 0) {
    return std::unexpected(Error{code, std::move(text), native});
}
std::atomic<std::uint64_t> nextGeneration{1};
struct Pending {
    std::uint64_t serial;
    id<MTLCommandBuffer> buffer;
};
struct State {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    const std::thread::id thread = std::this_thread::get_id();
    std::uint64_t generation{}, last{}, completed{};
    bool retired{};
    std::optional<Error> failure;
    std::deque<Pending> pending;
    ~State() {
        @autoreleasepool {
            for (auto& p : pending) [p.buffer waitUntilCompleted];
        }
    }
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
    // Later slices implement resources, drawing, presentation and native validation.
    auto unsupported() const {
        if (auto ok = check(); !ok) return std::unexpected(ok.error());
        return fail(ErrorCode::Unsupported, "Not implemented by the Metal submission backend");
    }
};
struct MetalCommands final : CommandList {
    std::shared_ptr<State> owner;
    id<MTLCommandBuffer> buffer;
    bool open{}, ended{};
    std::uint64_t submitted{};
    explicit MetalCommands(std::shared_ptr<State> state) : owner(std::move(state)) {}
    Result<void> begin() override {
        if (auto ok = owner->poll(); !ok) return ok;
        if (open || submitted > owner->completed) return fail(ErrorCode::InvalidState, "Metal command list is open or in flight");
        @autoreleasepool {
            id<MTLCommandBuffer> next = [owner->queue commandBuffer];
            if (!next) return fail(ErrorCode::OutOfMemory, "Metal command buffer creation failed");
            buffer = next;
        }
        open = true; ended = false; submitted = 0;
        return {};
    }
    Result<void> end() override {
        if (auto ok = owner->check(); !ok) return ok;
        if (!open) return fail(ErrorCode::InvalidState, "Metal command list is not recording");
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
            id<MTLBlitCommandEncoder> encoder = [buffer blitCommandEncoder];
            if (!encoder) return fail(ErrorCode::OutOfMemory, "Metal marker encoder creation failed");
            [encoder insertDebugSignpost:label];
            [encoder endEncoding];
        }
        return {};
    }
    Result<void> transition(const BufferPtr&, ResourceState) override { return owner->unsupported(); }
    Result<void> transition(const TexturePtr&, ResourceState) override { return owner->unsupported(); }
    Result<void> copyBuffer(const BufferPtr&, const BufferPtr&, std::uint64_t) override { return owner->unsupported(); }
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
        // Allocate the retention node before commit; retain even if the caller destroys its list.
        owner->pending.push_back({owner->last + 1, commands->buffer});
        @autoreleasepool { [commands->buffer commit]; }
        commands->submitted = ++owner->last;
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
    ~MetalDevice() override { owner->retired = true; }
    const DeviceCaps& caps() const override { return capabilities; }
    Result<void> checkStatus() override { return owner->poll(); }
    Result<ResourceStatistics> resourceStatistics() override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        ResourceStatistics result;
        result.pendingSubmissions = owner->pending.size();
        return result; // No public resource allocations in this slice.
    }
    Result<MemoryBudget> queryMemoryBudget(MemorySegment) override { return owner->unsupported(); }
    Result<std::unique_ptr<Device>> recover() override { return owner->unsupported(); }
    Result<void> removeForTesting() override { return owner->unsupported(); }
    Result<BufferPtr> createBuffer(BufferDesc) override { return owner->unsupported(); }
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
        auto generation = nextGeneration.load(std::memory_order_relaxed);
        do {
            if (generation == UINT64_MAX) return fail(ErrorCode::InvalidState, "Metal generation sequence exhausted");
        } while (!nextGeneration.compare_exchange_weak(generation, generation + 1, std::memory_order_relaxed));
        state->device = selected; state->queue = queue; state->generation = generation;
        DeviceCaps caps;
        caps.backend = Backend::Metal; caps.adapterName = name; caps.adapterId = {Backend::Metal, selected.registryID};
        return std::make_unique<MetalDevice>(std::move(state), std::move(caps));
    }
}
}
