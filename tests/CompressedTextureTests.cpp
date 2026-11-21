#include <graphics/Graphics.hpp>
#include "fixtures/BindingFixtures.hpp"

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
    std::uint64_t supportedFormatsTested{};
    std::uint64_t unsupportedFormatsRejected{};
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

const char* formatName(gfx::Format f) {
    switch (f) {
    case gfx::Format::BC1UNorm: return "BC1UNorm";
    case gfx::Format::BC2UNorm: return "BC2UNorm";
    case gfx::Format::BC3UNorm: return "BC3UNorm";
    case gfx::Format::BC4UNorm: return "BC4UNorm";
    case gfx::Format::BC5UNorm: return "BC5UNorm";
    case gfx::Format::BC6HUFloat: return "BC6HUFloat";
    case gfx::Format::BC7UNorm: return "BC7UNorm";
    case gfx::Format::ETC2RGB8UNorm: return "ETC2RGB8UNorm";
    case gfx::Format::ETC2RGBA8UNorm: return "ETC2RGBA8UNorm";
    case gfx::Format::EACR11UNorm: return "EACR11UNorm";
    case gfx::Format::EACRG11UNorm: return "EACRG11UNorm";
    case gfx::Format::ASTC4x4UNorm: return "ASTC4x4UNorm";
    case gfx::Format::ASTC6x6UNorm: return "ASTC6x6UNorm";
    case gfx::Format::ASTC8x8UNorm: return "ASTC8x8UNorm";
    default: return "Other";
    }
}

void writeBits(std::uint8_t* dst, std::uint32_t bitOffset, std::uint32_t bitCount, std::uint32_t value) {
    for (std::uint32_t i = 0; i < bitCount; ++i) {
        if ((value >> i) & 1u) {
            const std::uint32_t pos = bitOffset + i;
            dst[pos / 8u] |= static_cast<std::uint8_t>(1u << (pos % 8u));
        }
    }
}

struct EncodedBlock {
    std::vector<std::byte> bytes;
    RGBA expected;
    int tolerance{2};
};

std::uint16_t packRgb565(std::uint8_t r5, std::uint8_t g6, std::uint8_t b5) {
    return static_cast<std::uint16_t>((r5 << 11) | (g6 << 5) | b5);
}

