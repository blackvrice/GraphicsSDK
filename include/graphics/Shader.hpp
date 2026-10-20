#pragma once
#include "Types.hpp"
#include <span>
#include <string_view>
#include <vector>

namespace gfx {
enum class ShaderStage { Vertex, Fragment };
struct ShaderBinary { ShaderStage stage{}; std::vector<std::byte> bytes; };
struct ShaderBinding { std::string name; std::uint32_t slot{}, count{}, space{}; };
// Bootstrap compiler: SM5.1 DXBC on Windows; DXC/SPIR-V/MSL is a separate roadmap gate.
[[nodiscard]] Result<ShaderBinary> compileHlsl(std::string_view source, std::string_view entry, ShaderStage stage);
[[nodiscard]] Result<std::vector<ShaderBinding>> reflectShader(const ShaderBinary& shader);
}
