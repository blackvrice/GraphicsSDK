#include <graphics/Shader.hpp>
#ifdef GFX_HAS_D3D12
#include <d3dcompiler.h>
#include <d3d12shader.h>
#include <wrl/client.h>
#include <cstring>
#endif

namespace gfx {
Result<ShaderBinary> compileHlsl(std::string_view source, std::string_view entry, ShaderStage stage) {
    if (source.empty() || entry.empty() || entry.find('\0') != std::string_view::npos ||
        (stage != ShaderStage::Vertex && stage != ShaderStage::Fragment))
        return std::unexpected(Error{ErrorCode::InvalidArgument, "Invalid HLSL input"});
#ifdef GFX_HAS_D3D12
    Microsoft::WRL::ComPtr<ID3DBlob> binary, errors;
    const std::string name{entry};
    HRESULT result = D3DCompile(source.data(), source.size(), "GraphicsSDK.hlsl", nullptr, nullptr, name.c_str(),
        stage == ShaderStage::Vertex ? "vs_5_1" : "ps_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &binary, &errors);
    if (FAILED(result)) return std::unexpected(Error{ErrorCode::BackendFailure,
        errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : "HLSL compilation failed", result});
    ShaderBinary output{stage, std::vector<std::byte>(binary->GetBufferSize())};
    std::memcpy(output.bytes.data(), binary->GetBufferPointer(), binary->GetBufferSize());
    return output;
#else
    return std::unexpected(Error{ErrorCode::Unsupported, "HLSL bootstrap compiler requires D3D12 build"});
#endif
}
Result<std::vector<ShaderBinding>> reflectShader(const ShaderBinary& shader) {
    if (shader.bytes.empty() || (shader.stage!=ShaderStage::Vertex && shader.stage!=ShaderStage::Fragment)) return std::unexpected(Error{ErrorCode::InvalidArgument, "Invalid shader binary/stage"});
    if (shader.format != ShaderFormat::DXBC) return std::unexpected(Error{ErrorCode::Unsupported, "Public reflection currently supports DXBC only"});
#ifdef GFX_HAS_D3D12
    Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
    HRESULT result = D3DReflect(shader.bytes.data(), shader.bytes.size(), IID_PPV_ARGS(&reflection));
    if (FAILED(result)) return std::unexpected(Error{ErrorCode::BackendFailure, "DXBC reflection failed", result});
    D3D12_SHADER_DESC description{};
    result = reflection->GetDesc(&description);
    if (FAILED(result)) return std::unexpected(Error{ErrorCode::BackendFailure, "Shader reflection description failed", result});
    const auto type=static_cast<D3D12_SHADER_VERSION_TYPE>(D3D12_SHVER_GET_TYPE(description.Version));
    if(type!=(shader.stage==ShaderStage::Vertex?D3D12_SHVER_VERTEX_SHADER:D3D12_SHVER_PIXEL_SHADER))
        return std::unexpected(Error{ErrorCode::InvalidArgument,"Shader bytecode stage does not match its declared stage"});
    std::vector<ShaderBinding> output;
    for (UINT index = 0; index < description.BoundResources; ++index) {
        D3D12_SHADER_INPUT_BIND_DESC binding{};
        result = reflection->GetResourceBindingDesc(index, &binding);
        if (FAILED(result)) return std::unexpected(Error{ErrorCode::BackendFailure, "Shader binding reflection failed", result});
        ShaderBinding entry{binding.Name,binding.BindPoint,binding.BindCount,binding.Space};
        if(binding.Type==D3D_SIT_TEXTURE && binding.Dimension==D3D_SRV_DIMENSION_TEXTURE2D && binding.ReturnType==D3D_RETURN_TYPE_FLOAT)entry.type=ShaderResourceType::Texture2D;
        if(binding.Type==D3D_SIT_SAMPLER && !(binding.uFlags&D3D_SIF_COMPARISON_SAMPLER))entry.type=ShaderResourceType::Sampler;
        if(binding.Type==D3D_SIT_CBUFFER) {
            D3D12_SHADER_BUFFER_DESC buffer{};
            result=reflection->GetConstantBufferByName(binding.Name)->GetDesc(&buffer);
            if(FAILED(result))return std::unexpected(Error{ErrorCode::BackendFailure,"Constant buffer reflection failed",result});
            entry.type=ShaderResourceType::ConstantBuffer;entry.minimumBytes=buffer.Size;
        }
        output.push_back(std::move(entry));
    }
    return output;
#else
    return std::unexpected(Error{ErrorCode::Unsupported, "Shader reflection is not compiled"});
#endif
}
}
