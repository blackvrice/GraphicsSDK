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
    [[nodiscard]] virtual Result<void> setScissor(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height) = 0;
    [[nodiscard]] virtual Result<void> draw(std::uint32_t vertices, std::uint32_t instances = 1) = 0;
    [[nodiscard]] virtual Result<void> writeTimestamp(std::uint32_t index) = 0;
    [[nodiscard]] virtual Result<void> resolveTimestamps() = 0;
    [[nodiscard]] virtual Result<double> elapsedGpuMilliseconds() = 0;
    [[nodiscard]] virtual Result<void> debugMarker(std::string_view text) = 0;
};
}
