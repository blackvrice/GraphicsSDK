#pragma once

#include <graphics/Shader.hpp>
#include <d3d12shader.h>
#include <wrl/client.h>

namespace gfx {
struct D3D12ShaderReflection {
    Microsoft::WRL::ComPtr<ID3D12ShaderReflection> native;
    D3D12_SHADER_DESC description{};
};

Result<D3D12ShaderReflection> reflectD3D12Shader(const ShaderBinary& shader);
Result<std::vector<ShaderBinding>> d3d12ShaderBindings(const D3D12ShaderReflection& reflection);
}
