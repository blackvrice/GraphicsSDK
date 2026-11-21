#include <graphics/Graphics.hpp>
#include "fixtures/BindingFixtures.hpp"
#include "fixtures/SpirvFixtures.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
struct Totals {
    std::uint64_t assertions{};
    std::uint64_t rejections{};
    std::uint64_t subresourcesVerified{};
    std::uint64_t sampledPassesVerified{};
};

void check(bool condition, const char* message, Totals& totals) {
    if (!condition) throw std::runtime_error(message);
    ++totals.assertions;
}

template <class T>
T take(gfx::Result<T>&& value, const char* message, Totals& totals) {
    if (!value) {
        throw std::runtime_error(std::string(message) + ": " + value.error().message +
            " (code=" + std::to_string(static_cast<int>(value.error().code)) +
            ", native=" + std::to_string(value.error().nativeCode) + ")");
    }
    ++totals.assertions;
    return std::move(*value);
}

void take(gfx::Result<void>&& value, const char* message, Totals& totals) {
    if (!value) {
        throw std::runtime_error(std::string(message) + ": " + value.error().message +
            " (code=" + std::to_string(static_cast<int>(value.error().code)) +
            ", native=" + std::to_string(value.error().nativeCode) + ")");
    }
    ++totals.assertions;
}

template <class T>
void reject(const gfx::Result<T>& value, gfx::ErrorCode expected, const char* message, Totals& totals) {
    if (value) throw std::runtime_error(std::string("Unexpected success: ") + message);
    if (value.error().code != expected) {
        throw std::runtime_error(std::string(message) + " (expected code=" +
            std::to_string(static_cast<int>(expected)) + ", got code=" +
            std::to_string(static_cast<int>(value.error().code)) + ": " + value.error().message + ")");
    }
    ++totals.assertions;
    ++totals.rejections;
}

using RGBA = std::array<std::uint8_t, 4>;

gfx::ShaderBinary spirvBinary(gfx::ShaderStage stage, std::span<const std::uint32_t> words) {
    const auto raw = std::as_bytes(words);
    return {stage, std::vector<std::byte>(raw.begin(), raw.end()), gfx::ShaderFormat::SPIRV};
}

// Builds a minimal, externally-valid SPIR-V 1.3 fragment shader sampling
// set 0 binding 0 (1D/2D/3D/2DArray/Cube), binding 1 (float4 params UBO), binding 32 (sampler).
std::vector<std::uint32_t> buildSampleFragmentSpirv(gfx::TextureDimension dim) {
    std::vector<std::uint32_t> w;
    auto emit = [&](std::uint16_t op, std::initializer_list<std::uint32_t> args) {
        w.push_back((static_cast<std::uint32_t>(args.size() + 1) << 16) | op);
        for (std::uint32_t v : args) w.push_back(v);
    };
    const std::uint32_t spvDim = dim == gfx::TextureDimension::Texture1D ? 0u
                               : dim == gfx::TextureDimension::Texture3D ? 2u
                               : dim == gfx::TextureDimension::TextureCube ? 3u
                               : 1u;
    const std::uint32_t spvArrayed = dim == gfx::TextureDimension::Texture2DArray ? 1u : 0u;

    // Header (bound = 38)
    w.push_back(0x07230203u);
    w.push_back(0x00010300u);
    w.push_back(0x00070000u);
    w.push_back(38u);
    w.push_back(0u);

    emit(17, {1}); // OpCapability Shader
    if (dim == gfx::TextureDimension::Texture1D) {
        emit(17, {43}); // OpCapability Sampled1D
    }
    emit(14, {0, 1}); // OpMemoryModel Logical GLSL450
    emit(15, {4, 1, 0x6e69616du, 0u, 2, 3}); // OpEntryPoint Fragment %1 "main" %2 %3
    emit(16, {1, 7}); // OpExecutionMode %1 OriginUpperLeft

    // Decorations
    emit(71, {2, 30, 0});  // %inUv Location 0
    emit(71, {3, 30, 0});  // %outColor Location 0
    emit(71, {4, 34, 0});  // %texVar DescriptorSet 0
    emit(71, {4, 33, 0});  // %texVar Binding 0
    emit(71, {5, 2});      // %uboStruct Block
    emit(72, {5, 0, 35, 0}); // %uboStruct member 0 Offset 0
    emit(71, {6, 34, 0});  // %uboVar DescriptorSet 0
    emit(71, {6, 33, 1});  // %uboVar Binding 1
    emit(71, {7, 34, 0});  // %sampVar DescriptorSet 0
    emit(71, {7, 33, 32}); // %sampVar Binding 32

    // Types & constants
    emit(19, {8});         // %void
    emit(33, {9, 8});      // %fnType
    emit(22, {10, 32});    // %f32
    emit(23, {11, 10, 2}); // %v2
    emit(23, {12, 10, 3}); // %v3
    emit(23, {13, 10, 4}); // %v4
    emit(21, {14, 32, 1}); // %i32
    emit(43, {14, 15, 0}); // %c0 = 0
    emit(32, {16, 1, 11}); // %ptrInV2
    emit(32, {17, 3, 13}); // %ptrOutV4
    emit(25, {18, 10, spvDim, 0, spvArrayed, 0, 1, 0}); // %imgType
    emit(32, {19, 0, 18}); // %ptrImg
    emit(26, {20});        // %sampType
    emit(32, {21, 0, 20}); // %ptrSamp
    emit(27, {22, 18});    // %sampledImgType
    emit(30, {5, 13});     // %uboStruct = { v4 }
    emit(32, {23, 2, 5});  // %ptrUbo
    emit(32, {24, 2, 13}); // %ptrUboV4

    // Variables
    emit(59, {16, 2, 1}); // %inUv
    emit(59, {17, 3, 3}); // %outColor
    emit(59, {19, 4, 0}); // %texVar
    emit(59, {23, 6, 2}); // %uboVar
    emit(59, {21, 7, 0}); // %sampVar

    // Function main
    emit(54, {8, 1, 0, 9});
    emit(248, {25});
    emit(61, {11, 26, 2});     // %uvVal
    emit(65, {24, 27, 6, 15}); // %pPtr
    emit(61, {13, 28, 27});    // %params
    emit(61, {18, 29, 4});     // %imgVal
    emit(61, {20, 30, 7});     // %sampVal
    emit(86, {22, 31, 29, 30}); // %siVal

    std::uint32_t coordId = 26;
    std::uint32_t lodId = 33;
    if (dim == gfx::TextureDimension::Texture1D) {
        emit(81, {10, 32, 26, 0}); // u = uv.x
        emit(81, {10, 33, 28, 0}); // lod = params.x
        coordId = 32;
        lodId = 33;
    } else if (dim == gfx::TextureDimension::Texture2D) {
        emit(81, {10, 33, 28, 0}); // lod = params.x
        coordId = 26;
        lodId = 33;
    } else if (dim == gfx::TextureDimension::Texture2DArray || dim == gfx::TextureDimension::Texture3D) {
        emit(81, {10, 32, 26, 0}); // u = uv.x
        emit(81, {10, 33, 26, 1}); // v = uv.y
        emit(81, {10, 34, 28, 0}); // w = params.x (layer or z)
        emit(80, {12, 35, 32, 33, 34}); // vec3(u, v, w)
        emit(81, {10, 36, 28, 1}); // lod = params.y
        coordId = 35;
        lodId = 36;
    } else { // TextureCube
        emit(81, {10, 32, 28, 0}); // dx = params.x
        emit(81, {10, 33, 28, 1}); // dy = params.y
        emit(81, {10, 34, 28, 2}); // dz = params.z
        emit(80, {12, 35, 32, 33, 34}); // vec3(dx, dy, dz)
        emit(81, {10, 36, 28, 3}); // lod = params.w
        coordId = 35;
        lodId = 36;
    }

    emit(88, {13, 37, 31, coordId, 2, lodId}); // OpImageSampleExplicitLod
    emit(62, {3, 37}); // OpStore %outColor %texel
    emit(253, {});     // OpReturn
    emit(56, {});      // OpFunctionEnd
    return w;
}

// Solid color fragment shader (no varyings, UBO at binding 0).
std::vector<std::uint32_t> buildSolidFragmentSpirv() {
    std::vector<std::uint32_t> w;
    auto emit = [&](std::uint16_t op, std::initializer_list<std::uint32_t> args) {
        w.push_back((static_cast<std::uint32_t>(args.size() + 1) << 16) | op);
        for (std::uint32_t v : args) w.push_back(v);
    };
    w.push_back(0x07230203u);
    w.push_back(0x00010300u);
    w.push_back(0x00070000u);
    w.push_back(18u);
    w.push_back(0u);
    emit(17, {1});
    emit(14, {0, 1});
    emit(15, {4, 1, 0x6e69616du, 0u, 2}); // Fragment %1 "main" %outColor(2)
    emit(16, {1, 7});
    emit(71, {2, 30, 0});     // %outColor Location 0
    emit(71, {3, 2});         // %uboStruct Block
    emit(72, {3, 0, 35, 0});  // Offset 0
    emit(71, {4, 34, 0});     // %uboVar DescriptorSet 0
    emit(71, {4, 33, 0});     // %uboVar Binding 0
    emit(19, {5});            // %void
    emit(33, {6, 5});         // %fnType
    emit(22, {7, 32});        // %f32
    emit(23, {8, 7, 4});      // %v4
    emit(21, {9, 32, 1});     // %i32
    emit(43, {9, 10, 0});     // %c0
    emit(32, {11, 3, 8});     // %ptrOutV4
    emit(30, {3, 8});         // %uboStruct
    emit(32, {12, 2, 3});     // %ptrUbo
    emit(32, {13, 2, 8});     // %ptrUboV4
    emit(59, {11, 2, 3});     // %outColor
    emit(59, {12, 4, 2});     // %uboVar
    emit(54, {5, 1, 0, 6});
    emit(248, {14});
    emit(65, {13, 15, 4, 10});
    emit(61, {8, 16, 15});
    emit(62, {2, 16});
    emit(253, {});
    emit(56, {});
    return w;
}

