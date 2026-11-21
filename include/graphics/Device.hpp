#pragma once
#include "CommandQueue.hpp"
#include "SwapChain.hpp"
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace gfx {
namespace detail { struct ProfilingTracker; }
struct RGResourceHandle {
    std::uint32_t id{};
    bool operator==(const RGResourceHandle&) const = default;
    explicit operator bool() const noexcept { return id != 0; }
};

struct RGResourceUse {
    RGResourceHandle resource;
    ResourceState state{ResourceState::Common};
};

class RenderGraphResources {
public:
    virtual ~RenderGraphResources() = default;
    [[nodiscard]] virtual Result<BufferPtr> buffer(RGResourceHandle handle) const = 0;
    [[nodiscard]] virtual Result<TexturePtr> texture(RGResourceHandle handle) const = 0;
};

struct RGPassDesc {
    std::string name;
    QueueType queue{QueueType::Graphics};
    bool sideEffect{};
    std::vector<RGResourceUse> reads;
    std::vector<RGResourceUse> writes;
    std::vector<std::uint32_t> dependsOn;
    std::function<Result<void>(CommandList&, const RenderGraphResources&)> execute;
};

struct RGBarrierInfo {
    std::uint32_t passIndex{};
    std::uint32_t executionOrder{};
    RGResourceHandle resource;
    std::uint32_t physicalSlot{};
    ResourceState before{ResourceState::Common};
    ResourceState after{ResourceState::Common};
    bool isUavBarrier{};
    bool afterPass{};
};

struct RGSyncEdge {
    std::uint32_t producerBatch{};
    std::uint32_t consumerBatch{};
    QueueType producerQueue{QueueType::Graphics};
    QueueType consumerQueue{QueueType::Graphics};
    std::uint64_t timelineValue{};
};

struct RGCompileStats {
    std::uint32_t totalPasses{};
    std::uint32_t executedPasses{};
    std::uint32_t culledPasses{};
    std::uint32_t importedResources{};
    std::uint32_t transientResourcesDeclared{};
    std::uint32_t transientResourcesCulled{};
    std::uint32_t physicalTransientSlots{};
    std::uint32_t aliasedResources{};
    std::uint32_t insertedBarriers{};
    std::uint32_t insertedUavBarriers{};
    std::uint32_t queueBatches{};
    std::uint32_t crossQueueSyncs{};
};

class Device {
public:
    Device() noexcept;
    virtual ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    [[nodiscard]] virtual const DeviceCaps& caps() const = 0;
    // Creating thread only, no GPU wait. Queries effective adapter and SDK format support.
    // Unknown format values return InvalidArgument.
    [[nodiscard]] virtual Result<FormatSupport> queryFormatSupport(Format format) = 0;
    [[nodiscard]] virtual Result<void> checkStatus() = 0;
    // Creating thread only. Diagnostic traversal; does not wait for in-flight work.
    [[nodiscard]] virtual Result<ResourceStatistics> resourceStatistics() = 0;
    // Creating thread only, no GPU wait. D3D12 queries node 0 of the actual device's
    // adapter. OS usage includes other owners on that adapter in this process.
    // Unsupported queries return an error, never a synthesized zero snapshot.
    [[nodiscard]] virtual Result<MemoryBudget> queryMemoryBudget(MemorySegment segment) = 0;
    // On loss, retire this generation and return a fresh device with the same selection policy.
    // Old handles keep CPU descriptions, but all their native operations remain DeviceLost.
    [[nodiscard]] virtual Result<std::unique_ptr<Device>> recover() = 0;
    // Native device removal, only when explicitly enabled in DeviceDesc. Never a driver TDR.
    [[nodiscard]] virtual Result<void> removeForTesting() = 0;
    [[nodiscard]] virtual Result<BufferPtr> createBuffer(BufferDesc) = 0;
    [[nodiscard]] virtual Result<TexturePtr> createTexture(TextureDesc) = 0;
    // Creating thread only. Creates a validated subresource view over a texture from this device.
    [[nodiscard]] virtual Result<TextureViewPtr> createTextureView(const TexturePtr&, TextureViewDesc = {}) = 0;
    // Creating thread only. Queries native size and alignment for placed buffer/texture allocation on a MemoryHeap.
    [[nodiscard]] virtual Result<ResourceMemoryRequirements> queryBufferMemoryRequirements(BufferDesc desc) = 0;
    [[nodiscard]] virtual Result<ResourceMemoryRequirements> queryTextureMemoryRequirements(TextureDesc desc) = 0;
    // Creating thread only. Allocates an explicit device-local MemoryHeap for placed/aliased resources.
    // Default createBuffer()/createTexture() retain dedicated-per-resource allocation unchanged.
    [[nodiscard]] virtual Result<MemoryHeapPtr> createMemoryHeap(MemoryHeapDesc desc) = 0;
    [[nodiscard]] virtual Result<BufferPtr> createPlacedBuffer(const MemoryHeapPtr& heap, std::uint64_t heapOffset, BufferDesc desc) = 0;
    [[nodiscard]] virtual Result<TexturePtr> createPlacedTexture(const MemoryHeapPtr& heap, std::uint64_t heapOffset, TextureDesc desc) = 0;
    // Creating thread only. Queries native sparse/tiled 2D texture tile geometry and packed mip tail properties.
    [[nodiscard]] virtual Result<SparseTextureProperties> querySparseTextureProperties(TextureDesc desc) = 0;
    [[nodiscard]] virtual Result<PipelinePtr> createGraphicsPipeline(const GraphicsPipelineDesc&) = 0;
    [[nodiscard]] virtual Result<PipelinePtr> createComputePipeline(const ComputePipelineDesc&) = 0;
    // Capability-gated Mesh/Task shader pipeline entry point. Validates descriptor and returns
    // ErrorCode::Unsupported when hardware or SDK mesh shader pipeline support is unavailable.
    [[nodiscard]] virtual Result<PipelinePtr> createMeshPipeline(const MeshPipelineDesc&) = 0;
    // Creating thread only. Exact descriptor match within this device generation.
    // The cache retains up to eight pipelines. Oversized keys are created uncached.
    [[nodiscard]] Result<CachedGraphicsPipeline> createCachedGraphicsPipeline(const GraphicsPipelineDesc&);
    // Drops cache ownership. Existing handles and recorded work remain valid.
    [[nodiscard]] Result<void> clearGraphicsPipelineCache();
    [[nodiscard]] virtual Result<std::unique_ptr<CommandList>> createCommandList() = 0;
    [[nodiscard]] virtual Result<std::unique_ptr<CommandList>> createCommandList(QueueType type) = 0;
    // Creating thread only. Creates an explicit command allocator/pool for reusable CommandLists.
    [[nodiscard]] virtual Result<std::unique_ptr<CommandPool>> createCommandPool() = 0;
    [[nodiscard]] virtual Result<std::unique_ptr<CommandPool>> createCommandPool(QueueType type) = 0;
    // Creating thread only. Creates a 64-bit GPU/CPU timeline fence for cross-queue synchronization.
    [[nodiscard]] virtual Result<TimelineFencePtr> createTimelineFence(std::uint64_t initialValue = 0) = 0;
    // Creating thread only. Creates a fence-reclaimed segmented UploadRing on this device.
    [[nodiscard]] Result<std::unique_ptr<UploadRing>> createUploadRing(UploadRingDesc desc = {});
    // Creating thread only. Creates a fence-reclaimed ReadbackPool on this device.
    [[nodiscard]] Result<std::unique_ptr<ReadbackPool>> createReadbackPool(ReadbackPoolDesc desc = {});
    // Creating thread only. Creates a fence-reclaimed TransientResourcePool with VRAM budget pressure policy.
    [[nodiscard]] Result<std::unique_ptr<TransientResourcePool>> createTransientResourcePool(TransientPoolDesc desc = {});
    // Creating thread only. Validate a live native surface and selected queue;
    // does not create a swapchain or guarantee format/present support.
    [[nodiscard]] virtual Result<void> checkSurfaceSupport(Surface) = 0;
    // Queries supported present modes, frame-latency pacing support, and image count limits for a surface.
    [[nodiscard]] virtual Result<SurfacePresentCapabilities> querySurfacePresentCapabilities(Surface) = 0;
    // Queries supported swapchain formats, color spaces, 10-bit/HDR output, and display HDR state.
    [[nodiscard]] virtual Result<SurfaceColorCapabilities> querySurfaceColorCapabilities(Surface) = 0;
    // Queries live native surface client extent, DPI scale, and minimized state.
    [[nodiscard]] virtual Result<SurfaceMetrics> querySurfaceMetrics(Surface) = 0;
    [[nodiscard]] virtual Result<std::unique_ptr<SwapChain>> createSwapChain(Surface, SwapChainDesc) = 0;
    // Presents multiple swapchains in deterministic order; rejects null or duplicate pointers atomically.
    // When skipMinimized is true, minimized (0x0) swapchains are skipped cleanly without failing.
    [[nodiscard]] Result<MultiPresentResult> presentSwapChains(std::span<SwapChain* const> swapChains, bool skipMinimized = true);
    [[nodiscard]] virtual CommandQueue& queue() = 0;
    [[nodiscard]] virtual Result<CommandQueue*> queue(QueueType type) = 0;
    // Cumulative ERROR/CORRUPTION count since callback registration, independent of
    // native text storage limits/clearing. D3D12 validation requires InfoQueue1;
    // Vulkan requires Khronos validation/debug utils, synchronization features and layer settings.
    // Vulkan counts delivered ERROR callbacks; external layer settings can filter/limit them.
    [[nodiscard]] virtual Result<std::uint64_t> validationErrorCount() = 0;
    // Creating thread only. Enables or disables device-level profiling counters, stall timing,
    // and GPU scope aggregation. Disabled by default so uninstrumented paths stay overhead-free.
    [[nodiscard]] Result<void> setProfilingEnabled(bool enabled);
    [[nodiscard]] bool isProfilingEnabled() const noexcept;
    // Creating thread only. Returns current profiling snapshot. When disabled, enabled=false and
    // unmeasured/optional fields remain std::nullopt.
    [[nodiscard]] Result<ProfilingSnapshot> profilingSnapshot();
    // Creating thread only. Resets accumulated counters, stall timing, and recorded GPU scopes.
    [[nodiscard]] Result<void> resetProfilingStatistics();
protected:
    [[nodiscard]] const std::shared_ptr<detail::ProfilingTracker>& profilingTracker() const noexcept { return profiling; }
private:
    friend class RenderGraph;
    struct GraphicsCache;
    std::unique_ptr<GraphicsCache> graphicsCache;
    std::shared_ptr<bool> lifetimeGuard;
    std::shared_ptr<detail::ProfilingTracker> profiling;
};

class RenderGraph {
public:
    RenderGraph();
    ~RenderGraph();
    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;
    RenderGraph(RenderGraph&&) noexcept;
    RenderGraph& operator=(RenderGraph&&) noexcept;

    [[nodiscard]] Result<RGResourceHandle> createBuffer(std::string name, BufferDesc desc);
    [[nodiscard]] Result<RGResourceHandle> createTexture(std::string name, TextureDesc desc);
    [[nodiscard]] Result<RGResourceHandle> importBuffer(
        std::string name,
        BufferPtr buffer,
        std::optional<ResourceState> initialState = std::nullopt,
        std::optional<ResourceState> finalState = std::nullopt);
    [[nodiscard]] Result<RGResourceHandle> importTexture(
        std::string name,
        TexturePtr texture,
        std::optional<ResourceState> initialState = std::nullopt,
        std::optional<ResourceState> finalState = std::nullopt);
    [[nodiscard]] Result<std::uint32_t> addPass(RGPassDesc desc);

    // Compiles DAG dependencies, culls dead passes/transients, computes transient aliasing slots,
    // plans automatic state transitions/UAV barriers, and groups multi-queue timeline batches.
    [[nodiscard]] Result<RGCompileStats> compile();

    // Executes the compiled graph on the device's Graphics/Compute/Copy queues.
    // When transientPool is non-null, physical transient slots are acquired from and recycled back to it.
    [[nodiscard]] Result<void> execute(Device& device, TransientResourcePool* transientPool = nullptr);

    // Clears declared passes and resources for the next frame while preserving cached command pools.
    void reset();
    // Releases cached command pools and timeline fence held for repeated execute() calls.
    void releaseGpuResources();

    [[nodiscard]] bool isCompiled() const noexcept;
    [[nodiscard]] const RGCompileStats& stats() const noexcept;
    [[nodiscard]] const std::vector<RGBarrierInfo>& barriers() const noexcept;
    [[nodiscard]] const std::vector<RGSyncEdge>& syncEdges() const noexcept;
    [[nodiscard]] std::string dumpText() const;
    [[nodiscard]] std::string dumpDot() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

[[nodiscard]] Result<std::unique_ptr<Device>> createDevice(DeviceDesc desc = {});
}
