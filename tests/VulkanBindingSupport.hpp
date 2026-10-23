#pragma once
#include <graphics/Graphics.hpp>
#include "fixtures/BindingFixtures.hpp"
#include <cstring>

namespace bindingTest {
inline gfx::ShaderBinary binary(std::span<const std::uint32_t> words, gfx::ShaderStage stage) {
    const auto bytes = std::as_bytes(words); return {stage, {bytes.begin(), bytes.end()}, gfx::ShaderFormat::SPIRV};
}
inline gfx::GraphicsPipelineDesc pipeline(bool arrays = false, bool large = false, gfx::Format format = gfx::Format::RGBA8UNorm) {
    gfx::GraphicsPipelineDesc p;
    p.vertex = binary(arrays ? std::span<const std::uint32_t>(bindingFixtures::transform) : bindingFixtures::uv, gfx::ShaderStage::Vertex);
    p.fragment = binary(arrays ? (large ? std::span<const std::uint32_t>(bindingFixtures::large_block) : bindingFixtures::arrays) : bindingFixtures::sample, gfx::ShaderStage::Fragment);
    p.colorFormat = format;
    if (arrays) p.bindingLayout = {{{gfx::BindingType::Texture2D, 4, 2, 7, gfx::ShaderVisibility::Fragment},
        {gfx::BindingType::ConstantBuffer, 6, 2, 9, gfx::ShaderVisibility::Fragment},
        {gfx::BindingType::ConstantBuffer, 6, 1, 9, gfx::ShaderVisibility::Vertex}},
        {{3, 5, gfx::ShaderVisibility::Fragment, gfx::SamplerFilter::Point, gfx::SamplerAddress::Clamp},
         {4, 5, gfx::ShaderVisibility::Fragment, gfx::SamplerFilter::Linear, gfx::SamplerAddress::Repeat}}};
    else p.sampledTexture = true;
    return p;
}
}