gfx::BindingType bindingTypeFor(gfx::TextureDimension dim) {
    switch (dim) {
    case gfx::TextureDimension::Texture1D: return gfx::BindingType::Texture1D;
    case gfx::TextureDimension::Texture3D: return gfx::BindingType::Texture3D;
    case gfx::TextureDimension::Texture2DArray: return gfx::BindingType::Texture2DArray;
    case gfx::TextureDimension::TextureCube: return gfx::BindingType::TextureCube;
    case gfx::TextureDimension::Texture2D: default: return gfx::BindingType::Texture2D;
    }
}

gfx::ShaderResourceType resourceTypeFor(gfx::TextureDimension dim) {
    switch (dim) {
    case gfx::TextureDimension::Texture1D: return gfx::ShaderResourceType::Texture1D;
    case gfx::TextureDimension::Texture3D: return gfx::ShaderResourceType::Texture3D;
    case gfx::TextureDimension::Texture2DArray: return gfx::ShaderResourceType::Texture2DArray;
    case gfx::TextureDimension::TextureCube: return gfx::ShaderResourceType::TextureCube;
    case gfx::TextureDimension::Texture2D: default: return gfx::ShaderResourceType::Texture2D;
    }
}

gfx::PipelinePtr makeSamplePipeline(gfx::Device& device, gfx::TextureDimension dim, Totals& totals) {
    gfx::GraphicsPipelineDesc desc{};
    if (device.caps().backend == gfx::Backend::D3D12) {
        constexpr std::string_view vsHlsl = R"(
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut VS(uint id : SV_VertexID) {
    float2 pts[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
    VSOut o;
    o.pos = float4(pts[id % 3], 0.5, 1.0);
    o.uv = pts[id % 3] * 0.5 + 0.5;
    return o;
}
)";
        std::string_view psHlsl;
        switch (dim) {
        case gfx::TextureDimension::Texture1D:
            psHlsl = R"(
Texture1D<float4> tex : register(t0);
cbuffer Params : register(b0) { float4 params; };
SamplerState samp : register(s0);
float4 PS(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
    return tex.SampleLevel(samp, uv.x, params.x);
}
)";
            break;
        case gfx::TextureDimension::Texture2D:
            psHlsl = R"(
Texture2D<float4> tex : register(t0);
cbuffer Params : register(b0) { float4 params; };
SamplerState samp : register(s0);
float4 PS(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
    return tex.SampleLevel(samp, uv, params.x);
}
)";
            break;
        case gfx::TextureDimension::Texture2DArray:
            psHlsl = R"(
Texture2DArray<float4> tex : register(t0);
cbuffer Params : register(b0) { float4 params; };
SamplerState samp : register(s0);
float4 PS(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
    return tex.SampleLevel(samp, float3(uv, params.x), params.y);
}
)";
            break;
        case gfx::TextureDimension::TextureCube:
            psHlsl = R"(
TextureCube<float4> tex : register(t0);
cbuffer Params : register(b0) { float4 params; };
SamplerState samp : register(s0);
float4 PS(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
    return tex.SampleLevel(samp, params.xyz, params.w);
}
)";
            break;
        case gfx::TextureDimension::Texture3D:
            psHlsl = R"(
Texture3D<float4> tex : register(t0);
cbuffer Params : register(b0) { float4 params; };
SamplerState samp : register(s0);
float4 PS(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
    return tex.SampleLevel(samp, float3(uv, params.x), params.y);
}
)";
            break;
        }
        desc.vertex = take(gfx::compileHlsl(vsHlsl, "VS", gfx::ShaderStage::Vertex), "compile sample VS", totals);
        desc.fragment = take(gfx::compileHlsl(psHlsl, "PS", gfx::ShaderStage::Fragment), "compile sample PS", totals);
    } else {
        desc.vertex = spirvBinary(gfx::ShaderStage::Vertex, bindingFixtures::uv);
        const auto fragWords = buildSampleFragmentSpirv(dim);
        desc.fragment = spirvBinary(gfx::ShaderStage::Fragment, fragWords);
    }
    // Verify reflection reports the exact texture dimension type.
    const auto reflected = take(gfx::reflectShader(desc.fragment), "reflect sample PS", totals);
    bool foundTex = false;
    for (const auto& b : reflected) {
        if (b.type == resourceTypeFor(dim)) foundTex = true;
    }
    check(foundTex, "Reflected shader must report exact sampled texture dimension", totals);

    desc.colorFormat = gfx::Format::RGBA8UNorm;
    desc.bindingLayout = {
        {
            {bindingTypeFor(dim), 0, 1, 0, gfx::ShaderVisibility::Fragment},
            {gfx::BindingType::ConstantBuffer, 0, 1, 0, gfx::ShaderVisibility::Fragment}
        },
        {
            {0, 0, gfx::ShaderVisibility::Fragment, gfx::SamplerFilter::Point, gfx::SamplerAddress::Clamp}
        }
    };
    return take(device.createGraphicsPipeline(desc), "create sample pipeline", totals);
}

gfx::PipelinePtr makeSolidPipeline(gfx::Device& device, Totals& totals) {
    gfx::GraphicsPipelineDesc desc{};
    if (device.caps().backend == gfx::Backend::D3D12) {
        constexpr std::string_view vsHlsl = R"(
float4 VS(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * 2.0 - 1.0, 0.5, 1.0);
}
)";
        constexpr std::string_view psHlsl = R"(
cbuffer Color : register(b0) { float4 color; };
float4 PS() : SV_Target0 { return color; }
)";
        desc.vertex = take(gfx::compileHlsl(vsHlsl, "VS", gfx::ShaderStage::Vertex), "compile solid VS", totals);
        desc.fragment = take(gfx::compileHlsl(psHlsl, "PS", gfx::ShaderStage::Fragment), "compile solid PS", totals);
    } else {
        desc.vertex = spirvBinary(gfx::ShaderStage::Vertex, fixtures::fullscreen);
        const auto fragWords = buildSolidFragmentSpirv();
        desc.fragment = spirvBinary(gfx::ShaderStage::Fragment, fragWords);
    }
    desc.colorFormat = gfx::Format::RGBA8UNorm;
    desc.bindingLayout = {{{gfx::BindingType::ConstantBuffer, 0, 1, 0, gfx::ShaderVisibility::Fragment}}, {}};
    return take(device.createGraphicsPipeline(desc), "create solid pipeline", totals);
}

void writeCbSlot(const gfx::BufferPtr& cb, std::uint32_t slot, std::array<float, 4> values, Totals& totals) {
    take(cb->write(static_cast<std::uint64_t>(slot) * 256u, std::as_bytes(std::span{values})), "write cb slot", totals);
}

std::vector<std::byte> makeSolidSliceBytes(std::uint32_t w, std::uint32_t h, std::uint32_t rowPitch, RGBA color) {
    std::vector<std::byte> data(static_cast<std::size_t>(rowPitch) * h, std::byte{0x5a});
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            std::memcpy(data.data() + static_cast<std::size_t>(y) * rowPitch + static_cast<std::size_t>(x) * 4u, color.data(), 4);
        }
    }
    return data;
}

std::vector<std::byte> makeHalfSplitSliceBytes(std::uint32_t w, std::uint32_t h, std::uint32_t rowPitch, RGBA left, RGBA right) {
    std::vector<std::byte> data(static_cast<std::size_t>(rowPitch) * h, std::byte{0x3c});
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const RGBA c = (x < w / 2u) ? left : right;
            std::memcpy(data.data() + static_cast<std::size_t>(y) * rowPitch + static_cast<std::size_t>(x) * 4u, c.data(), 4);
        }
    }
    return data;
}

void verifyUniformPixels(
    const std::vector<std::byte>& bytes,
    std::uint32_t w,
    std::uint32_t h,
    std::uint32_t rowPitch,
    RGBA expected,
    int tolerance,
    const char* label,
    Totals& totals) {
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::size_t off = static_cast<std::size_t>(y) * rowPitch + static_cast<std::size_t>(x) * 4u;
            for (std::size_t c = 0; c < 4; ++c) {
                const int got = std::to_integer<int>(bytes[off + c]);
                if (std::abs(got - static_cast<int>(expected[c])) > tolerance) {
                    throw std::runtime_error(std::string(label) + " pixel mismatch at (" + std::to_string(x) + "," +
                        std::to_string(y) + ") ch=" + std::to_string(c) + " got=" + std::to_string(got) +
                        " expected=" + std::to_string(static_cast<int>(expected[c])));
                }
            }
        }
    }
    ++totals.assertions;
}

