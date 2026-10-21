#pragma once
#include "Pipeline.hpp"
#include "Resources.hpp"

namespace gfx {
class CommandList {
public:
    virtual ~CommandList() = default;
    [[nodiscard]] virtual Result<void> begin() = 0;
    [[nodiscard]] virtual Result<void> end() = 0;
    [[nodiscard]] virtual Result<void> transition(const BufferPtr&, ResourceState) = 0;
    [[nodiscard]] virtual Result<void> transition(const TexturePtr&, ResourceState) = 0;
    [[nodiscard]] virtual Result<void> copyBuffer(const BufferPtr& source, const BufferPtr& destination, std::uint64_t bytes) = 0;
    [[nodiscard]] virtual Result<void> copyBufferToTexture(const BufferPtr&, const TexturePtr&) = 0;
    [[nodiscard]] virtual Result<void> copyTextureToBuffer(const TexturePtr&, const BufferPtr&) = 0;
    [[nodiscard]] virtual Result<void> beginRenderPass(const TexturePtr&, Color clear) = 0;
    [[nodiscard]] virtual Result<void> endRenderPass() = 0;
    [[nodiscard]] virtual Result<void> setPipeline(const PipelinePtr&) = 0;
    // A sampled pipeline declares a single fragment texture t0, space0, sampler s0.
    [[nodiscard]] virtual Result<void> bindTexture(const TexturePtr&) = 0;
    // size=0 means the remaining bytes. Layout stride must match at draw time.
    [[nodiscard]] virtual Result<void> bindVertexBuffer(std::uint32_t slot, const BufferPtr&, std::uint32_t stride, std::uint64_t offset=0, std::uint64_t size=0) = 0;
    [[nodiscard]] virtual Result<void> bindIndexBuffer(const BufferPtr&, IndexFormat, std::uint64_t offset=0, std::uint64_t size=0) = 0;
    [[nodiscard]] virtual Result<void> setScissor(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height) = 0;
    [[nodiscard]] virtual Result<void> draw(std::uint32_t vertices, std::uint32_t instances=1, std::uint32_t firstVertex=0, std::uint32_t firstInstance=0) = 0;
    // Caller guarantees that index values + baseVertex address valid vertex data.
    [[nodiscard]] virtual Result<void> drawIndexed(std::uint32_t indices, std::uint32_t instances=1, std::uint32_t firstIndex=0, std::int32_t baseVertex=0, std::uint32_t firstInstance=0) = 0;
    [[nodiscard]] virtual Result<void> writeTimestamp(std::uint32_t index) = 0;
    [[nodiscard]] virtual Result<void> resolveTimestamps() = 0;
    [[nodiscard]] virtual Result<double> elapsedGpuMilliseconds() = 0;
    [[nodiscard]] virtual Result<void> debugMarker(std::string_view text) = 0;
};
}