EncodedBlock makeConstantBlock(gfx::Format format, int variant) {
    std::array<std::uint8_t, 16> raw{};
    EncodedBlock out{};
    out.tolerance = 2;

    auto setBc1Color = [&](std::uint8_t* p, std::uint16_t c0) {
        p[0] = static_cast<std::uint8_t>(c0 & 0xffu);
        p[1] = static_cast<std::uint8_t>((c0 >> 8) & 0xffu);
        p[2] = 0; p[3] = 0; // c0 > c1 => 4-color opaque mode, index 0 selects c0
        p[4] = p[5] = p[6] = p[7] = 0;
    };

    const std::array<std::tuple<std::uint16_t, RGBA>, 3> bc1Presets{{
        {packRgb565(31, 0, 0),   {255, 0, 0, 255}},
        {packRgb565(0, 63, 0),   {0, 255, 0, 255}},
        {packRgb565(0, 0, 31),   {0, 0, 255, 255}}
    }};
    const auto [bc1Code, bc1Rgba] = bc1Presets[static_cast<std::size_t>(variant % 3)];

    switch (format) {
    case gfx::Format::BC1UNorm:
        setBc1Color(raw.data(), bc1Code);
        out.bytes.assign(reinterpret_cast<const std::byte*>(raw.data()), reinterpret_cast<const std::byte*>(raw.data()) + 8);
        out.expected = bc1Rgba;
        return out;

    case gfx::Format::BC2UNorm: {
        const std::uint8_t aNib = variant % 3 == 0 ? 0xFu : variant % 3 == 1 ? 0x8u : 0xCu;
        const std::uint8_t aByte = static_cast<std::uint8_t>((aNib << 4) | aNib);
        for (int i = 0; i < 8; ++i) raw[i] = aByte;
        setBc1Color(raw.data() + 8, bc1Code);
        out.bytes.assign(reinterpret_cast<const std::byte*>(raw.data()), reinterpret_cast<const std::byte*>(raw.data()) + 16);
        out.expected = {bc1Rgba[0], bc1Rgba[1], bc1Rgba[2], static_cast<std::uint8_t>((aNib * 255u + 7u) / 15u)};
        return out;
    }

    case gfx::Format::BC3UNorm: {
        const std::uint8_t a0 = variant % 3 == 0 ? 224u : variant % 3 == 1 ? 128u : 192u;
        raw[0] = a0; raw[1] = 0; // a0 > a1, all 3-bit indices = 0 => a0
        setBc1Color(raw.data() + 8, bc1Code);
        out.bytes.assign(reinterpret_cast<const std::byte*>(raw.data()), reinterpret_cast<const std::byte*>(raw.data()) + 16);
        out.expected = {bc1Rgba[0], bc1Rgba[1], bc1Rgba[2], a0};
        return out;
    }

    case gfx::Format::BC4UNorm: {
        const std::uint8_t r0 = variant % 3 == 0 ? 200u : variant % 3 == 1 ? 80u : 240u;
        raw[0] = r0; raw[1] = 0;
        out.bytes.assign(reinterpret_cast<const std::byte*>(raw.data()), reinterpret_cast<const std::byte*>(raw.data()) + 8);
        out.expected = {r0, 0, 0, 255};
        return out;
    }

    case gfx::Format::BC5UNorm: {
        const std::uint8_t r0 = variant % 3 == 0 ? 210u : variant % 3 == 1 ? 60u : 180u;
        const std::uint8_t g0 = variant % 3 == 0 ? 90u : variant % 3 == 1 ? 220u : 140u;
        raw[0] = r0; raw[1] = 0;
        raw[8] = g0; raw[9] = 0;
        out.bytes.assign(reinterpret_cast<const std::byte*>(raw.data()), reinterpret_cast<const std::byte*>(raw.data()) + 16);
        out.expected = {r0, g0, 0, 255};
        return out;
    }

    case gfx::Format::BC6HUFloat: {
        // Mode 11 (5-bit mode 0b00011): 1 subset, 10-bit untransformed unsigned endpoints, all indices 0 => endpoint 0
        // u=496 => ~1.03 (clamps to 255), u=448 => ~0.394 (~101), u=0 => 0.0 (0)
        const std::uint32_t rw = variant % 3 == 0 ? 496u : variant % 3 == 1 ? 0u : 448u;
        const std::uint32_t gw = variant % 3 == 0 ? 448u : variant % 3 == 1 ? 496u : 0u;
        const std::uint32_t bw = variant % 3 == 0 ? 0u : variant % 3 == 1 ? 448u : 496u;
        writeBits(raw.data(), 0, 5, 0x03u);
        writeBits(raw.data(), 5, 10, rw);
        writeBits(raw.data(), 15, 10, gw);
        writeBits(raw.data(), 25, 10, bw);
        writeBits(raw.data(), 35, 10, rw);
        writeBits(raw.data(), 45, 10, gw);
        writeBits(raw.data(), 55, 10, bw);
        out.bytes.assign(reinterpret_cast<const std::byte*>(raw.data()), reinterpret_cast<const std::byte*>(raw.data()) + 16);
        out.expected = variant % 3 == 0 ? RGBA{255, 101, 0, 255}
                     : variant % 3 == 1 ? RGBA{0, 255, 101, 255}
                     : RGBA{101, 0, 255, 255};
        out.tolerance = 4;
        return out;
    }

    case gfx::Format::BC7UNorm: {
        // Mode 6 (7-bit prefix 0b1000000): 1 subset, 7-bit RGBA + 1 P-bit => exact 8-bit RGBA
        const std::array<RGBA, 6> palette{{
            {200, 100, 240, 180},
            {40, 220, 80, 250},
            {240, 180, 30, 200},
            {30, 120, 230, 220},
            {220, 50, 140, 160},
            {90, 210, 210, 240}
        }};
        const RGBA c = palette[static_cast<std::size_t>(variant % 6)];
        const std::uint32_t r7 = c[0] >> 1, g7 = c[1] >> 1, b7 = c[2] >> 1, a7 = c[3] >> 1;
        writeBits(raw.data(), 0, 7, 0x40u);
        writeBits(raw.data(), 7, 7, r7);
        writeBits(raw.data(), 14, 7, r7);
        writeBits(raw.data(), 21, 7, g7);
        writeBits(raw.data(), 28, 7, g7);
        writeBits(raw.data(), 35, 7, b7);
        writeBits(raw.data(), 42, 7, b7);
        writeBits(raw.data(), 49, 7, a7);
        writeBits(raw.data(), 56, 7, a7);
        // P0 = 0, P1 = 0, indices = 0
        out.bytes.assign(reinterpret_cast<const std::byte*>(raw.data()), reinterpret_cast<const std::byte*>(raw.data()) + 16);
        out.expected = {static_cast<std::uint8_t>(r7 << 1), static_cast<std::uint8_t>(g7 << 1),
                        static_cast<std::uint8_t>(b7 << 1), static_cast<std::uint8_t>(a7 << 1)};
        return out;
    }

    case gfx::Format::ETC2RGB8UNorm:
    case gfx::Format::ETC2RGBA8UNorm: {
        // ETC2 differential mode (diff=1, flip=0, dR=dG=dB=0, tbl=0, indices=0 => base + 2)
        const std::uint8_t r5 = variant % 3 == 0 ? 24u : variant % 3 == 1 ? 8u : 16u;
        const std::uint8_t g5 = variant % 3 == 0 ? 8u : variant % 3 == 1 ? 24u : 12u;
        const std::uint8_t b5 = variant % 3 == 0 ? 16u : variant % 3 == 1 ? 10u : 24u;
        auto expand5 = [](std::uint8_t v) -> std::uint8_t {
            return static_cast<std::uint8_t>(std::min<int>(255, ((v << 3) | (v >> 2)) + 2));
        };
        const std::size_t rgbOff = (format == gfx::Format::ETC2RGBA8UNorm) ? 8u : 0u;
        if (format == gfx::Format::ETC2RGBA8UNorm) {
            raw[0] = 200u; // EAC alpha base=200, mult=0, tbl=0, indices=0
        }
        raw[rgbOff + 0] = static_cast<std::uint8_t>(r5 << 3);
        raw[rgbOff + 1] = static_cast<std::uint8_t>(g5 << 3);
        raw[rgbOff + 2] = static_cast<std::uint8_t>(b5 << 3);
        raw[rgbOff + 3] = 0x02u; // diff=1, flip=0, cw1=0, cw2=0
        const std::size_t sz = (format == gfx::Format::ETC2RGBA8UNorm) ? 16u : 8u;
        out.bytes.assign(reinterpret_cast<const std::byte*>(raw.data()), reinterpret_cast<const std::byte*>(raw.data()) + sz);
        out.expected = {expand5(r5), expand5(g5), expand5(b5), format == gfx::Format::ETC2RGBA8UNorm ? std::uint8_t{200} : std::uint8_t{255}};
        out.tolerance = 3;
        return out;
    }

    case gfx::Format::EACR11UNorm: {
        const std::uint8_t base = variant % 3 == 0 ? 192u : variant % 3 == 1 ? 96u : 224u;
        raw[0] = base; // mult=0, tbl=0, indices=0 => base*8+4 / 2047
        out.bytes.assign(reinterpret_cast<const std::byte*>(raw.data()), reinterpret_cast<const std::byte*>(raw.data()) + 8);
        out.expected = {base, 0, 0, 255};
        out.tolerance = 3;
        return out;
    }

    case gfx::Format::EACRG11UNorm: {
        const std::uint8_t rBase = variant % 3 == 0 ? 192u : variant % 3 == 1 ? 80u : 224u;
        const std::uint8_t gBase = variant % 3 == 0 ? 96u : variant % 3 == 1 ? 208u : 128u;
        raw[0] = rBase;
        raw[8] = gBase;
        out.bytes.assign(reinterpret_cast<const std::byte*>(raw.data()), reinterpret_cast<const std::byte*>(raw.data()) + 16);
        out.expected = {rBase, gBase, 0, 255};
        out.tolerance = 3;
        return out;
    }

    case gfx::Format::ASTC4x4UNorm:
    case gfx::Format::ASTC6x6UNorm:
    case gfx::Format::ASTC8x8UNorm: {
        // 2D LDR void-extent constant-color block (valid for all ASTC block footprints)
        raw[0] = 0xFCu; raw[1] = 0xFDu;
        for (int i = 2; i < 8; ++i) raw[i] = 0xFFu;
        const std::array<RGBA, 3> cols{{{192, 64, 128, 255}, {64, 192, 96, 224}, {128, 96, 224, 192}}};
        const RGBA c = cols[static_cast<std::size_t>(variant % 3)];
        for (int ch = 0; ch < 4; ++ch) {
            const std::uint16_t u16 = static_cast<std::uint16_t>((static_cast<std::uint16_t>(c[ch]) << 8) | c[ch]);
            raw[8 + ch * 2] = static_cast<std::uint8_t>(u16 & 0xffu);
            raw[9 + ch * 2] = static_cast<std::uint8_t>(u16 >> 8);
        }
        out.bytes.assign(reinterpret_cast<const std::byte*>(raw.data()), reinterpret_cast<const std::byte*>(raw.data()) + 16);
        out.expected = c;
        return out;
    }

    default:
        throw std::runtime_error("Unhandled format in makeConstantBlock");
    }
}