void verifyHalfSplitPixels(
    const std::vector<std::byte>& bytes,
    std::uint32_t w,
    std::uint32_t h,
    std::uint32_t rowPitch,
    RGBA left,
    RGBA right,
    int tolerance,
    const char* label,
    Totals& totals) {
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const RGBA expected = (x < w / 2u) ? left : right;
            const std::size_t off = static_cast<std::size_t>(y) * rowPitch + static_cast<std::size_t>(x) * 4u;
            for (std::size_t c = 0; c < 4; ++c) {
                const int got = std::to_integer<int>(bytes[off + c]);
                if (std::abs(got - static_cast<int>(expected[c])) > tolerance) {
                    throw std::runtime_error(std::string(label) + " split mismatch at (" + std::to_string(x) + "," +
                        std::to_string(y) + ") ch=" + std::to_string(c) + " got=" + std::to_string(got) +
                        " expected=" + std::to_string(static_cast<int>(expected[c])));
                }
            }
        }
    }
    ++totals.assertions;
}

void verifyReclaimed(gfx::Device& device, Totals& totals) {
    take(device.queue().waitIdle(), "waitIdle before stats", totals);
    const auto stats = take(device.resourceStatistics(), "resourceStatistics", totals);
    check(stats.trackedObjects == 0 && stats.uniqueResources == 0 &&
          stats.resourceAllocationBytes == 0 && stats.pendingSubmissions == 0 &&
          stats.pendingObjectReferences == 0,
          "All native resources and pending submissions must be reclaimed", totals);
}

void runRejectionContracts(gfx::Device& device, Totals& totals) {
    using Dim = gfx::TextureDimension;
    using F = gfx::Format;
    using Usage = gfx::TextureViewUsage;

    // 1. Unsupported and invalid TextureDesc combinations
    reject(device.createTexture({16, 2, F::RGBA8UNorm, false, false, false, 1, Dim::Texture1D, 1, 1, 1}),
           gfx::ErrorCode::InvalidArgument, "Texture1D height != 1", totals);
    reject(device.createTexture({16, 1, F::RGBA8UNorm, true, false, false, 1, Dim::Texture1D, 1, 1, 1}),
           gfx::ErrorCode::Unsupported, "Texture1D renderTarget unsupported", totals);
    reject(device.createTexture({16, 1, F::D32Float, false, true, false, 1, Dim::Texture1D, 1, 1, 1}),
           gfx::ErrorCode::Unsupported, "Texture1D depthStencil unsupported", totals);
    reject(device.createTexture({16, 1, F::RGBA8UNorm, false, false, true, 1, Dim::Texture1D, 1, 1, 1}),
           gfx::ErrorCode::Unsupported, "Texture1D storage unsupported", totals);
    reject(device.createTexture({16, 1, F::RGBA8UNorm, false, false, false, 1, Dim::Texture1D, 1, 6, 1}),
           gfx::ErrorCode::InvalidArgument, "Texture1D mipLevels exceeds chain", totals);

    reject(device.createTexture({8, 8, F::RGBA8UNorm, false, false, false, 1, Dim::Texture3D, 0, 1, 1}),
           gfx::ErrorCode::InvalidArgument, "Texture3D depth == 0", totals);
    reject(device.createTexture({8, 8, F::RGBA8UNorm, false, false, false, 1, Dim::Texture3D, 4, 1, 2}),
           gfx::ErrorCode::InvalidArgument, "Texture3D arrayLayers != 1", totals);
    reject(device.createTexture({8, 8, F::RGBA8UNorm, true, false, false, 1, Dim::Texture3D, 4, 1, 1}),
           gfx::ErrorCode::Unsupported, "Texture3D renderTarget unsupported", totals);
    reject(device.createTexture({8, 8, F::D32Float, false, true, false, 1, Dim::Texture3D, 4, 1, 1}),
           gfx::ErrorCode::Unsupported, "Texture3D depthStencil unsupported", totals);
    reject(device.createTexture({8, 8, F::RGBA8UNorm, false, false, true, 1, Dim::Texture3D, 4, 1, 1}),
           gfx::ErrorCode::Unsupported, "Texture3D storage unsupported", totals);
    reject(device.createTexture({8, 8, F::RGBA8UNorm, false, false, false, 1, Dim::Texture3D, 4, 5, 1}),
           gfx::ErrorCode::InvalidArgument, "Texture3D mipLevels exceeds chain", totals);

    reject(device.createTexture({16, 16, F::RGBA8UNorm, false, false, false, 1, Dim::Texture2DArray, 1, 1, 0}),
           gfx::ErrorCode::InvalidArgument, "Texture2DArray arrayLayers == 0", totals);
    reject(device.createTexture({16, 16, F::RGBA8UNorm, false, false, true, 1, Dim::Texture2DArray, 1, 1, 2}),
           gfx::ErrorCode::Unsupported, "Texture2DArray storage unsupported", totals);

    reject(device.createTexture({16, 8, F::RGBA8UNorm, false, false, false, 1, Dim::TextureCube, 1, 1, 6}),
           gfx::ErrorCode::InvalidArgument, "TextureCube non-square rejected", totals);
    reject(device.createTexture({16, 16, F::RGBA8UNorm, false, false, false, 1, Dim::TextureCube, 1, 1, 4}),
           gfx::ErrorCode::InvalidArgument, "TextureCube arrayLayers != 6 rejected", totals);
    reject(device.createTexture({16, 16, F::D32Float, false, true, false, 1, Dim::TextureCube, 1, 1, 6}),
           gfx::ErrorCode::Unsupported, "TextureCube depthStencil unsupported", totals);
    reject(device.createTexture({16, 16, F::RGBA8UNorm, false, false, true, 1, Dim::TextureCube, 1, 1, 6}),
           gfx::ErrorCode::Unsupported, "TextureCube storage unsupported", totals);

    reject(device.createTexture({16, 16, F::RGBA8UNorm, true, false, false, 4, Dim::Texture2D, 1, 2, 1}),
           gfx::ErrorCode::InvalidArgument, "MSAA with mipLevels > 1 rejected", totals);
    reject(device.createTexture({16, 16, F::RGBA8UNorm, true, false, false, 4, Dim::Texture2DArray, 1, 1, 2}),
           gfx::ErrorCode::InvalidArgument, "MSAA Texture2DArray rejected", totals);
    reject(device.createTexture({16, 16, F::RGBA8UNorm, true, false, false, 4, Dim::TextureCube, 1, 1, 6}),
           gfx::ErrorCode::InvalidArgument, "MSAA TextureCube rejected", totals);

    // 2. View & subresource copy & cross-dimension binding rejections
    auto arr = take(device.createTexture({16, 16, F::RGBA8UNorm, true, false, false, 1, Dim::Texture2DArray, 1, 3, 4}), "create array for rejections", totals);
    auto cube = take(device.createTexture({16, 16, F::RGBA8UNorm, true, false, false, 1, Dim::TextureCube, 1, 2, 6}), "create cube for rejections", totals);
    auto vol = take(device.createTexture({8, 8, F::RGBA8UNorm, false, false, false, 1, Dim::Texture3D, 4, 2, 1}), "create 3D for rejections", totals);
    auto tex2d = take(device.createTexture({16, 16, F::RGBA8UNorm, true, false, false, 1, Dim::Texture2D, 1, 1, 1}), "create 2D for rejections", totals);
    auto upload = take(device.createBuffer({4096, gfx::Memory::Upload}), "create upload for rejections", totals);
    auto cb = take(device.createBuffer({256, gfx::Memory::Upload}), "create cb for rejections", totals);
    auto cubePipe = makeSamplePipeline(device, Dim::TextureCube, totals);

    reject(device.createTextureView(arr, {std::nullopt, Usage::ShaderRead, 0, 1, 0, 1, Dim::Texture2D}),
           gfx::ErrorCode::Unsupported, "Texture2DArray 2D SRV view unsupported", totals);
    reject(device.createTextureView(cube, {std::nullopt, Usage::ShaderRead, 0, 1, 0, 4, Dim::TextureCube}),
           gfx::ErrorCode::InvalidArgument, "TextureCube view requires 6 layers", totals);
    reject(device.createTextureView(cube, {std::nullopt, Usage::ShaderRead, 0, 1, 0, 1, Dim::Texture2D}),
           gfx::ErrorCode::Unsupported, "TextureCube 2D SRV view unsupported", totals);
    reject(device.createTextureView(arr, {std::nullopt, Usage::RenderTarget, 0, 2, 0, 1}),
           gfx::ErrorCode::InvalidArgument, "Multi-mip RTV view rejected", totals);
    reject(device.createTextureView(arr, {std::nullopt, Usage::RenderTarget, 0, 1, 0, 2}),
           gfx::ErrorCode::InvalidArgument, "Multi-layer RTV view rejected", totals);

    auto cmd = take(device.createCommandList(), "create cmd for rejections", totals);
    take(cmd->begin(), "begin rejection cmd", totals);
    take(cmd->transition(arr, gfx::ResourceState::RenderTarget), "transition arr RT", totals);
    reject(cmd->beginRenderPass(arr, {0, 0, 0, 1}), gfx::ErrorCode::InvalidArgument, "Raw Texture2DArray render pass requires TextureView", totals);
    take(cmd->transition(arr, gfx::ResourceState::CopyDestination), "transition arr CopyDst", totals);
    take(cmd->transition(vol, gfx::ResourceState::CopyDestination), "transition vol CopyDst", totals);
    take(cmd->transition(tex2d, gfx::ResourceState::RenderTarget), "transition tex2d RT", totals);
    take(cmd->transition(cb, gfx::ResourceState::ConstantBuffer), "transition cb", totals);

    // Out-of-bounds mip, layer, extent, z/depth in copyBufferToTextureRegion
    reject(cmd->copyBufferToTextureRegion(upload, arr, {0, 0, 0, 0, 16, 16, 3, 0, 0, 1}),
           gfx::ErrorCode::InvalidArgument, "Copy mipLevel out of bounds", totals);
    reject(cmd->copyBufferToTextureRegion(upload, arr, {0, 0, 0, 0, 16, 16, 0, 4, 0, 1}),
           gfx::ErrorCode::InvalidArgument, "Copy arrayLayer out of bounds", totals);
    reject(cmd->copyBufferToTextureRegion(upload, arr, {0, 0, 0, 0, 16, 16, 1, 0, 0, 1}),
           gfx::ErrorCode::InvalidArgument, "Copy width exceeds mip 1 extent", totals);
    reject(cmd->copyBufferToTextureRegion(upload, vol, {0, 0, 0, 0, 8, 8, 0, 0, 2, 3}),
           gfx::ErrorCode::InvalidArgument, "Copy 3D z+depth exceeds depth", totals);
    reject(cmd->copyBufferToTextureRegion(upload, vol, {0, 0, 0, 0, 4, 4, 1, 0, 0, 3}),
           gfx::ErrorCode::InvalidArgument, "Copy 3D mip 1 depth exceeds mipD", totals);
    reject(cmd->copyBufferToTextureRegion(upload, vol, {0, 0, 0, 0, 8, 8, 0, 0, 0, 0}),
           gfx::ErrorCode::InvalidArgument, "Copy depth == 0 rejected", totals);

    // Cross-dimension binding mismatch (Texture2D bound to TextureCube pipeline)
    auto rt = take(device.createTexture({8, 8, F::RGBA8UNorm, true}), "create rt for mismatch", totals);
    take(cmd->beginRenderPass(tex2d, {1, 0, 0, 1}), "init tex2d", totals);
    take(cmd->endRenderPass(), "end init tex2d", totals);
    take(cmd->transition(tex2d, gfx::ResourceState::ShaderRead), "tex2d ShaderRead", totals);
    take(cmd->transition(rt, gfx::ResourceState::RenderTarget), "rt RenderTarget", totals);
    take(cmd->beginRenderPass(rt, {0, 0, 0, 1}), "begin mismatch pass", totals);
    take(cmd->setPipeline(cubePipe), "set cube pipeline", totals);
    reject(cmd->bindResources({{{0, 0, tex2d}}, {{1, 0, cb, 0, 256}}}),
           gfx::ErrorCode::InvalidArgument, "Binding Texture2D to TextureCube pipeline rejected", totals);
    take(cmd->endRenderPass(), "end mismatch pass", totals);
    take(cmd->end(), "end rejection cmd", totals);
    auto f = take(device.queue().submit(*cmd), "submit rejection cmd", totals);
    take(device.queue().wait(f), "wait rejection cmd", totals);
}

