#pragma once
#include <graphics/Graphics.hpp>
#include "fixtures/DepthFixtures.hpp"
#include <cstring>

namespace depthTest {
inline gfx::ShaderBinary binary(std::span<const std::uint32_t> words, gfx::ShaderStage stage) {
    const auto bytes = std::as_bytes(words); return {stage, {bytes.begin(), bytes.end()}, gfx::ShaderFormat::SPIRV};
}
inline gfx::GraphicsPipelineDesc pipeline(gfx::RenderTargetLayout targets, gfx::DepthState state = {}) {
    gfx::GraphicsPipelineDesc p; p.vertex = binary(depthFixtures::vertex, gfx::ShaderStage::Vertex);
    if (!targets.colors.empty()) p.fragment = binary(targets.colors.size() == 8 ? std::span<const std::uint32_t>(depthFixtures::mrt8) : targets.colors.size() == 2 ? std::span<const std::uint32_t>(depthFixtures::mrt2) : depthFixtures::color, gfx::ShaderStage::Fragment);
    p.renderTargets = std::move(targets); p.depthState = state;
    p.bindingLayout = {{{gfx::BindingType::ConstantBuffer, 0, 1, 0, gfx::ShaderVisibility::All}}, {}}; return p;
}
}
