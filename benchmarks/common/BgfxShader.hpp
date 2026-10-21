#pragma once
#include "Adapter.hpp"
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace bench {
// Pinned bgfx shader container v13. The enclosed common DXBC is byte-for-byte unchanged.
// This intentionally handles only the benchmark's resource-free, vertex-ID shaders.
inline std::vector<std::byte> bgfxShaderContainer(std::span<const std::byte> dxbc,bool fragment) {
    if (dxbc.size()<4 || dxbc.size()>std::numeric_limits<std::uint32_t>::max() || std::memcmp(dxbc.data(),"DXBC",4))
        throw std::runtime_error("bgfx wrapper requires DXBC");
    Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
    if (FAILED(D3DReflect(dxbc.data(),dxbc.size(),IID_PPV_ARGS(&reflection)))) throw std::runtime_error("bgfx DXBC reflection failed");
    D3D11_SHADER_DESC desc{}; reflection->GetDesc(&desc);
    const auto stage=static_cast<D3D11_SHADER_VERSION_TYPE>(D3D11_SHVER_GET_TYPE(desc.Version));
    if (desc.BoundResources || desc.ConstantBuffers || stage!=(fragment?D3D11_SHVER_PIXEL_SHADER:D3D11_SHVER_VERTEX_SHADER))
        throw std::runtime_error("bgfx wrapper supports only the resource-free common VS/PS");
    for (UINT i=0;i<desc.InputParameters;++i) {
        D3D11_SIGNATURE_PARAMETER_DESC input{}; reflection->GetInputParameterDesc(i,&input);
        if (fragment || input.SystemValueType!=D3D_NAME_VERTEX_ID) throw std::runtime_error("bgfx wrapper does not support user varyings or vertex attributes");
    }
    for (UINT i=0;i<desc.OutputParameters;++i) {
        D3D11_SIGNATURE_PARAMETER_DESC output{}; reflection->GetOutputParameterDesc(i,&output);
        if (output.SystemValueType!=(fragment?D3D_NAME_TARGET:D3D_NAME_POSITION)) throw std::runtime_error("bgfx wrapper does not support user outputs");
    }
    std::vector<std::byte> bytes;
    auto integer=[&](std::uint32_t value,unsigned count) { for (unsigned i=0;i<count;++i) bytes.push_back(static_cast<std::byte>((value>>(8*i))&255)); };
    integer(static_cast<std::uint32_t>(fragment?'F':'V')|('S'<<8)|('H'<<16)|(13u<<24),4);
    integer(0,4); integer(0,4); // input/output interface hashes: no user varyings
    integer(0,4); integer(0,4); // raw SRV/UAV masks
    integer(0,2); // uniform count
    integer(static_cast<std::uint32_t>(dxbc.size()),4);
    bytes.insert(bytes.end(),dxbc.begin(),dxbc.end());
    integer(0,1); integer(0,1); integer(0,2); // terminator, attribute count, constant buffer bytes
    return bytes;
}
}