void runMultiMip2D(gfx::Device& device, const gfx::PipelinePtr& sample2dPipe, const gfx::PipelinePtr& solidPipe, Totals& totals) {
    constexpr std::uint32_t baseSize = 16;
    constexpr std::uint32_t mipCount = 4; // 16, 8, 4, 2
    const std::array<RGBA, mipCount> uploadColors{{
        {220, 40, 40, 255},
        {40, 220, 40, 255},
        {40, 80, 230, 255},
        {230, 210, 40, 255}
    }};
    const RGBA renderedMip1{200, 50, 220, 255};

    auto tex = take(device.createTexture({
        baseSize, baseSize, gfx::Format::RGBA8UNorm, true, false, false, 1,
        gfx::TextureDimension::Texture2D, 1, mipCount, 1
    }), "create multi-mip Texture2D", totals);

    auto upload = take(device.createBuffer({16384, gfx::Memory::Upload}), "create 2D mip upload", totals);
    auto readback = take(device.createBuffer({16384, gfx::Memory::Readback}), "create 2D mip readback", totals);
    auto cb = take(device.createBuffer({2048, gfx::Memory::Upload}), "create 2D mip cb", totals);
    auto sampleOut = take(device.createTexture({8, 8, gfx::Format::RGBA8UNorm, true}), "create 2D sampleOut", totals);
    auto sampleRead = take(device.createBuffer({sampleOut->footprint().size, gfx::Memory::Readback}), "create 2D sampleRead", totals);

    std::array<std::uint64_t, mipCount> offsets{};
    std::array<gfx::TextureFootprint, mipCount> fps{};
    std::uint64_t cursor = 0;
    for (std::uint32_t m = 0; m < mipCount; ++m) {
        const std::uint32_t dim = baseSize >> m;
        fps[m] = take(gfx::computeTextureRegionFootprint(gfx::Format::RGBA8UNorm, dim, dim), "2D mip footprint", totals);
        offsets[m] = cursor;
        const auto sliceBytes = makeSolidSliceBytes(dim, dim, fps[m].rowPitch, uploadColors[m]);
        take(upload->write(cursor, sliceBytes), "write 2D mip upload", totals);
        cursor = (cursor + fps[m].size + 511u) & ~std::uint64_t{511};
    }

    // Slot 0: solid render color for mip 1; slots 1..4: lod=0..3; slot 5: lod=0 (for single-mip SRV view)
    writeCbSlot(cb, 0, {renderedMip1[0] / 255.0f, renderedMip1[1] / 255.0f, renderedMip1[2] / 255.0f, 1.0f}, totals);
    for (std::uint32_t m = 0; m < mipCount; ++m) {
        writeCbSlot(cb, 1 + m, {static_cast<float>(m), 0.0f, 0.0f, 0.0f}, totals);
    }

    auto rtvMip1 = take(device.createTextureView(tex, {std::nullopt, gfx::TextureViewUsage::RenderTarget, 1, 1, 0, 1}), "create 2D mip1 RTV", totals);
    auto srvMip2 = take(device.createTextureView(tex, {std::nullopt, gfx::TextureViewUsage::ShaderRead, 2, 1, 0, 1}), "create 2D mip2 SRV", totals);

    auto cmd = take(device.createCommandList(), "create 2D mip cmd", totals);
    take(cmd->begin(), "begin 2D mip cmd", totals);
    take(cmd->transition(cb, gfx::ResourceState::ConstantBuffer), "cb state", totals);
    take(cmd->transition(tex, gfx::ResourceState::CopyDestination), "tex CopyDst", totals);
    for (std::uint32_t m = 0; m < mipCount; ++m) {
        const std::uint32_t dim = baseSize >> m;
        take(cmd->copyBufferToTextureRegion(upload, tex, {offsets[m], 0, 0, 0, dim, dim, m, 0, 0, 1}), "upload 2D mip", totals);
    }
    // Render into mip 1 (8x8) via RTV TextureView (validates mip-scaled viewport/scissor)
    take(cmd->transition(tex, gfx::ResourceState::RenderTarget), "tex RT", totals);
    take(cmd->beginRenderPass({{{nullptr, gfx::LoadOp::Clear, {0, 0, 0, 1}, rtvMip1}}, {}}), "begin mip1 pass", totals);
    take(cmd->setViewport({0.0f, 0.0f, 8.0f, 8.0f, 0.0f, 1.0f}), "set mip1 viewport", totals);
    take(cmd->setScissor(0, 0, 8, 8), "set mip1 scissor", totals);
    reject(cmd->setScissor(0, 0, 16, 16), gfx::ErrorCode::InvalidArgument, "Scissor exceeding mip1 extent rejected", totals);
    take(cmd->setPipeline(solidPipe), "set solid pipe", totals);
    take(cmd->bindResources({{}, {{0, 0, cb, 0, 256}}}), "bind solid cb", totals);
    take(cmd->draw(3), "draw mip1", totals);
    take(cmd->endRenderPass(), "end mip1 pass", totals);

    // Direct readback of every mip
    take(cmd->transition(tex, gfx::ResourceState::CopySource), "tex CopySrc", totals);
    for (std::uint32_t m = 0; m < mipCount; ++m) {
        const std::uint32_t dim = baseSize >> m;
        take(cmd->copyTextureToBufferRegion(tex, readback, {offsets[m], 0, 0, 0, dim, dim, m, 0, 0, 1}), "readback 2D mip", totals);
    }
    take(cmd->end(), "end 2D mip upload+rt+readback", totals);
    auto fence = take(device.queue().submit(*cmd), "submit 2D mip", totals);
    take(device.queue().wait(fence), "wait 2D mip", totals);

    std::array<RGBA, mipCount> expected = uploadColors;
    expected[1] = renderedMip1;
    for (std::uint32_t m = 0; m < mipCount; ++m) {
        const std::uint32_t dim = baseSize >> m;
        const auto rb = take(readback->read(offsets[m], fps[m].size), "read 2D mip", totals);
        verifyUniformPixels(rb, dim, dim, fps[m].rowPitch, expected[m], 1, "Texture2D direct mip readback", totals);
        ++totals.subresourcesVerified;
    }

    // Sample all 4 mips via SampleLevel(lod) and verify single-mip SRV view on mip 2
    for (std::uint32_t pass = 0; pass <= mipCount; ++pass) {
        take(cmd->begin(), "begin 2D sample cmd", totals);
        if (pass == 0) take(cmd->transition(tex, gfx::ResourceState::ShaderRead), "tex ShaderRead", totals);
        take(cmd->transition(sampleOut, gfx::ResourceState::RenderTarget), "sampleOut RT", totals);
        take(cmd->beginRenderPass(sampleOut, {0, 0, 0, 1}), "begin 2D sample pass", totals);
        take(cmd->setPipeline(sample2dPipe), "set sample2d pipe", totals);
        if (pass < mipCount) {
            take(cmd->bindResources({{{0, 0, tex}}, {{1, 0, cb, (1u + pass) * 256ull, 256}}}), "bind 2D full chain", totals);
        } else {
            take(cmd->bindResources({{{0, 0, nullptr, srvMip2}}, {{1, 0, cb, 1u * 256ull, 256}}}), "bind 2D mip2 view", totals);
        }
        take(cmd->draw(3), "draw 2D sample", totals);
        take(cmd->endRenderPass(), "end 2D sample pass", totals);
        take(cmd->transition(sampleOut, gfx::ResourceState::CopySource), "sampleOut CopySrc", totals);
        take(cmd->copyTextureToBuffer(sampleOut, sampleRead), "copy sampleOut", totals);
        take(cmd->end(), "end 2D sample cmd", totals);
        auto sf = take(device.queue().submit(*cmd), "submit 2D sample", totals);
        take(device.queue().wait(sf), "wait 2D sample", totals);

        const auto outBytes = take(sampleRead->read(0, sampleOut->footprint().size), "read 2D sample", totals);
        const RGBA exp = pass < mipCount ? expected[pass] : expected[2];
        verifyUniformPixels(outBytes, 8, 8, sampleOut->footprint().rowPitch, exp, 1, "Texture2D sampled mip", totals);
        ++totals.sampledPassesVerified;
    }
}

