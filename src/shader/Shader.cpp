#include <graphics/Shader.hpp>
#include "../backends/vulkan/Spirv.hpp"
#ifdef GFX_HAS_D3D12
#include "D3D12Shader.hpp"
#include <d3dcompiler.h>
#include <cstring>
#endif
#ifdef GFX_HAS_DXC
#include <dxcapi.h>
#include <bit>
#include <limits>
#endif

namespace gfx {
namespace {
bool graphicsStage(ShaderStage stage) {
    return stage == ShaderStage::Vertex || stage == ShaderStage::Fragment;
}

#ifdef GFX_HAS_DXC
struct DxcRuntime {
    DxcCreateInstanceProc create{};
    DWORD error{};
};

const DxcRuntime& dxcRuntime() {
    static const auto runtime = []() -> DxcRuntime {
        std::wstring path(32768, L'\0');
        const DWORD count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!count || count == path.size()) return {nullptr, GetLastError()};
        path.resize(count);
        path.resize(path.find_last_of(L"\\/") + 1);
        path += L"dxcompiler.dll";
        HMODULE module = LoadLibraryExW(path.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) return {nullptr, GetLastError()};
        const auto address = GetProcAddress(module, "DxcCreateInstance");
        if (!address) {
            const DWORD error = GetLastError();
            FreeLibrary(module);
            return {nullptr, error};
        }
        // Reflected COM objects may outlive this call. Keep the DLL loaded for the process.
        return {std::bit_cast<DxcCreateInstanceProc>(address), ERROR_SUCCESS};
    }();
    return runtime;
}

template<class Interface>
Result<Microsoft::WRL::ComPtr<Interface>> createDxc(REFCLSID clsid) {
    const auto& runtime = dxcRuntime();
    if (!runtime.create)
        return std::unexpected(Error{ErrorCode::Unsupported, "DXIL requires the application-local dxcompiler.dll",
            HRESULT_FROM_WIN32(runtime.error)});
    Microsoft::WRL::ComPtr<Interface> instance;
    const HRESULT result = runtime.create(clsid, __uuidof(Interface), reinterpret_cast<void**>(instance.GetAddressOf()));
    if (FAILED(result)) return std::unexpected(Error{ErrorCode::BackendFailure, "DXC interface creation failed", result});
    return instance;
}
#endif
}

Result<ShaderBinary> compileHlsl(std::string_view source, std::string_view entry, ShaderStage stage) {
    if (source.empty() || entry.empty() || entry.find('\0') != std::string_view::npos || !graphicsStage(stage))
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

Result<ShaderBinary> compileHlslDxil(std::string_view source, std::string_view entry, ShaderStage stage) {
    if (source.empty() || entry.empty() || source.find('\0') != std::string_view::npos ||
        entry.find('\0') != std::string_view::npos || !graphicsStage(stage))
        return std::unexpected(Error{ErrorCode::InvalidArgument, "Invalid DXIL HLSL input"});
#ifdef GFX_HAS_DXC
    if (entry.size() > std::numeric_limits<int>::max())
        return std::unexpected(Error{ErrorCode::InvalidArgument, "DXIL entry point is too long"});
    const auto entrySize = static_cast<int>(entry.size());
    const int wideSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, entry.data(), entrySize, nullptr, 0);
    if (!wideSize) return std::unexpected(Error{ErrorCode::InvalidArgument, "DXIL entry point must be UTF-8"});
    std::wstring name(wideSize, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, entry.data(), entrySize, name.data(), wideSize);
    auto compiler = createDxc<IDxcCompiler3>(CLSID_DxcCompiler);
    if (!compiler) return std::unexpected(compiler.error());
    const DxcBuffer input{source.data(), source.size(), DXC_CP_UTF8};
    const wchar_t* arguments[]{L"GraphicsSDK.hlsl", L"-E", name.c_str(), L"-T",
        stage == ShaderStage::Vertex ? L"vs_6_0" : L"ps_6_0", L"-HV", L"2021", L"-O3", L"-WX", L"-Ges"};
    Microsoft::WRL::ComPtr<IDxcResult> compiled;
    HRESULT result = (*compiler)->Compile(&input, arguments, static_cast<UINT32>(std::size(arguments)), nullptr,
        IID_PPV_ARGS(&compiled));
    if (SUCCEEDED(result)) {
        HRESULT status{};
        result = compiled->GetStatus(&status);
        if (SUCCEEDED(result)) result = status;
    }
    if (FAILED(result)) {
        Microsoft::WRL::ComPtr<IDxcBlobUtf8> errors;
        if (compiled) compiled->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
        return std::unexpected(Error{ErrorCode::BackendFailure,
            errors && errors->GetStringLength() ? std::string(errors->GetStringPointer(), errors->GetStringLength())
                                                 : "DXIL compilation failed", result});
    }
    Microsoft::WRL::ComPtr<IDxcBlob> binary;
    result = compiled->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&binary), nullptr);
    if (FAILED(result) || !binary || !binary->GetBufferSize())
        return std::unexpected(Error{ErrorCode::BackendFailure, "DXC produced no DXIL object", result});
    ShaderBinary output{stage, std::vector<std::byte>(binary->GetBufferSize()), ShaderFormat::DXIL};
    std::memcpy(output.bytes.data(), binary->GetBufferPointer(), output.bytes.size());
    return output;