gfx::ShaderBinary spirvBinary(gfx::ShaderStage stage, std::span<const std::uint32_t> words) {
    const auto raw = std::as_bytes(words);
    return {stage, std::vector<std::byte>(raw.begin(), raw.end()), gfx::ShaderFormat::SPIRV};
}

std::vector<std::uint32_t> buildSampleFragmentSpirv(gfx::TextureDimension dim) {
    std::vector<std::uint32_t> w;
    auto emit = [&](std::uint16_t op, std::initializer_list<std::uint32_t> args) {
        w.push_back((static_cast<std::uint32_t>(args.size() + 1) << 16) | op);
        for (std::uint32_t v : args) w.push_back(v);
    };
    const std::uint32_t spvDim = dim == gfx::TextureDimension::TextureCube ? 3u : 1u;
    const std::uint32_t spvArrayed = dim == gfx::TextureDimension::Texture2DArray ? 1u : 0u;
    w.push_back(0x07230203u); w.push_back(0x00010300u); w.push_back(0x00070000u); w.push_back(38u); w.push_back(0u);
    emit(17, {1});
    emit(14, {0, 1});
    emit(15, {4, 1, 0x6e69616du, 0u, 2, 3});
    emit(16, {1, 7});
    emit(71, {2, 30, 0}); emit(71, {3, 30, 0});
    emit(71, {4, 34, 0}); emit(71, {4, 33, 0});
    emit(71, {5, 2}); emit(72, {5, 0, 35, 0});
    emit(71, {6, 34, 0}); emit(71, {6, 33, 1});
    emit(71, {7, 34, 0}); emit(71, {7, 33, 32});
    emit(19, {8}); emit(33, {9, 8}); emit(22, {10, 32});
    emit(23, {11, 10, 2}); emit(23, {12, 10, 3}); emit(23, {13, 10, 4});
    emit(21, {14, 32, 1}); emit(43, {14, 15, 0});
    emit(32, {16, 1, 11}); emit(32, {17, 3, 13});
    emit(25, {18, 10, spvDim, 0, spvArrayed, 0, 1, 0});
    emit(32, {19, 0, 18}); emit(26, {20}); emit(32, {21, 0, 20}); emit(27, {22, 18});
    emit(30, {5, 13}); emit(32, {23, 2, 5}); emit(32, {24, 2, 13});
    emit(59, {16, 2, 1}); emit(59, {17, 3, 3}); emit(59, {19, 4, 0}); emit(59, {23, 6, 2}); emit(59, {21, 7, 0});
    emit(54, {8, 1, 0, 9}); emit(248, {25});
    emit(61, {11, 26, 2}); emit(65, {24, 27, 6, 15}); emit(61, {13, 28, 27});
    emit(61, {18, 29, 4}); emit(61, {20, 30, 7}); emit(86, {22, 31, 29, 30});
    std::uint32_t coordId = 26, lodId = 33;
    if (dim == gfx::TextureDimension::Texture2D) {
        emit(81, {10, 33, 28, 0});
    } else if (dim == gfx::TextureDimension::Texture2DArray) {
        emit(81, {10, 32, 26, 0}); emit(81, {10, 33, 26, 1}); emit(81, {10, 34, 28, 0});
        emit(80, {12, 35, 32, 33, 34}); emit(81, {10, 36, 28, 1});
        coordId = 35; lodId = 36;
    } else {
        emit(81, {10, 32, 28, 0}); emit(81, {10, 33, 28, 1}); emit(81, {10, 34, 28, 2});
        emit(80, {12, 35, 32, 33, 34}); emit(81, {10, 36, 28, 3});
        coordId = 35; lodId = 36;
    }
    emit(88, {13, 37, 31, coordId, 2, lodId});
    emit(62, {3, 37}); emit(253, {}); emit(56, {});
    return w;
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
        std::string_view psHlsl = dim == gfx::TextureDimension::Texture2D
            ? R"(Texture2D<float4> tex : register(t0); cbuffer Params : register(b0) { float4 params; }; SamplerState samp : register(s0);
                 float4 PS(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 { return tex.SampleLevel(samp, uv, params.x); })"
            : dim == gfx::TextureDimension::Texture2DArray
            ? R"(Texture2DArray<float4> tex : register(t0); cbuffer Params : register(b0) { float4 params; }; SamplerState samp : register(s0);
                 float4 PS(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 { return tex.SampleLevel(samp, float3(uv, params.x), params.y); })"
            : R"(TextureCube<float4> tex : register(t0); cbuffer Params : register(b0) { float4 params; }; SamplerState samp : register(s0);
                 float4 PS(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 { return tex.SampleLevel(samp, params.xyz, params.w); })";
        desc.vertex = take(gfx::compileHlsl(vsHlsl, "VS", gfx::ShaderStage::Vertex), "compile VS", totals);
        desc.fragment = take(gfx::compileHlsl(psHlsl, "PS", gfx::ShaderStage::Fragment), "compile PS", totals);
    } else {
        desc.vertex = spirvBinary(gfx::ShaderStage::Vertex, bindingFixtures::uv);
        const auto fragWords = buildSampleFragmentSpirv(dim);
        desc.fragment = spirvBinary(gfx::ShaderStage::Fragment, fragWords);
    }
    const auto btype = dim == gfx::TextureDimension::Texture2D ? gfx::BindingType::Texture2D
                     : dim == gfx::TextureDimension::Texture2DArray ? gfx::BindingType::Texture2DArray
                     : gfx::BindingType::TextureCube;
    desc.colorFormat = gfx::Format::RGBA8UNorm;
    desc.bindingLayout = {
        {{btype, 0, 1, 0, gfx::ShaderVisibility::Fragment},
         {gfx::BindingType::ConstantBuffer, 0, 1, 0, gfx::ShaderVisibility::Fragment}},
        {{0, 0, gfx::ShaderVisibility::Fragment, gfx::SamplerFilter::Point, gfx::SamplerAddress::Clamp}}
    };
    return take(device.createGraphicsPipeline(desc), "create sample pipeline", totals);
}