void runTexture1D(gfx::Device& device, const gfx::PipelinePtr& sample1dPipe, Totals& totals) {
    constexpr std::uint32_t baseW = 16;
    constexpr std::uint32_t mipCount = 3; // 16, 8, 4
    const std::array<std::pair<RGBA, RGBA>, mipCount> mipColors{{
        {{240, 30, 30, 255}, {30, 240, 30, 255}},
        {{30, 30, 240, 255}, {240, 240, 30, 255}},
        {{240, 30, 240, 255}, {30, 240, 240, 255}}
    }};

    auto tex = take(device.createTexture({
        baseW, 1, gfx::Format::RGBA8UNorm, false, false, false, 1,
        gfx::TextureDimension::Texture1D, 1, mipCount, 1
    }), "create Texture1D", totals);

    auto upload = take(device.createBuffer({4096, gfx::Memory::Upload}), "create 1D upload", totals);
    auto readback = take(device.createBuffer({4096, gfx::Memory::Readback}), "create 1D readback", totals);
    auto cb = take(device.createBuffer({1024, gfx::Memory::Upload}), "create 1D cb", totals);
    auto sampleOut = take(device.createTexture({16, 4, gfx::Format::RGBA8UNorm, true}), "create 1D sampleOut", totals);
    auto sampleRead = take(device.createBuffer({sampleOut->footprint().size, gfx::Memory::Readback}), "create 1D sampleRead", totals);

    std::array<std::uint64_t, mipCount> offsets{};
    std::array<gfx::TextureFootprint, mipCount> fps{};
    std::uint64_t cursor = 0;
    for (std::uint32_t m = 0; m < mipCount; ++m) {
        const std::uint32_t w = baseW >> m;
        fps[m] = take(gfx::computeTextureRegionFootprint(gfx::Format::RGBA8UNorm, w, 1), "1D mip footprint", totals);
        offsets[m] = cursor;
        const auto bytes = makeHalfSplitSliceBytes(w, 1, fps[m].rowPitch, mipColors[m].first, mipColors[m].second);
        take(upload->write(cursor, bytes), "write 1D upload", totals);
        cursor = (cursor + fps[m].size + 511u) & ~std::uint64_t{511};
        writeCbSlot(cb, m, {static_cast<float>(m), 0.0f, 0.0f, 0.0f}, totals);
    }

    auto cmd = take(device.createCommandList(), "create 1D cmd", totals);
    take(cmd->begin(), "begin 1D upload+readback", totals);
    take(cmd->transition(cb, gfx::ResourceState::ConstantBuffer), "1D cb state", totals);
    take(cmd->transition(tex, gfx::ResourceState::CopyDestination), "1D CopyDst", totals);
    for (std::uint32_t m = 0; m < mipCount; ++m) {
        take(cmd->copyBufferToTextureRegion(upload, tex, {offsets[m], 0, 0, 0, baseW >> m, 1, m, 0, 0, 1}), "upload 1D mip", totals);
    }
    take(cmd->transition(tex, gfx::ResourceState::CopySource), "1D CopySrc", totals);
    for (std::uint32_t m = 0; m < mipCount; ++m) {
        take(cmd->copyTextureToBufferRegion(tex, readback, {offsets[m], 0, 0, 0, baseW >> m, 1, m, 0, 0, 1}), "readback 1D mip", totals);
    }
    take(cmd->transition(tex, gfx::ResourceState::ShaderRead), "1D ShaderRead", totals);
    take(cmd->end(), "end 1D upload+readback", totals);
    auto f = take(device.queue().submit(*cmd), "submit 1D upload", totals);
    take(device.queue().wait(f), "wait 1D upload", totals);

    for (std::uint32_t m = 0; m < mipCount; ++m) {
        const auto rb = take(readback->read(offsets[m], fps[m].size), "read 1D mip", totals);
        verifyHalfSplitPixels(rb, baseW >> m, 1, fps[m].rowPitch, mipColors[m].first, mipColors[m].second, 0, "Texture1D direct readback", totals);
        ++totals.subresourcesVerified;
    }

    for (std::uint32_t m = 0; m < mipCount; ++m) {
        take(cmd->begin(), "begin 1D sample", totals);
        take(cmd->transition(sampleOut, gfx::ResourceState::RenderTarget), "1D sampleOut RT", totals);
        take(cmd->beginRenderPass(sampleOut, {0, 0, 0, 1}), "begin 1D sample pass", totals);
        take(cmd->setPipeline(sample1dPipe), "set 1D pipe", totals);
        take(cmd->bindResources({{{0, 0, tex}}, {{1, 0, cb, m * 256ull, 256}}}), "bind 1D tex", totals);
        take(cmd->draw(3), "draw 1D sample", totals);
        take(cmd->endRenderPass(), "end 1D sample pass", totals);
        take(cmd->transition(sampleOut, gfx::ResourceState::CopySource), "1D sampleOut CopySrc", totals);
        take(cmd->copyTextureToBuffer(sampleOut, sampleRead), "copy 1D sampleOut", totals);
        take(cmd->end(), "end 1D sample", totals);
        auto sf = take(device.queue().submit(*cmd), "submit 1D sample", totals);
        take(device.queue().wait(sf), "wait 1D sample", totals);

        const auto outBytes = take(sampleRead->read(0, sampleOut->footprint().size), "read 1D sample", totals);
        verifyHalfSplitPixels(outBytes, 16, 4, sampleOut->footprint().rowPitch, mipColors[m].first, mipColors[m].second, 1, "Texture1D sampled", totals);
        ++totals.sampledPassesVerified;
    }
}