#else
    return std::unexpected(Error{ErrorCode::Unsupported, "DXIL compiler support is not enabled"});
#endif
}

#ifdef GFX_HAS_D3D12
Result<D3D12ShaderReflection> reflectD3D12Shader(const ShaderBinary& shader) {
    D3D12ShaderReflection reflection;
    HRESULT result{};
    if (shader.format == ShaderFormat::DXBC) {
        result = D3DReflect(shader.bytes.data(), shader.bytes.size(), IID_PPV_ARGS(&reflection.native));
    } else if (shader.format == ShaderFormat::DXIL) {
#ifdef GFX_HAS_DXC
        auto utils = createDxc<IDxcUtils>(CLSID_DxcUtils);
        if (!utils) return std::unexpected(utils.error());
        const DxcBuffer input{shader.bytes.data(), shader.bytes.size(), 0};
        result = (*utils)->CreateReflection(&input, IID_PPV_ARGS(&reflection.native));
#else
        return std::unexpected(Error{ErrorCode::Unsupported, "DXIL reflection support is not enabled"});
#endif
    } else {
        return std::unexpected(Error{ErrorCode::Unsupported, "D3D12 reflection requires DXBC or DXIL"});
    }
    if (FAILED(result)) return std::unexpected(Error{ErrorCode::BackendFailure, "Shader reflection failed", result});
    result = reflection.native->GetDesc(&reflection.description);
    if (FAILED(result)) return std::unexpected(Error{ErrorCode::BackendFailure, "Shader reflection description failed", result});
    const auto version = reflection.description.Version;
    const auto expected = shader.stage == ShaderStage::Vertex ? D3D12_SHVER_VERTEX_SHADER : D3D12_SHVER_PIXEL_SHADER;
    if (static_cast<D3D12_SHADER_VERSION_TYPE>(D3D12_SHVER_GET_TYPE(version)) != expected)
        return std::unexpected(Error{ErrorCode::InvalidArgument, "Shader bytecode stage does not match its declared stage"});
    if (shader.format == ShaderFormat::DXIL && (D3D12_SHVER_GET_MAJOR(version) != 6 || D3D12_SHVER_GET_MINOR(version) != 0))
        return std::unexpected(Error{ErrorCode::Unsupported, "DXIL reflection requires Shader Model 6.0"});
    return reflection;
}

Result<std::vector<ShaderBinding>> d3d12ShaderBindings(const D3D12ShaderReflection& reflection) {
    std::vector<ShaderBinding> output;
    for (UINT index = 0; index < reflection.description.BoundResources; ++index) {
        D3D12_SHADER_INPUT_BIND_DESC binding{};
        HRESULT result = reflection.native->GetResourceBindingDesc(index, &binding);
        if (FAILED(result)) return std::unexpected(Error{ErrorCode::BackendFailure, "Shader binding reflection failed", result});
        ShaderBinding entry{binding.Name,binding.BindPoint,binding.BindCount,binding.Space};
        if(binding.Type==D3D_SIT_TEXTURE && binding.Dimension==D3D_SRV_DIMENSION_TEXTURE2D && binding.ReturnType==D3D_RETURN_TYPE_FLOAT)entry.type=ShaderResourceType::Texture2D;
        if(binding.Type==D3D_SIT_SAMPLER && !(binding.uFlags&D3D_SIF_COMPARISON_SAMPLER))entry.type=ShaderResourceType::Sampler;
        if(binding.Type==D3D_SIT_CBUFFER) {
            D3D12_SHADER_BUFFER_DESC buffer{};
            result=reflection.native->GetConstantBufferByName(binding.Name)->GetDesc(&buffer);
            if(FAILED(result))return std::unexpected(Error{ErrorCode::BackendFailure,"Constant buffer reflection failed",result});
            entry.type=ShaderResourceType::ConstantBuffer;entry.minimumBytes=buffer.Size;
        }
        output.push_back(std::move(entry));
    }
    return output;
}
#endif

Result<std::vector<ShaderBinding>> reflectShader(const ShaderBinary& shader) {
    if (shader.bytes.empty() || !graphicsStage(shader.stage))
        return std::unexpected(Error{ErrorCode::InvalidArgument, "Invalid shader binary/stage"});
    if (shader.format == ShaderFormat::SPIRV) {
        auto reflection = reflectSpirvInterface(shader);
        if (!reflection) return std::unexpected(reflection.error());
        return std::move(reflection->bindings);
    }
#ifdef GFX_HAS_D3D12
    auto reflection = reflectD3D12Shader(shader);
    if (!reflection) return std::unexpected(reflection.error());
    return d3d12ShaderBindings(*reflection);
#else
    return std::unexpected(Error{ErrorCode::Unsupported, "Shader reflection is not compiled"});
#endif
}

Result<SpirvShaderInterface> reflectSpirvInterface(const ShaderBinary& shader) {
    auto parsed = vulkan::parseSpirv(shader, shader.stage);
    if (!parsed) return std::unexpected(parsed.error());
    return std::move(parsed->reflection);
}
}