std::vector<std::byte> tileBlockSlice(const std::vector<std::byte>& block, std::uint32_t blocksX, std::uint32_t blocksY, std::uint32_t rowPitch) {
    std::vector<std::byte> buf(static_cast<std::size_t>(rowPitch) * blocksY, std::byte{0xa5});
    for (std::uint32_t by = 0; by < blocksY; ++by) {
        for (std::uint32_t bx = 0; bx < blocksX; ++bx) {
            std::memcpy(buf.data() + static_cast<std::size_t>(by) * rowPitch + static_cast<std::size_t>(bx) * block.size(),
                        block.data(), block.size());
        }
    }
    return buf;
}

void verifyUniformPixels(
    const std::vector<std::byte>& bytes,
    std::uint32_t w,
    std::uint32_t h,
    std::uint32_t rowPitch,
    RGBA expected,
    int tolerance,
    const std::string& label,
    Totals& totals) {
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::size_t off = static_cast<std::size_t>(y) * rowPitch + static_cast<std::size_t>(x) * 4u;
            for (std::size_t c = 0; c < 4; ++c) {
                const int got = std::to_integer<int>(bytes[off + c]);
                if (std::abs(got - static_cast<int>(expected[c])) > tolerance) {
                    throw std::runtime_error(label + " mismatch at (" + std::to_string(x) + "," + std::to_string(y) +
                        ") ch=" + std::to_string(c) + " got=" + std::to_string(got) +
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

void runCpuContracts(Totals& totals) {
    using F = gfx::Format;
    using Dim = gfx::TextureDimension;

    const auto bc1 = take(gfx::queryFormatBlockInfo(F::BC1UNorm), "BC1 block info", totals);
    check(bc1 == gfx::FormatBlockInfo{4, 4, 8, true}, "BC1 is 4x4 8B", totals);
    const auto bc7 = take(gfx::queryFormatBlockInfo(F::BC7UNorm), "BC7 block info", totals);
    check(bc7 == gfx::FormatBlockInfo{4, 4, 16, true}, "BC7 is 4x4 16B", totals);
    const auto astc6 = take(gfx::queryFormatBlockInfo(F::ASTC6x6UNorm), "ASTC6x6 block info", totals);
    check(astc6 == gfx::FormatBlockInfo{6, 6, 16, true}, "ASTC6x6 is 6x6 16B", totals);
    const auto astc8 = take(gfx::queryFormatBlockInfo(F::ASTC8x8UNorm), "ASTC8x8 block info", totals);
    check(astc8 == gfx::FormatBlockInfo{8, 8, 16, true}, "ASTC8x8 is 8x8 16B", totals);
    reject(gfx::queryFormatBlockInfo(static_cast<F>(99)), gfx::ErrorCode::InvalidArgument, "Unknown format block info", totals);

    // Block footprint alignment (including sub-block tail mips 2x2 and 1x1)
    const auto fpBc1_16 = take(gfx::computeTextureRegionFootprint(F::BC1UNorm, 16, 16), "BC1 16x16 fp", totals);
    check(fpBc1_16.rowBytes == 32 && fpBc1_16.rowPitch == 256 && fpBc1_16.height == 4 && fpBc1_16.size == 1024, "BC1 16x16 footprint", totals);
    const auto fpBc7_2 = take(gfx::computeTextureRegionFootprint(F::BC7UNorm, 2, 2), "BC7 2x2 tail mip fp", totals);
    check(fpBc7_2.rowBytes == 16 && fpBc7_2.rowPitch == 256 && fpBc7_2.height == 1 && fpBc7_2.size == 256, "BC7 2x2 tail mip footprint", totals);
    const auto fpAstc6_12 = take(gfx::computeTextureRegionFootprint(F::ASTC6x6UNorm, 12, 18), "ASTC6x6 12x18 fp", totals);
    check(fpAstc6_12.rowBytes == 32 && fpAstc6_12.rowPitch == 256 && fpAstc6_12.height == 3 && fpAstc6_12.size == 768, "ASTC6x6 12x18 footprint", totals);
    const auto fpBc7_custom = take(gfx::computeTextureRegionFootprint(F::BC7UNorm, 128, 8, 512), "BC7 128x8 custom pitch", totals);
    check(fpBc7_custom.rowBytes == 512 && fpBc7_custom.rowPitch == 512 && fpBc7_custom.height == 2 && fpBc7_custom.size == 1024, "BC7 custom pitch", totals);
    reject(gfx::computeTextureRegionFootprint(F::BC7UNorm, 128, 8, 256), gfx::ErrorCode::InvalidArgument, "BC7 pitch < rowBytes", totals);

    // Description validation & unsupported combinations
    take(gfx::validateTextureDescription({16, 16, F::BC1UNorm, false, false, false, 1, Dim::Texture2D, 1, 5, 1}), "valid BC1 16x16 5-mip", totals);
    take(gfx::validateTextureDescription({12, 12, F::ASTC6x6UNorm, false, false, false, 1, Dim::Texture2D, 1, 2, 1}), "valid ASTC6x6 12x12", totals);
    take(gfx::validateTextureDescription({16, 16, F::BC7UNorm, false, false, false, 1, Dim::TextureCube, 1, 3, 6}), "valid BC7 Cube", totals);

    reject(gfx::validateTextureDescription({6, 8, F::BC1UNorm}), gfx::ErrorCode::InvalidArgument, "BC1 non-4x-multiple mip0 width", totals);
    reject(gfx::validateTextureDescription({8, 10, F::BC7UNorm}), gfx::ErrorCode::InvalidArgument, "BC7 non-4x-multiple mip0 height", totals);
    reject(gfx::validateTextureDescription({8, 8, F::ASTC6x6UNorm}), gfx::ErrorCode::InvalidArgument, "ASTC6x6 non-6x-multiple mip0", totals);
    reject(gfx::validateTextureDescription({12, 12, F::ASTC8x8UNorm}), gfx::ErrorCode::InvalidArgument, "ASTC8x8 non-8x-multiple mip0", totals);
    reject(gfx::validateTextureDescription({16, 16, F::BC1UNorm, true}), gfx::ErrorCode::Unsupported, "Compressed renderTarget unsupported", totals);
    reject(gfx::validateTextureDescription({16, 16, F::BC3UNorm, false, false, true}), gfx::ErrorCode::Unsupported, "Compressed storage unsupported", totals);
    reject(gfx::validateTextureDescription({16, 16, F::BC7UNorm, false, true}), gfx::ErrorCode::InvalidArgument, "Compressed depthStencil invalid", totals);
    reject(gfx::validateTextureDescription({16, 16, F::BC1UNorm, false, false, false, 4}), gfx::ErrorCode::InvalidArgument, "Compressed MSAA invalid", totals);
    reject(gfx::validateTextureDescription({16, 1, F::BC1UNorm, false, false, false, 1, Dim::Texture1D}), gfx::ErrorCode::Unsupported, "Compressed 1D unsupported", totals);
    reject(gfx::validateTextureDescription({8, 8, F::BC7UNorm, false, false, false, 1, Dim::Texture3D, 4}), gfx::ErrorCode::Unsupported, "Compressed 3D unsupported", totals);
    reject(gfx::validateRenderTargets({{F::BC7UNorm}, {}}), gfx::ErrorCode::InvalidArgument, "Compressed format in RenderTargetLayout rejected", totals);
}

void verifySupportedCompressed2D(
    gfx::Device& device,
    gfx::Format format,
    const gfx::PipelinePtr& sample2dPipe,
    Totals& totals) {
    const auto block = take(gfx::queryFormatBlockInfo(format), "block info", totals);
    const std::uint32_t baseSize = block.blockWidth == 6u ? 12u : 16u;
    // 16x16 4x4 => 5 mips (16, 8, 4, 2, 1) including sub-block tail mips!
    // 12x12 6x6 => 2 mips (12, 6); 16x16 8x8 => 2 mips (16, 8)
    const std::uint32_t mipCount = block.blockWidth == 4u ? 5u : 2u;

    auto tex = take(device.createTexture({
        baseSize, baseSize, format, false, false, false, 1,
        gfx::TextureDimension::Texture2D, 1, mipCount, 1
    }), "create compressed 2D texture", totals);

    auto up = take(device.createBuffer({16384, gfx::Memory::Upload}), "create compressed upload", totals);
    auto rb = take(device.createBuffer({16384, gfx::Memory::Readback}), "create compressed readback", totals);
    auto cb = take(device.createBuffer({2048, gfx::Memory::Upload}), "create compressed cb", totals);
    auto outTex = take(device.createTexture({8, 8, gfx::Format::RGBA8UNorm, true}), "create sample out", totals);
    auto outRead = take(device.createBuffer({outTex->footprint().size, gfx::Memory::Readback}), "create sample outRead", totals);

    std::vector<EncodedBlock> mipBlocks;
    std::vector<std::uint64_t> offsets(mipCount);
    std::vector<gfx::TextureFootprint> fps(mipCount);
    std::uint64_t cursor = 0;
    for (std::uint32_t m = 0; m < mipCount; ++m) {
        const std::uint32_t dim = std::max(1u, baseSize >> m);
        fps[m] = take(gfx::computeTextureRegionFootprint(format, dim, dim), "mip fp", totals);
        offsets[m] = cursor;
        mipBlocks.push_back(makeConstantBlock(format, static_cast<int>(m)));
        const std::uint32_t bx = (dim + block.blockWidth - 1u) / block.blockWidth;
        const std::uint32_t by = (dim + block.blockHeight - 1u) / block.blockHeight;
        const auto sliceBytes = tileBlockSlice(mipBlocks[m].bytes, bx, by, fps[m].rowPitch);
        take(up->write(cursor, sliceBytes), "write compressed mip upload", totals);
        cursor = (cursor + fps[m].size + 511u) & ~std::uint64_t{511};
        std::array<float, 4> params{static_cast<float>(m), 0.0f, 0.0f, 0.0f};
        take(cb->write(static_cast<std::uint64_t>(m) * 256u, std::as_bytes(std::span{params})), "write lod cb", totals);
    }

    // Prepare a single block-aligned sub-region patch on mip 0 at (blockWidth, blockHeight)
    const EncodedBlock patchBlock = makeConstantBlock(format, 2);
    const auto patchFp = take(gfx::computeTextureRegionFootprint(format, block.blockWidth, block.blockHeight), "patch fp", totals);
    const std::uint64_t patchOffset = cursor;
    take(up->write(patchOffset, tileBlockSlice(patchBlock.bytes, 1, 1, patchFp.rowPitch)), "write patch block", totals);

    auto cmd = take(device.createCommandList(), "create cmd", totals);
    take(cmd->begin(), "begin upload+readback", totals);
    take(cmd->transition(cb, gfx::ResourceState::ConstantBuffer), "cb state", totals);
    take(cmd->transition(tex, gfx::ResourceState::CopyDestination), "tex CopyDst", totals);

    // Verify unaligned compressed copy origin/extent are rejected cleanly
    if (block.blockWidth > 1u) {
        reject(cmd->copyBufferToTextureRegion(up, tex, {0, 0, 2, 0, block.blockWidth, block.blockHeight, 0, 0, 0, 1}),
               gfx::ErrorCode::InvalidArgument, "Unaligned compressed copy X origin rejected", totals);
        reject(cmd->copyBufferToTextureRegion(up, tex, {0, 0, 0, 0, block.blockWidth + 2u, block.blockHeight, 0, 0, 0, 1}),
               gfx::ErrorCode::InvalidArgument, "Unaligned compressed copy width rejected", totals);
    }

    // Upload all mips
    for (std::uint32_t m = 0; m < mipCount; ++m) {
        const std::uint32_t dim = std::max(1u, baseSize >> m);
        take(cmd->copyBufferToTextureRegion(up, tex, {offsets[m], 0, 0, 0, dim, dim, m, 0, 0, 1}), "upload compressed mip", totals);
    }
    // Partial block-aligned sub-region patch on mip 0 at (bw, bh)
    take(cmd->copyBufferToTextureRegion(up, tex, {patchOffset, 0, block.blockWidth, block.blockHeight, block.blockWidth, block.blockHeight, 0, 0, 0, 1}),
         "patch compressed block", totals);

    // Direct bit-exact readback of all mips
    take(cmd->transition(tex, gfx::ResourceState::CopySource), "tex CopySrc", totals);
    for (std::uint32_t m = 0; m < mipCount; ++m) {
        const std::uint32_t dim = std::max(1u, baseSize >> m);
        take(cmd->copyTextureToBufferRegion(tex, rb, {offsets[m], 0, 0, 0, dim, dim, m, 0, 0, 1}), "readback compressed mip", totals);
    }
    // Overwrite mip 0 back to uniform mipBlocks[0] before sampling every mip cleanly
    take(cmd->transition(tex, gfx::ResourceState::CopyDestination), "tex CopyDst restore", totals);
    take(cmd->copyBufferToTextureRegion(up, tex, {offsets[0], 0, 0, 0, baseSize, baseSize, 0, 0, 0, 1}), "restore mip0", totals);
    take(cmd->transition(tex, gfx::ResourceState::ShaderRead), "tex ShaderRead", totals);
    take(cmd->end(), "end upload+readback", totals);
    auto f = take(device.queue().submit(*cmd), "submit upload+readback", totals);
    take(device.queue().wait(f), "wait upload+readback", totals);

    // Verify bit-exact compressed block bytes on every mip (including patched block at (1,1) on mip 0!)
    for (std::uint32_t m = 0; m < mipCount; ++m) {
        const std::uint32_t dim = std::max(1u, baseSize >> m);
        const std::uint32_t bx = (dim + block.blockWidth - 1u) / block.blockWidth;
        const std::uint32_t by = (dim + block.blockHeight - 1u) / block.blockHeight;
        const auto got = take(rb->read(offsets[m], fps[m].size), "read compressed mip", totals);
        for (std::uint32_t y = 0; y < by; ++y) {
            for (std::uint32_t x = 0; x < bx; ++x) {
                const auto& expectedBlock = (m == 0 && x == 1 && y == 1) ? patchBlock.bytes : mipBlocks[m].bytes;
                const std::size_t off = static_cast<std::size_t>(y) * fps[m].rowPitch + static_cast<std::size_t>(x) * block.bytesPerBlock;
                check(std::memcmp(got.data() + off, expectedBlock.data(), block.bytesPerBlock) == 0,
                      "Compressed block bytes must roundtrip bit-exact", totals);
            }
        }
    }

    // Hardware decompression + sampling verification across all mips (including 2x2 and 1x1 tail mips!)
    for (std::uint32_t m = 0; m < mipCount; ++m) {
        take(cmd->begin(), "begin sample", totals);
        take(cmd->transition(outTex, gfx::ResourceState::RenderTarget), "outTex RT", totals);
        take(cmd->beginRenderPass(outTex, {0, 0, 0, 1}), "begin sample pass", totals);
        take(cmd->setPipeline(sample2dPipe), "set sample2d", totals);
        take(cmd->bindResources({{{0, 0, tex}}, {{1, 0, cb, static_cast<std::uint64_t>(m) * 256u, 256}}}), "bind compressed tex", totals);
        take(cmd->draw(3), "draw sample", totals);
        take(cmd->endRenderPass(), "end sample pass", totals);
        take(cmd->transition(outTex, gfx::ResourceState::CopySource), "outTex CopySrc", totals);
        take(cmd->copyTextureToBuffer(outTex, outRead), "copy outTex", totals);
        take(cmd->end(), "end sample", totals);
        auto sf = take(device.queue().submit(*cmd), "submit sample", totals);
        take(device.queue().wait(sf), "wait sample", totals);

        const auto pixels = take(outRead->read(0, outTex->footprint().size), "read sampled pixels", totals);
        verifyUniformPixels(pixels, 8, 8, outTex->footprint().rowPitch,
            mipBlocks[m].expected, mipBlocks[m].tolerance,
            std::string(formatName(format)) + " mip=" + std::to_string(m), totals);
        ++totals.sampledPassesVerified;
    }
}

void verifyCompressedArrayAndCube(
    gfx::Device& device,
    const gfx::PipelinePtr& sampleArrayPipe,
    const gfx::PipelinePtr& sampleCubePipe,
    Totals& totals) {
    const auto support = take(device.queryFormatSupport(gfx::Format::BC7UNorm), "query BC7 support", totals);
    if (!support.texture2D || !support.sampled) return;

    auto arr = take(device.createTexture({
        8, 8, gfx::Format::BC7UNorm, false, false, false, 1,
        gfx::TextureDimension::Texture2DArray, 1, 2, 2
    }), "create BC7 Texture2DArray", totals);
    auto cube = take(device.createTexture({
        8, 8, gfx::Format::BC7UNorm, false, false, false, 1,
        gfx::TextureDimension::TextureCube, 1, 2, 6
    }), "create BC7 TextureCube", totals);

    auto up = take(device.createBuffer({16384, gfx::Memory::Upload}), "create bc7 arr/cube up", totals);
    auto cb = take(device.createBuffer({4096, gfx::Memory::Upload}), "create bc7 arr/cube cb", totals);
    auto outTex = take(device.createTexture({8, 8, gfx::Format::RGBA8UNorm, true}), "create outTex", totals);
    auto outRead = take(device.createBuffer({outTex->footprint().size, gfx::Memory::Readback}), "create outRead", totals);

    const auto fp8 = take(gfx::computeTextureRegionFootprint(gfx::Format::BC7UNorm, 8, 8), "bc7 8x8 fp", totals);
    const auto fp4 = take(gfx::computeTextureRegionFootprint(gfx::Format::BC7UNorm, 4, 4), "bc7 4x4 fp", totals);

    std::array<EncodedBlock, 6> blocks{{
        makeConstantBlock(gfx::Format::BC7UNorm, 0),
        makeConstantBlock(gfx::Format::BC7UNorm, 1),
        makeConstantBlock(gfx::Format::BC7UNorm, 2),
        makeConstantBlock(gfx::Format::BC7UNorm, 3),
        makeConstantBlock(gfx::Format::BC7UNorm, 4),
        makeConstantBlock(gfx::Format::BC7UNorm, 5)
    }};

    std::uint64_t cursor = 0;
    std::array<std::uint64_t, 6> off8{}, off4{};
    for (std::size_t i = 0; i < 6; ++i) {
        off8[i] = cursor;
        take(up->write(cursor, tileBlockSlice(blocks[i].bytes, 2, 2, fp8.rowPitch)), "write bc7 8x8", totals);
        cursor = (cursor + fp8.size + 511u) & ~std::uint64_t{511};
        off4[i] = cursor;
        take(up->write(cursor, tileBlockSlice(blocks[(i + 3) % 6].bytes, 1, 1, fp4.rowPitch)), "write bc7 4x4", totals);
        cursor = (cursor + fp4.size + 511u) & ~std::uint64_t{511};
    }

    auto cmd = take(device.createCommandList(), "cmd", totals);
    take(cmd->begin(), "begin upload arr+cube", totals);
    take(cmd->transition(cb, gfx::ResourceState::ConstantBuffer), "cb state", totals);
    take(cmd->transition(arr, gfx::ResourceState::CopyDestination), "arr CopyDst", totals);
    take(cmd->transition(cube, gfx::ResourceState::CopyDestination), "cube CopyDst", totals);
    for (std::uint32_t l = 0; l < 2; ++l) {
        take(cmd->copyBufferToTextureRegion(up, arr, {off8[l], 0, 0, 0, 8, 8, 0, l, 0, 1}), "upload arr m0", totals);
        take(cmd->copyBufferToTextureRegion(up, arr, {off4[l], 0, 0, 0, 4, 4, 1, l, 0, 1}), "upload arr m1", totals);
    }
    for (std::uint32_t f = 0; f < 6; ++f) {
        take(cmd->copyBufferToTextureRegion(up, cube, {off8[f], 0, 0, 0, 8, 8, 0, f, 0, 1}), "upload cube m0", totals);
        take(cmd->copyBufferToTextureRegion(up, cube, {off4[f], 0, 0, 0, 4, 4, 1, f, 0, 1}), "upload cube m1", totals);
    }
    take(cmd->transition(arr, gfx::ResourceState::ShaderRead), "arr ShaderRead", totals);
    take(cmd->transition(cube, gfx::ResourceState::ShaderRead), "cube ShaderRead", totals);
    take(cmd->end(), "end upload arr+cube", totals);
    auto uf = take(device.queue().submit(*cmd), "submit upload arr+cube", totals);
    take(device.queue().wait(uf), "wait upload arr+cube", totals);

    // Sample BC7 Texture2DArray layer 1 mip 1
    std::uint32_t slot = 0;
    {
        std::array<float, 4> params{1.0f, 1.0f, 0.0f, 0.0f};
        take(cb->write(slot * 256ull, std::as_bytes(std::span{params})), "write arr params", totals);
        take(cmd->begin(), "begin arr sample", totals);
        take(cmd->transition(outTex, gfx::ResourceState::RenderTarget), "out RT", totals);
        take(cmd->beginRenderPass(outTex, {0, 0, 0, 1}), "pass", totals);
        take(cmd->setPipeline(sampleArrayPipe), "pipe", totals);
        take(cmd->bindResources({{{0, 0, arr}}, {{1, 0, cb, slot * 256ull, 256}}}), "bind", totals);
        take(cmd->draw(3), "draw", totals);
        take(cmd->endRenderPass(), "end pass", totals);
        take(cmd->transition(outTex, gfx::ResourceState::CopySource), "out CopySrc", totals);
        take(cmd->copyTextureToBuffer(outTex, outRead), "copy", totals);
        take(cmd->end(), "end", totals);
        auto sf = take(device.queue().submit(*cmd), "submit", totals);
        take(device.queue().wait(sf), "wait", totals);
        const auto px = take(outRead->read(0, outTex->footprint().size), "read", totals);
        verifyUniformPixels(px, 8, 8, outTex->footprint().rowPitch, blocks[4].expected, 2, "BC7 Texture2DArray L1M1", totals);
        ++totals.sampledPassesVerified;
        ++slot;
    }

    // Sample all 6 faces of BC7 TextureCube at mip 0
    const std::array<std::array<float, 3>, 6> dirs{{
        { 1.0f,  0.0f,  0.0f}, {-1.0f,  0.0f,  0.0f},
        { 0.0f,  1.0f,  0.0f}, { 0.0f, -1.0f,  0.0f},
        { 0.0f,  0.0f,  1.0f}, { 0.0f,  0.0f, -1.0f}
    }};
    for (std::size_t f = 0; f < 6; ++f) {
        std::array<float, 4> params{dirs[f][0], dirs[f][1], dirs[f][2], 0.0f};
        take(cb->write(slot * 256ull, std::as_bytes(std::span{params})), "write cube params", totals);
        take(cmd->begin(), "begin cube sample", totals);
        take(cmd->transition(outTex, gfx::ResourceState::RenderTarget), "out RT", totals);
        take(cmd->beginRenderPass(outTex, {0, 0, 0, 1}), "pass", totals);
        take(cmd->setPipeline(sampleCubePipe), "pipe", totals);
        take(cmd->bindResources({{{0, 0, cube}}, {{1, 0, cb, slot * 256ull, 256}}}), "bind", totals);
        take(cmd->draw(3), "draw", totals);
        take(cmd->endRenderPass(), "end pass", totals);
        take(cmd->transition(outTex, gfx::ResourceState::CopySource), "out CopySrc", totals);
        take(cmd->copyTextureToBuffer(outTex, outRead), "copy", totals);
        take(cmd->end(), "end", totals);
        auto sf = take(device.queue().submit(*cmd), "submit", totals);
        take(device.queue().wait(sf), "wait", totals);
        const auto px = take(outRead->read(0, outTex->footprint().size), "read", totals);
        verifyUniformPixels(px, 8, 8, outTex->footprint().rowPitch, blocks[f].expected, 2,
            "BC7 TextureCube face=" + std::to_string(f), totals);
        ++totals.sampledPassesVerified;
        ++slot;
    }
}

void runDeviceSuite(gfx::Device& device, Totals& totals) {
    constexpr std::array<gfx::Format, 14> allCompressed{
        gfx::Format::BC1UNorm, gfx::Format::BC2UNorm, gfx::Format::BC3UNorm,
        gfx::Format::BC4UNorm, gfx::Format::BC5UNorm, gfx::Format::BC6HUFloat, gfx::Format::BC7UNorm,
        gfx::Format::ETC2RGB8UNorm, gfx::Format::ETC2RGBA8UNorm, gfx::Format::EACR11UNorm, gfx::Format::EACRG11UNorm,
        gfx::Format::ASTC4x4UNorm, gfx::Format::ASTC6x6UNorm, gfx::Format::ASTC8x8UNorm
    };
    {
        auto sample2dPipe = makeSamplePipeline(device, gfx::TextureDimension::Texture2D, totals);
        auto sampleArrayPipe = makeSamplePipeline(device, gfx::TextureDimension::Texture2DArray, totals);
        auto sampleCubePipe = makeSamplePipeline(device, gfx::TextureDimension::TextureCube, totals);

        for (const auto fmt : allCompressed) {
            const auto support = take(device.queryFormatSupport(fmt), "queryFormatSupport compressed", totals);
            check(support.format == fmt, "FormatSupport format matches query", totals);
            check(!support.renderTarget && !support.depthStencil && !support.storage && !support.blendable,
                  "Compressed formats must never advertise RT/DS/storage/blendable", totals);
            const auto block = take(gfx::queryFormatBlockInfo(fmt), "block info", totals);
            const std::uint32_t dim = block.blockWidth == 6u ? 12u : 16u;
            if (!support.texture2D || !support.sampled) {
                check(support.sampleCounts == 0u, "Unsupported compressed format reports sampleCounts=0", totals);
                reject(device.createTexture({dim, dim, fmt}), gfx::ErrorCode::Unsupported,
                       "Unsupported compressed format must fail with Unsupported", totals);
                ++totals.unsupportedFormatsRejected;
            } else {
                check(support.copySource && support.copyDestination && support.partialRegionCopy && support.sampleCounts == 1u,
                      "Supported compressed format must advertise copy/partialRegionCopy/sampleCounts=1", totals);
                verifySupportedCompressed2D(device, fmt, sample2dPipe, totals);
                ++totals.supportedFormatsTested;
            }
        }

        // All Windows D3D12 & desktop Vulkan GPUs must support at least BC1..BC7 (7 formats)
        check(totals.supportedFormatsTested >= 7u, "Expected at least BC1..BC7 supported on desktop GPU/WARP", totals);
        verifyCompressedArrayAndCube(device, sampleArrayPipe, sampleCubePipe, totals);
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
    runCpuContracts(totals);

    std::uint64_t totalValErrors = 0;
    std::string adapterSummary;
    if (warp) {
        auto created = gfx::createDevice({backend, validation, true, true});
        if (!created) {
            if (created.error().code == gfx::ErrorCode::Unsupported) {
                std::cout << "SKIP: " << created.error().message << '\n';
                return 77;
            }
            throw std::runtime_error("createDevice failed: " + created.error().message);
        }
        auto device = std::move(*created);
        adapterSummary = device->caps().adapterName;
        runDeviceSuite(*device, totals);
        take(device->removeForTesting(), "WARP removeForTesting", totals);
        reject(device->createTexture({16, 16, gfx::Format::BC7UNorm}),
               gfx::ErrorCode::DeviceLost, "Lost device rejects BC7 creation", totals);
        auto recovered = take(device->recover(), "WARP recover", totals);
        {
            auto sample2dPipe = makeSamplePipeline(*recovered, gfx::TextureDimension::Texture2D, totals);
            verifySupportedCompressed2D(*recovered, gfx::Format::BC7UNorm, sample2dPipe, totals);
        }
        verifyReclaimed(*recovered, totals);
    } else {
        auto adapters = take(gfx::enumerateAdapters(backend), "enumerateAdapters", totals);
        unsigned testedAdapters = 0;
        for (const auto& info : adapters) {
            if (info.softwareAdapter) continue;
            auto created = gfx::createDevice({backend, validation, false, false, info.id});
            if (!created) {
                if (created.error().code == gfx::ErrorCode::Unsupported) continue;
                throw std::runtime_error("createDevice failed for " + info.name + ": " + created.error().message);
            }
            auto device = std::move(*created);
            const std::uint64_t beforeSup = totals.supportedFormatsTested;
            const std::uint64_t beforeUnsup = totals.unsupportedFormatsRejected;
            runDeviceSuite(*device, totals);
            if (validation) {
                const auto valErrors = take(device->validationErrorCount(), "validationErrorCount", totals);
                check(valErrors == 0, "Validation error count must be zero", totals);
                totalValErrors += valErrors;
            }
            if (!adapterSummary.empty()) adapterSummary += "; ";
            adapterSummary += info.name + "(" +
                std::to_string(totals.supportedFormatsTested - beforeSup) + "sup/" +
                std::to_string(totals.unsupportedFormatsRejected - beforeUnsup) + "unsup)";
            ++testedAdapters;
        }
        if (testedAdapters == 0) {
            std::cout << "SKIP: No hardware adapter available\n";
            return 77;
        }
    }

    std::cout << "PASS: backend=" << (backend == gfx::Backend::D3D12 ? "D3D12" : "Vulkan")
              << " adapters=[" << adapterSummary << "]"
              << " validation=" << (validation ? "on" : "off")
              << " warp=" << (warp ? "on" : "off")
              << " assertions=" << totals.assertions
              << " rejections=" << totals.rejections
              << " supported_formats=" << totals.supportedFormatsTested
              << " unsupported_rejected=" << totals.unsupportedFormatsRejected
              << " sampled_passes=" << totals.sampledPassesVerified
              << " val_errors=" << totalValErrors << '\n';
    return 0;
} catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
}