void runTexture2DArray(gfx::Device& device, const gfx::PipelinePtr& sampleArrayPipe, const gfx::PipelinePtr& solidPipe, Totals& totals) {
    constexpr std::uint32_t baseSize = 16;
    constexpr std::uint32_t layers = 4;
    constexpr std::uint32_t mipCount = 3; // 16, 8, 4
    auto colorFor = [](std::uint32_t layer, std::uint32_t mip) -> RGBA {
        return {
            static_cast<std::uint8_t>(40 + layer * 55),
            static_cast<std::uint8_t>(50 + mip * 80),
            static_cast<std::uint8_t>(220 - layer * 40 - mip * 20),
            255
        };
    };
    const RGBA renderedL2M1{15, 235, 190, 255};

    auto tex = take(device.createTexture({
        baseSize, baseSize, gfx::Format::RGBA8UNorm, true, false, false, 1,
        gfx::TextureDimension::Texture2DArray, 1, mipCount, layers
    }), "create Texture2DArray", totals);

    auto upload = take(device.createBuffer({65536, gfx::Memory::Upload}), "create array upload", totals);
    auto readback = take(device.createBuffer({65536, gfx::Memory::Readback}), "create array readback", totals);
    auto cb = take(device.createBuffer({4096, gfx::Memory::Upload}), "create array cb", totals);
    auto sampleOut = take(device.createTexture({8, 8, gfx::Format::RGBA8UNorm, true}), "create array sampleOut", totals);
    auto sampleRead = take(device.createBuffer({sampleOut->footprint().size, gfx::Memory::Readback}), "create array sampleRead", totals);

    std::array<std::array<std::uint64_t, mipCount>, layers> offsets{};
    std::array<gfx::TextureFootprint, mipCount> fps{};
    for (std::uint32_t m = 0; m < mipCount; ++m) {
        fps[m] = take(gfx::computeTextureRegionFootprint(gfx::Format::RGBA8UNorm, baseSize >> m, baseSize >> m), "array mip fp", totals);
    }
    std::uint64_t cursor = 0;
    for (std::uint32_t l = 0; l < layers; ++l) {
        for (std::uint32_t m = 0; m < mipCount; ++m) {
            offsets[l][m] = cursor;
            const auto bytes = makeSolidSliceBytes(baseSize >> m, baseSize >> m, fps[m].rowPitch, colorFor(l, m));
            take(upload->write(cursor, bytes), "write array upload", totals);
            cursor = (cursor + fps[m].size + 511u) & ~std::uint64_t{511};
        }
    }

    writeCbSlot(cb, 0, {renderedL2M1[0] / 255.0f, renderedL2M1[1] / 255.0f, renderedL2M1[2] / 255.0f, 1.0f}, totals);

    // Single-slice RTV view on (layer=2, mip=1)
    auto rtvL2M1 = take(device.createTextureView(tex, {std::nullopt, gfx::TextureViewUsage::RenderTarget, 1, 1, 2, 1}), "create array RTV L2M1", totals);
    // Sub-range SRV view on layers [1..2], mips [1..2]
    auto srvSub = take(device.createTextureView(tex, {std::nullopt, gfx::TextureViewUsage::ShaderRead, 1, 2, 1, 2}), "create array sub SRV", totals);

    auto cmd = take(device.createCommandList(), "create array cmd", totals);
    take(cmd->begin(), "begin array upload+rt+readback", totals);
    take(cmd->transition(cb, gfx::ResourceState::ConstantBuffer), "array cb state", totals);
    take(cmd->transition(tex, gfx::ResourceState::CopyDestination), "array CopyDst", totals);
    for (std::uint32_t l = 0; l < layers; ++l) {
        for (std::uint32_t m = 0; m < mipCount; ++m) {
            const std::uint32_t dim = baseSize >> m;
            take(cmd->copyBufferToTextureRegion(upload, tex, {offsets[l][m], 0, 0, 0, dim, dim, m, l, 0, 1}), "upload array subresource", totals);
        }
    }
    take(cmd->transition(tex, gfx::ResourceState::RenderTarget), "array RT", totals);
    take(cmd->beginRenderPass({{{nullptr, gfx::LoadOp::Clear, {0, 0, 0, 1}, rtvL2M1}}, {}}), "begin array L2M1 pass", totals);
    take(cmd->setPipeline(solidPipe), "set solid pipe for array", totals);
    take(cmd->bindResources({{}, {{0, 0, cb, 0, 256}}}), "bind solid cb for array", totals);
    take(cmd->draw(3), "draw array L2M1", totals);
    take(cmd->endRenderPass(), "end array L2M1 pass", totals);

    take(cmd->transition(tex, gfx::ResourceState::CopySource), "array CopySrc", totals);
    for (std::uint32_t l = 0; l < layers; ++l) {
        for (std::uint32_t m = 0; m < mipCount; ++m) {
            const std::uint32_t dim = baseSize >> m;
            take(cmd->copyTextureToBufferRegion(tex, readback, {offsets[l][m], 0, 0, 0, dim, dim, m, l, 0, 1}), "readback array subresource", totals);
        }
    }
    take(cmd->transition(tex, gfx::ResourceState::ShaderRead), "array ShaderRead", totals);
    take(cmd->end(), "end array upload+rt+readback", totals);
    auto f = take(device.queue().submit(*cmd), "submit array", totals);
    take(device.queue().wait(f), "wait array", totals);

    auto expectedFor = [&](std::uint32_t l, std::uint32_t m) -> RGBA {
        return (l == 2 && m == 1) ? renderedL2M1 : colorFor(l, m);
    };

    for (std::uint32_t l = 0; l < layers; ++l) {
        for (std::uint32_t m = 0; m < mipCount; ++m) {
            const auto rb = take(readback->read(offsets[l][m], fps[m].size), "read array subresource", totals);
            verifyUniformPixels(rb, baseSize >> m, baseSize >> m, fps[m].rowPitch, expectedFor(l, m), 1, "Texture2DArray direct readback", totals);
            ++totals.subresourcesVerified;
        }
    }

    // Sample all layers at mip 0 and mip 1 via full Texture2DArray, plus sub-range SRV view (view layer 1, view mip 0 => parent layer 2, mip 1)
    std::uint32_t slot = 1;
    struct SampleQuery { bool useSubView; std::uint32_t queryLayer; std::uint32_t queryMip; RGBA expected; };
    std::vector<SampleQuery> queries;
    for (std::uint32_t l = 0; l < layers; ++l) {
        queries.push_back({false, l, 0, expectedFor(l, 0)});
        queries.push_back({false, l, 1, expectedFor(l, 1)});
    }
    queries.push_back({true, 0, 1, expectedFor(1, 2)});
    queries.push_back({true, 1, 0, expectedFor(2, 1)});

    for (const auto& q : queries) {
        writeCbSlot(cb, slot, {static_cast<float>(q.queryLayer), static_cast<float>(q.queryMip), 0.0f, 0.0f}, totals);
        take(cmd->begin(), "begin array sample", totals);
        take(cmd->transition(sampleOut, gfx::ResourceState::RenderTarget), "array sampleOut RT", totals);
        take(cmd->beginRenderPass(sampleOut, {0, 0, 0, 1}), "begin array sample pass", totals);
        take(cmd->setPipeline(sampleArrayPipe), "set array sample pipe", totals);
        if (q.useSubView) {
            take(cmd->bindResources({{{0, 0, nullptr, srvSub}}, {{1, 0, cb, slot * 256ull, 256}}}), "bind array sub SRV", totals);
        } else {
            take(cmd->bindResources({{{0, 0, tex}}, {{1, 0, cb, slot * 256ull, 256}}}), "bind array tex", totals);
        }
        take(cmd->draw(3), "draw array sample", totals);
        take(cmd->endRenderPass(), "end array sample pass", totals);
        take(cmd->transition(sampleOut, gfx::ResourceState::CopySource), "array sampleOut CopySrc", totals);
        take(cmd->copyTextureToBuffer(sampleOut, sampleRead), "copy array sampleOut", totals);
        take(cmd->end(), "end array sample", totals);
        auto sf = take(device.queue().submit(*cmd), "submit array sample", totals);
        take(device.queue().wait(sf), "wait array sample", totals);

        const auto outBytes = take(sampleRead->read(0, sampleOut->footprint().size), "read array sample", totals);
        verifyUniformPixels(outBytes, 8, 8, sampleOut->footprint().rowPitch, q.expected, 1, "Texture2DArray sampled", totals);
        ++totals.sampledPassesVerified;
        ++slot;
    }
}

