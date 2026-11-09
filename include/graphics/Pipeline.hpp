#pragma once
#include "Shader.hpp"
#include "Bindings.hpp"
#include "RenderPass.hpp"
#include <memory>

namespace gfx {
enum class VertexFormat { Float, Float2, Float3, Float4, UNorm8x4 };
enum class VertexInputRate { Vertex, Instance };
struct VertexBufferLayout {
    std::uint32_t slot{}, stride{};
    VertexInputRate rate{VertexInputRate::Vertex};
    bool operator==(const VertexBufferLayout&) const = default;
};
struct VertexAttribute {
    std::string semantic;
    std::uint32_t semanticIndex{};
    VertexFormat format{VertexFormat::Float3};
    std::uint32_t slot{}, offset{};
    bool operator==(const VertexAttribute&) const = default;
};
// Vulkan/Metal assign location/attribute i to attributes[i]; semantic/index map D3D12.
// Metal stream slot 0..14 maps to native buffer index 16+slot.
struct VertexLayout {
    std::vector<VertexBufferLayout> buffers;
    std::vector<VertexAttribute> attributes;
    bool operator==(const VertexLayout&) const = default;
};
// Portable structural validation; an empty layout preserves SV_VertexID-only shaders.
[[nodiscard]] Result<void> validateVertexLayout(const VertexLayout&);
struct GraphicsPipelineDesc {
    ShaderBinary vertex, fragment;
    Format colorFormat{Format::RGBA8UNorm};
    bool sampledTexture{}, alphaBlend{};
    VertexLayout vertexLayout;
    BindingLayout bindingLayout;
    std::optional<RenderTargetLayout> renderTargets;
    DepthState depthState;
    bool operator==(const GraphicsPipelineDesc&) const = default;
};
class Pipeline { public: virtual ~Pipeline() = default; };
using PipelinePtr = std::shared_ptr<Pipeline>;
struct CachedGraphicsPipeline { PipelinePtr pipeline; bool hit{}; };
}
