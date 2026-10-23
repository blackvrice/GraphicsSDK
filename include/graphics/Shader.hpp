#pragma once
#include "Types.hpp"
#include <span>
#include <string_view>
#include <vector>

namespace gfx {
enum class ShaderStage { Vertex, Fragment };
enum class ShaderFormat { DXBC, SPIRV };
// SPIRV input must already pass an external validator for the Vulkan environment.
// The backend checks its supported interface, not complete SPIR-V semantics.
struct ShaderBinary { ShaderStage stage{}; std::vector<std::byte> bytes; ShaderFormat format{ShaderFormat::DXBC}; };
enum class ShaderResourceType { Unsupported, Texture2D, ConstantBuffer, Sampler };
struct ShaderBinding { std::string name; std::uint32_t slot{}, count{}, space{}; ShaderResourceType type{}; std::uint32_t minimumBytes{}; };
// Bootstrap compiler: SM5.1 DXBC on Windows; DXC/SPIR-V/MSL is a separate roadmap gate.
[[nodiscard]] Result<ShaderBinary> compileHlsl(std::string_view source, std::string_view entry, ShaderStage stage);
[[nodiscard]] Result<std::vector<ShaderBinding>> reflectShader(const ShaderBinary& shader);
}