void runTextureCube(
    gfx::Device& device,
    const gfx::PipelinePtr& sampleCubePipe,
    const gfx::PipelinePtr& sampleArrayPipe,
    const gfx::PipelinePtr& solidPipe,
    Totals& totals) {
    constexpr std::uint32_t baseSize = 16;
    constexpr std::uint32_t faces = 6;
    constexpr std::uint32_t mipCount = 2; // 16, 8
    // Face order in both D3D12 & Vulkan: 0:+X, 1:-X, 2:+Y, 3:-Y, 4:+Z, 5:-Z
    const std::array<std::array<float, 3>, faces> dirs{{
        { 1.0f,  0.0f,  0.0f},
        {-1.0f,  0.0f,  0.0f},
        { 0.0f,  1.0f,  0.0f},
        { 0.0f, -1.0f,  0.0f},
        { 0.0f,  0.0f,  1.0f},
        { 0.0f,  0.0f, -1.0f}
    }};
    const std::array<RGBA, faces> mip0Colors{{
        {240, 20, 20, 255},
        {20, 240, 20, 255},
        {20, 20, 240, 255},
        {240, 240, 20, 255},
        {240, 20, 240, 255},
        {20, 240, 240, 255}
    }};
    const std::array<RGBA, faces> mip1Colors{{
        {130, 30, 30, 255},
        {30, 130, 30, 255},
        {30, 30, 130, 255},
        {130, 130, 30, 255},
        {130, 30, 130, 255},
        {30, 130, 130, 255}
    }};
    const RGBA renderedFace3Mip0{250, 128, 32, 255};

    auto cube = take(device.createTexture({
        baseSize, baseSize, gfx::Format::RGBA8UNorm, true, false, false, 1,
        gfx::TextureDimension::TextureCube, 1, mipCount, faces
    }), "create TextureCube", totals);

    auto upload = take(device.createBuffer({65536, gfx::Memory::Upload}), "create cube upload", totals);
    auto readback = take(device.createBuffer({65536, gfx::Memory::Readback}), "create cube readback", totals);
    auto cb = take(device.createBuffer({4096, gfx::Memory::Upload}), "create cube cb", totals);
    auto sampleOut = take(device.createTexture({8, 8, gfx::Format::RGBA8UNorm, true}), "create cube sampleOut", totals);
    auto sampleRead = take(device.createBuffer({sampleOut->footprint().size, gfx::Memory::Readback}), "create cube sampleRead", totals);

    std::array<gfx::TextureFootprint, mipCount> fps{{
        take(gfx::computeTextureRegionFootprint(gfx::Format::RGBA8UNorm, 16, 16), "cube mip0 fp", totals),
        take(gfx::computeTextureRegionFootprint(gfx::Format::RGBA8UNorm, 8, 8), "cube mip1 fp", totals)
    }};
    std::array<std::array<std::uint64_t, mipCount>, faces> offsets{};
    std::uint64_t cursor = 0;
    for (std::uint32_t f = 0; f < faces; ++f) {
        for (std::uint32_t m = 0; m < mipCount; ++m) {
            offsets[f][m] = cursor;
            const auto bytes = makeSolidSliceBytes(baseSize >> m, baseSize >> m, fps[m].rowPitch, m == 0 ? mip0Colors[f] : mip1Colors[f]);
            take(upload->write(cursor, bytes), "write cube upload", totals);
            cursor = (cursor + fps[m].size + 511u) & ~std::uint64_t{511};
        }
    }

    writeCbSlot(cb, 0, {renderedFace3Mip0[0] / 255.0f, renderedFace3Mip0[1] / 255.0f, renderedFace3Mip0[2] / 255.0f, 1.0f}, totals);

    auto rtvFace3 = take(device.createTextureView(cube, {std::nullopt, gfx::TextureViewUsage::RenderTarget, 0, 1, 3, 1}), "create cube face3 RTV", totals);
    auto srvCube = take(device.createTextureView(cube, {std::nullopt, gfx::TextureViewUsage::ShaderRead, 0, mipCount, 0, faces, gfx::TextureDimension::TextureCube}), "create cube SRV", totals);
    auto srvAsArray = take(device.createTextureView(cube, {std::nullopt, gfx::TextureViewUsage::ShaderRead, 0, mipCount, 0, faces, gfx::TextureDimension::Texture2DArray}), "create cube-as-array SRV", totals);

    auto cmd = take(device.createCommandList(), "create cube cmd", totals);
    take(cmd->begin(), "begin cube upload+rt+readback", totals);
    take(cmd->transition(cb, gfx::ResourceState::ConstantBuffer), "cube cb state", totals);
    take(cmd->transition(cube, gfx::ResourceState::CopyDestination), "cube CopyDst", totals);
    for (std::uint32_t f = 0; f < faces; ++f) {
        for (std::uint32_t m = 0; m < mipCount; ++m) {
            const std::uint32_t dim = baseSize >> m;
            take(cmd->copyBufferToTextureRegion(upload, cube, {offsets[f][m], 0, 0, 0, dim, dim, m, f, 0, 1}), "upload cube face", totals);
        }
    }
    take(cmd->transition(cube, gfx::ResourceState::RenderTarget), "cube RT", totals);
    take(cmd->beginRenderPass({{{nullptr, gfx::LoadOp::Clear, {0, 0, 0, 1}, rtvFace3}}, {}}), "begin cube face3 pass", totals);
    take(cmd->setPipeline(solidPipe), "set solid pipe for cube", totals);
    take(cmd->bindResources({{}, {{0, 0, cb, 0, 256}}}), "bind solid cb for cube", totals);
    take(cmd->draw(3), "draw cube face3", totals);
    take(cmd->endRenderPass(), "end cube face3 pass", totals);

    take(cmd->transition(cube, gfx::ResourceState::CopySource), "cube CopySrc", totals);
    for (std::uint32_t f = 0; f < faces; ++f) {
        for (std::uint32_t m = 0; m < mipCount; ++m) {
            const std::uint32_t dim = baseSize >> m;
            take(cmd->copyTextureToBufferRegion(cube, readback, {offsets[f][m], 0, 0, 0, dim, dim, m, f, 0, 1}), "readback cube face", totals);
        }
    }
    take(cmd->transition(cube, gfx::ResourceState::ShaderRead), "cube ShaderRead", totals);
    take(cmd->end(), "end cube upload+rt+readback", totals);
    auto fence = take(device.queue().submit(*cmd), "submit cube", totals);
    take(device.queue().wait(fence), "wait cube", totals);

    auto expectedFor = [&](std::uint32_t f, std::uint32_t m) -> RGBA {
        if (f == 3 && m == 0) return renderedFace3Mip0;
        return m == 0 ? mip0Colors[f] : mip1Colors[f];
    };

    for (std::uint32_t f = 0; f < faces; ++f) {
        for (std::uint32_t m = 0; m < mipCount; ++m) {
            const auto rb = take(readback->read(offsets[f][m], fps[m].size), "read cube face", totals);
            verifyUniformPixels(rb, baseSize >> m, baseSize >> m, fps[m].rowPitch, expectedFor(f, m), 1, "TextureCube direct readback", totals);
            ++totals.subresourcesVerified;
        }
    }

    // Sample all 6 faces at mip 0 and mip 1 via TextureCube
    std::uint32_t slot = 1;
    for (std::uint32_t m = 0; m < mipCount; ++m) {
        for (std::uint32_t f = 0; f < faces; ++f) {
            writeCbSlot(cb, slot, {dirs[f][0], dirs[f][1], dirs[f][2], static_cast<float>(m)}, totals);
            take(cmd->begin(), "begin cube sample", totals);
            take(cmd->transition(sampleOut, gfx::ResourceState::RenderTarget), "cube sampleOut RT", totals);
            take(cmd->beginRenderPass(sampleOut, {0, 0, 0, 1}), "begin cube sample pass", totals);
            take(cmd->setPipeline(sampleCubePipe), "set cube sample pipe", totals);
            if (m == 0) {
                take(cmd->bindResources({{{0, 0, cube}}, {{1, 0, cb, slot * 256ull, 256}}}), "bind raw cube", totals);
            } else {
                take(cmd->bindResources({{{0, 0, nullptr, srvCube}}, {{1, 0, cb, slot * 256ull, 256}}}), "bind cube view", totals);
            }
            take(cmd->draw(3), "draw cube sample", totals);
            take(cmd->endRenderPass(), "end cube sample pass", totals);
            take(cmd->transition(sampleOut, gfx::ResourceState::CopySource), "cube sampleOut CopySrc", totals);
            take(cmd->copyTextureToBuffer(sampleOut, sampleRead), "copy cube sampleOut", totals);
            take(cmd->end(), "end cube sample", totals);
            auto sf = take(device.queue().submit(*cmd), "submit cube sample", totals);
            take(device.queue().wait(sf), "wait cube sample", totals);

            const auto outBytes = take(sampleRead->read(0, sampleOut->footprint().size), "read cube sample", totals);
            verifyUniformPixels(outBytes, 8, 8, sampleOut->footprint().rowPitch, expectedFor(f, m), 1, "TextureCube sampled face", totals);
            ++totals.sampledPassesVerified;
            ++slot;
        }
    }

    // Also sample face 3 mip 0 through the Texture2DArray view over the cubemap
    writeCbSlot(cb, slot, {3.0f, 0.0f, 0.0f, 0.0f}, totals);
    take(cmd->begin(), "begin cube-as-array sample", totals);
    take(cmd->transition(sampleOut, gfx::ResourceState::RenderTarget), "cube-as-array RT", totals);
    take(cmd->beginRenderPass(sampleOut, {0, 0, 0, 1}), "begin cube-as-array pass", totals);
    take(cmd->setPipeline(sampleArrayPipe), "set array pipe for cube view", totals);
    take(cmd->bindResources({{{0, 0, nullptr, srvAsArray}}, {{1, 0, cb, slot * 256ull, 256}}}), "bind cube-as-array view", totals);
    take(cmd->draw(3), "draw cube-as-array", totals);
    take(cmd->endRenderPass(), "end cube-as-array pass", totals);
    take(cmd->transition(sampleOut, gfx::ResourceState::CopySource), "cube-as-array CopySrc", totals);
    take(cmd->copyTextureToBuffer(sampleOut, sampleRead), "copy cube-as-array", totals);
    take(cmd->end(), "end cube-as-array", totals);
    auto af = take(device.queue().submit(*cmd), "submit cube-as-array", totals);
    take(device.queue().wait(af), "wait cube-as-array", totals);
    const auto arrBytes = take(sampleRead->read(0, sampleOut->footprint().size), "read cube-as-array", totals);
    verifyUniformPixels(arrBytes, 8, 8, sampleOut->footprint().rowPitch, renderedFace3Mip0, 1, "TextureCube sampled as Texture2DArray", totals);
    ++totals.sampledPassesVerified;
}

