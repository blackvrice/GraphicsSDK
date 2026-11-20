#pragma once
#include "Pipeline.hpp"
#include "Resources.hpp"

namespace gfx {
class CommandList {
public:
    virtual ~CommandList() = default;
    [[nodiscard]] virtual Result<void> begin() = 0;
    [[nodiscard]] virtual Result<void> end() = 0;
    // Validation rejection of transitions/copies preserves recorded state and
    // captures no new resource references or observed versions.
    [[nodiscard]] virtual Result<void> transition(const BufferPtr&, ResourceState) = 0;
    [[nodiscard]] virtual Result<void> transition(const TexturePtr&, ResourceState) = 0;
    [[nodiscard]] virtual Result<void> copyBuffer(const BufferPtr& source, const BufferPtr& destination, std::uint64_t bytes) = 0;
    // Offset-aware sub-range buffer copy. Source and destination must be distinct buffers
    // with non-empty in-bounds ranges. Validation rejection preserves recorded state and
    // captures no new resource references or observed versions.
    [[nodiscard]] virtual Result<void> copyBufferRegion(
        const BufferPtr& source, std::uint64_t sourceOffset,
        const BufferPtr& destination, std::uint64_t destinationOffset,
        std::uint64_t bytes) = 0;
    [[nodiscard]] virtual Result<void> copyBufferToTexture(const BufferPtr&, const TexturePtr&) = 0;
    [[nodiscard]] virtual Result<void> copyTextureToBuffer(const TexturePtr&, const BufferPtr&) = 0;
    // Partial 2D buffer<->texture region copies. bufferOffset must be 512-byte aligned and
    // bufferRowPitch (if non-zero) 256-byte aligned. Color formats support arbitrary sub-regions;
    // D32Float rejects partial sub-regions with Unsupported. Validation rejection is atomic.
    [[nodiscard]] virtual Result<void> copyBufferToTextureRegion(
        const BufferPtr& source, const TexturePtr& destination, const TextureCopyRegion& region) = 0;
    [[nodiscard]] virtual Result<void> copyTextureToBufferRegion(
        const TexturePtr& source, const BufferPtr& destination, const TextureCopyRegion& region) = 0;
    [[nodiscard]] virtual Result<void> beginRenderPass(const TexturePtr&, Color clear) = 0;
    [[nodiscard]] virtual Result<void> beginRenderPass(const RenderPassDesc&) = 0;
    [[nodiscard]] virtual Result<void> endRenderPass() = 0;
    [[nodiscard]] virtual Result<void> setPipeline(const PipelinePtr&) = 0;
    // A sampled pipeline declares a single fragment texture t0, space0, sampler s0.
    [[nodiscard]] virtual Result<void> bindTexture(const TexturePtr&) = 0;
    // Complete immutable snapshot for the current pipeline. Failure preserves the previous set.
    [[nodiscard]] virtual Result<void> bindResources(const ResourceBindings&) = 0;
    // size=0 means the remaining bytes. Layout stride must match at draw time.
    [[nodiscard]] virtual Result<void> bindVertexBuffer(std::uint32_t slot, const BufferPtr&, std::uint32_t stride, std::uint64_t offset=0, std::uint64_t size=0) = 0;
    [[nodiscard]] virtual Result<void> bindIndexBuffer(const BufferPtr&, IndexFormat, std::uint64_t offset=0, std::uint64_t size=0) = 0;
    // Sets the active render-pass viewport. Reset to full attachment extent on beginRenderPass.
    // Invalid viewports fail without modifying the previously active viewport.
    [[nodiscard]] virtual Result<void> setViewport(const Viewport& viewport) = 0;
    [[nodiscard]] virtual Result<void> setScissor(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height) = 0;
    [[nodiscard]] virtual Result<void> draw(std::uint32_t vertices, std::uint32_t instances=1, std::uint32_t firstVertex=0, std::uint32_t firstInstance=0) = 0;
    // Caller guarantees that index values + baseVertex address valid vertex data.
    [[nodiscard]] virtual Result<void> drawIndexed(std::uint32_t indices, std::uint32_t instances=1, std::uint32_t firstIndex=0, std::int32_t baseVertex=0, std::uint32_t firstInstance=0) = 0;
    [[nodiscard]] virtual Result<void> writeTimestamp(std::uint32_t index) = 0;
    [[nodiscard]] virtual Result<void> resolveTimestamps() = 0;
    [[nodiscard]] virtual Result<double> elapsedGpuMilliseconds() = 0;
    [[nodiscard]] virtual Result<void> debugMarker(std::string_view text) = 0;
};
class CommandPool {
public:
    virtual ~CommandPool() = default;
    CommandPool(const CommandPool&) = delete;
    CommandPool& operator=(const CommandPool&) = delete;
    // Creating thread only. Allocates a reusable CommandList backed by this pool.
    // Fails with InvalidState while any command list from this pool is currently recording.
    // Within a pool epoch, at most one command list from the same pool may record at a time,
    // and each pooled command list may be recorded once before reset().
    [[nodiscard]] virtual Result<std::unique_ptr<CommandList>> createCommandList() = 0;
    // Creating thread only. Fails with InvalidState while any command list from this pool
    // is recording or while any submission recorded from this pool is still in flight.
    // On success, resets the native command allocator/pool, releases retained per-recording
    // references, discards unsubmitted recordings, and makes all live pooled lists reusable.
    [[nodiscard]] virtual Result<void> reset() = 0;
protected:
    CommandPool() = default;
};
}
