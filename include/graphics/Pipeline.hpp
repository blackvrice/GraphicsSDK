#pragma once
#include "Shader.hpp"
#include "Bindings.hpp"
#include "RenderPass.hpp"
#include <memory>

namespace gfx {
enum class VertexFormat { Float, Float2, Float3, Float4, UNorm8x4 };
enum class VertexInputRate { Vertex, Instance };
struct VertexBufferLayout { std::uint32_t slot{}, stride{}; VertexInputRate rate{VertexInputRate::Vertex}; };
struct VertexAttribute { std::string semantic; std::uint32_t semanticIndex{}; VertexFormat format{VertexFormat::Float3}; std::uint32_t slot{}, offset{}; };
struct VertexLayout { std::vector<VertexBufferLayout> buffers; std::vector<VertexAttribute> attributes; };
// Portable structural validation; an empty layout preserves SV_VertexID-only shaders.
[[nodiscard]] Result<void> validateVertexLayout(const VertexLayout&);
struct GraphicsPipelineDesc { ShaderBinary vertex, fragment; Format colorFormat{Format::RGBA8UNorm}; bool sampledTexture{}; bool alphaBlend{}; VertexLayout vertexLayout; BindingLayout bindingLayout; std::optional<RenderTargetLayout> renderTargets; DepthState depthState; };
class Pipeline { public: virtual ~Pipeline() = default; };
using PipelinePtr = std::shared_ptr<Pipeline>;
}
