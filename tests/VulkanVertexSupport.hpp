#pragma once
#include <graphics/Graphics.hpp>
#include "fixtures/VertexFixtures.hpp"
#include <cstring>

namespace vertexTest {
inline gfx::ShaderBinary binary(std::span<const std::uint32_t> words, gfx::ShaderStage stage) {
    const auto bytes = std::as_bytes(words); return {stage, {bytes.begin(), bytes.end()}, gfx::ShaderFormat::SPIRV};
}
inline std::span<const std::uint32_t> module(unsigned components) {
    switch (components) { case 1: return vertexFixtures::position1; case 2: return vertexFixtures::position2; case 3: return vertexFixtures::position3; default: return vertexFixtures::position4; }
}
inline gfx::VertexLayout layout(unsigned components, bool normalized) {
    using V = gfx::VertexFormat; const unsigned fields = components == 1 ? 2 : components, colorBytes = normalized ? 4 : 16;
    gfx::VertexLayout l; l.buffers.push_back({2, fields * 4 + colorBytes + 4});
    if (components == 1) l.attributes = {{"X", 0, V::Float, 2, 0}, {"Y", 0, V::Float, 2, 4}};
    else l.attributes.push_back({"POSITION", 0, components == 2 ? V::Float2 : components == 3 ? V::Float3 : V::Float4, 2, 0});
    l.attributes.push_back({"COLOR", 0, normalized ? V::UNorm8x4 : V::Float4, 2, fields * 4}); return l;
}
inline gfx::GraphicsPipelineDesc pipeline(unsigned components, bool normalized, gfx::Format format) {
    gfx::GraphicsPipelineDesc p; p.vertex = binary(module(components), gfx::ShaderStage::Vertex);
    p.fragment = binary(vertexFixtures::color, gfx::ShaderStage::Fragment); p.colorFormat = format; p.vertexLayout = layout(components, normalized); return p;
}
}