void runTexture3D(gfx::Device& device, const gfx::PipelinePtr& sample3dPipe, Totals& totals) {
    constexpr std::uint32_t w0 = 8, h0 = 8, d0 = 4;
    constexpr std::uint32_t w1 = 4, h1 = 4, d1 = 2;
    const std::array<RGBA, d0> mip0Init{{
        {230, 30, 30, 255},
        {30, 230, 30, 255},
        {30, 30, 230, 255},
        {230, 230, 30, 255}
    }};
    const RGBA slice2Patch{180, 70, 240, 255};
    const std::array<RGBA, d1> mip1Colors{{
        {30, 210, 210, 255},
        {210, 90, 40, 255}
    }};

    auto vol = take(device.createTexture({
        w0, h0, gfx::Format::RGBA8UNorm, false, false, false, 1,
        gfx::TextureDimension::Texture3D, d0, 2, 1
    }), "create Texture3D", totals);

    const auto fp0 = take(gfx::computeTextureRegionFootprint(gfx::Format::RGBA8UNorm, w0, h0), "3D mip0 slice fp", totals);
    const auto fp1 = take(gfx::computeTextureRegionFootprint(gfx::Format::RGBA8UNorm, w1, h1), "3D mip1 slice fp", totals);
    const std::uint64_t mip0Bytes = fp0.size * d0;
    const std::uint64_t patchOffset = (mip0Bytes + 511u) & ~std::uint64_t{511};
    const std::uint64_t mip1Offset = (patchOffset + fp0.size + 511u) & ~std::uint64_t{511};
    const std::uint64_t totalBufSize = mip1Offset + fp1.size * d1 + 512u;

    auto upload = take(device.createBuffer({totalBufSize, gfx::Memory::Upload}), "create 3D upload", totals);
    auto readback = take(device.createBuffer({totalBufSize, gfx::Memory::Readback}), "create 3D readback", totals);
    auto cb = take(device.createBuffer({2048, gfx::Memory::Upload}), "create 3D cb", totals);
    auto sampleOut = take(device.createTexture({8, 8, gfx::Format::RGBA8UNorm, true}), "create 3D sampleOut", totals);
    auto sampleRead = take(device.createBuffer({sampleOut->footprint().size, gfx::Memory::Readback}), "create 3D sampleRead", totals);

    for (std::uint32_t z = 0; z < d0; ++z) {
        const auto slice = makeSolidSliceBytes(w0, h0, fp0.rowPitch, mip0Init[z]);
        take(upload->write(static_cast<std::uint64_t>(z) * fp0.size, slice), "write 3D mip0 slice", totals);
    }
    take(upload->write(patchOffset, makeSolidSliceBytes(w0, h0, fp0.rowPitch, slice2Patch)), "write 3D slice2 patch", totals);
    for (std::uint32_t z = 0; z < d1; ++z) {
        const auto slice = makeSolidSliceBytes(w1, h1, fp1.rowPitch, mip1Colors[z]);
        take(upload->write(mip1Offset + static_cast<std::uint64_t>(z) * fp1.size, slice), "write 3D mip1 slice", totals);
    }

    auto cmd = take(device.createCommandList(), "create 3D cmd", totals);
    take(cmd->begin(), "begin 3D upload+readback", totals);
    take(cmd->transition(cb, gfx::ResourceState::ConstantBuffer), "3D cb state", totals);
    take(cmd->transition(vol, gfx::ResourceState::CopyDestination), "3D CopyDst", totals);
    // Full-volume mip 0 copy, then single-slice z=2 patch, then full-volume mip 1 copy
    take(cmd->copyBufferToTextureRegion(upload, vol, {0, 0, 0, 0, w0, h0, 0, 0, 0, d0}), "upload 3D mip0", totals);
    take(cmd->copyBufferToTextureRegion(upload, vol, {patchOffset, 0, 0, 0, w0, h0, 0, 0, 2, 1}), "upload 3D z=2 patch", totals);
    take(cmd->copyBufferToTextureRegion(upload, vol, {mip1Offset, 0, 0, 0, w1, h1, 1, 0, 0, d1}), "upload 3D mip1", totals);

    take(cmd->transition(vol, gfx::ResourceState::CopySource), "3D CopySrc", totals);
    take(cmd->copyTextureToBufferRegion(vol, readback, {0, 0, 0, 0, w0, h0, 0, 0, 0, d0}), "readback 3D mip0", totals);
    take(cmd->copyTextureToBufferRegion(vol, readback, {patchOffset, 0, 0, 0, w0, h0, 0, 0, 2, 1}), "readback 3D z=2 slice", totals);
    take(cmd->copyTextureToBufferRegion(vol, readback, {mip1Offset, 0, 0, 0, w1, h1, 1, 0, 0, d1}), "readback 3D mip1", totals);
    take(cmd->transition(vol, gfx::ResourceState::ShaderRead), "3D ShaderRead", totals);
    take(cmd->end(), "end 3D upload+readback", totals);
    auto f = take(device.queue().submit(*cmd), "submit 3D upload", totals);
    take(device.queue().wait(f), "wait 3D upload", totals);

    std::array<RGBA, d0> expectedMip0 = mip0Init;
    expectedMip0[2] = slice2Patch;
    for (std::uint32_t z = 0; z < d0; ++z) {
        const auto rb = take(readback->read(static_cast<std::uint64_t>(z) * fp0.size, fp0.size), "read 3D mip0 slice", totals);
        verifyUniformPixels(rb, w0, h0, fp0.rowPitch, expectedMip0[z], 0, "Texture3D mip0 direct readback", totals);
        ++totals.subresourcesVerified;
    }
    {
        const auto rb = take(readback->read(patchOffset, fp0.size), "read 3D single slice", totals);
        verifyUniformPixels(rb, w0, h0, fp0.rowPitch, slice2Patch, 0, "Texture3D z=2 direct readback", totals);
        ++totals.subresourcesVerified;
    }
    for (std::uint32_t z = 0; z < d1; ++z) {
        const auto rb = take(readback->read(mip1Offset + static_cast<std::uint64_t>(z) * fp1.size, fp1.size), "read 3D mip1 slice", totals);
        verifyUniformPixels(rb, w1, h1, fp1.rowPitch, mip1Colors[z], 0, "Texture3D mip1 direct readback", totals);
        ++totals.subresourcesVerified;
    }

    // Sample all 4 z-slices at mip 0 and both z-slices at mip 1
    std::uint32_t slot = 0;
    struct SliceQuery { float wCoord; float lod; RGBA expected; };
    std::vector<SliceQuery> queries;
    for (std::uint32_t z = 0; z < d0; ++z) {
        queries.push_back({(static_cast<float>(z) + 0.5f) / static_cast<float>(d0), 0.0f, expectedMip0[z]});
    }
    for (std::uint32_t z = 0; z < d1; ++z) {
        queries.push_back({(static_cast<float>(z) + 0.5f) / static_cast<float>(d1), 1.0f, mip1Colors[z]});
    }
    for (const auto& q : queries) {
        writeCbSlot(cb, slot, {q.wCoord, q.lod, 0.0f, 0.0f}, totals);
        take(cmd->begin(), "begin 3D sample", totals);
        take(cmd->transition(sampleOut, gfx::ResourceState::RenderTarget), "3D sampleOut RT", totals);
        take(cmd->beginRenderPass(sampleOut, {0, 0, 0, 1}), "begin 3D sample pass", totals);
        take(cmd->setPipeline(sample3dPipe), "set 3D sample pipe", totals);
        take(cmd->bindResources({{{0, 0, vol}}, {{1, 0, cb, slot * 256ull, 256}}}), "bind 3D vol", totals);
        take(cmd->draw(3), "draw 3D sample", totals);
        take(cmd->endRenderPass(), "end 3D sample pass", totals);
        take(cmd->transition(sampleOut, gfx::ResourceState::CopySource), "3D sampleOut CopySrc", totals);
        take(cmd->copyTextureToBuffer(sampleOut, sampleRead), "copy 3D sampleOut", totals);
        take(cmd->end(), "end 3D sample", totals);
        auto sf = take(device.queue().submit(*cmd), "submit 3D sample", totals);
        take(device.queue().wait(sf), "wait 3D sample", totals);

        const auto outBytes = take(sampleRead->read(0, sampleOut->footprint().size), "read 3D sample", totals);
        verifyUniformPixels(outBytes, 8, 8, sampleOut->footprint().rowPitch, q.expected, 1, "Texture3D sampled slice", totals);
        ++totals.sampledPassesVerified;
        ++slot;
    }
}

void runSuite(gfx::Device& device, Totals& totals) {
    {
        auto solidPipe = makeSolidPipeline(device, totals);
        auto sample1dPipe = makeSamplePipeline(device, gfx::TextureDimension::Texture1D, totals);
        auto sample2dPipe = makeSamplePipeline(device, gfx::TextureDimension::Texture2D, totals);
        auto sampleArrayPipe = makeSamplePipeline(device, gfx::TextureDimension::Texture2DArray, totals);
        auto sampleCubePipe = makeSamplePipeline(device, gfx::TextureDimension::TextureCube, totals);
        auto sample3dPipe = makeSamplePipeline(device, gfx::TextureDimension::Texture3D, totals);

        runRejectionContracts(device, totals);
        runMultiMip2D(device, sample2dPipe, solidPipe, totals);
        runTexture1D(device, sample1dPipe, totals);
        runTexture2DArray(device, sampleArrayPipe, solidPipe, totals);
        runTextureCube(device, sampleCubePipe, sampleArrayPipe, solidPipe, totals);
        runTexture3D(device, sample3dPipe, totals);
    }
    verifyReclaimed(device, totals);
}
} // namespace

int main(int argc, char** argv) try {
    gfx::Backend backend = gfx::Backend::Vulkan;
    bool validation = false;
    bool warp = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--d3d12") backend = gfx::Backend::D3D12;
        else if (arg == "--vulkan") backend = gfx::Backend::Vulkan;
        else if (arg == "--validation") validation = true;
        else if (arg == "--warp") warp = true;
        else throw std::runtime_error("Unknown argument: " + std::string(arg));
    }

    Totals totals{};
    auto created = gfx::createDevice({backend, validation, warp, warp});
    if (!created) {
        if (created.error().code == gfx::ErrorCode::Unsupported) {
            std::cout << "SKIP: " << created.error().message << '\n';
            return 77;
        }
        throw std::runtime_error("createDevice failed: " + created.error().message);
    }
    auto device = std::move(*created);
    runSuite(*device, totals);

    if (warp) {
        take(device->removeForTesting(), "WARP removeForTesting", totals);
        reject(device->createTexture({8, 8, gfx::Format::RGBA8UNorm, false, false, false, 1, gfx::TextureDimension::Texture3D, 2, 1, 1}),
               gfx::ErrorCode::DeviceLost, "Lost device rejects Texture3D creation", totals);
        auto recovered = take(device->recover(), "WARP recover", totals);
        {
            auto sample3dPipe = makeSamplePipeline(*recovered, gfx::TextureDimension::Texture3D, totals);
            runTexture3D(*recovered, sample3dPipe, totals);
        }
        verifyReclaimed(*recovered, totals);
    }

    std::uint64_t valErrors = 0;
    if (validation) {
        valErrors = take(device->validationErrorCount(), "validationErrorCount", totals);
        check(valErrors == 0, "Validation error count must be zero", totals);
    }

    std::cout << "PASS: backend=" << (backend == gfx::Backend::D3D12 ? "D3D12" : "Vulkan")
              << " adapter=" << device->caps().adapterName
              << " validation=" << (validation ? "on" : "off")
              << " warp=" << (warp ? "on" : "off")
              << " assertions=" << totals.assertions
              << " rejections=" << totals.rejections
              << " subresources=" << totals.subresourcesVerified
              << " sampled_passes=" << totals.sampledPassesVerified
              << " val_errors=" << valErrors << '\n';
    return 0;
} catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
}
