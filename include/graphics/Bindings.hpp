#pragma once
#include "Resources.hpp"
#include <vector>

namespace gfx {
enum class BindingType { Texture2D, ConstantBuffer };
enum class ShaderVisibility { Vertex, Fragment, All };
enum class SamplerFilter { Point, Linear };
enum class SamplerAddress { Clamp, Repeat };
struct BindingLayoutEntry {
    BindingType type{BindingType::Texture2D};
    std::uint32_t slot{}, count{1}, space{};
    ShaderVisibility visibility{ShaderVisibility::Fragment};
};
struct StaticSampler {
    std::uint32_t slot{}, space{};
    ShaderVisibility visibility{ShaderVisibility::Fragment};
    SamplerFilter filter{SamplerFilter::Linear};
    SamplerAddress address{SamplerAddress::Clamp};
};
struct BindingLayout { std::vector<BindingLayoutEntry> entries; std::vector<StaticSampler> samplers; };
[[nodiscard]] Result<void> validateBindingLayout(const BindingLayout&);
// binding indexes BindingLayout::entries; element indexes that entry's array.
struct TextureBinding { std::uint32_t binding{}, element{}; TexturePtr texture; };
struct ConstantBufferBinding {
    std::uint32_t binding{}, element{}; BufferPtr buffer;
    std::uint64_t offset{}, size{}; // Both multiples of 256; size is 256..65536.
};
struct ResourceBindings { std::vector<TextureBinding> textures; std::vector<ConstantBufferBinding> constants; };
}
