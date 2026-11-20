#include <graphics/Graphics.hpp>
#include "fixtures/VertexFixtures.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
unsigned assertions{};
unsigned resolves{};
unsigned images{};
unsigned rejections{};

void check(bool condition, const char* message) {
    ++assertions;
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <class T>
T take(gfx::Result<T> result) {
    if (!result) {
        throw std::runtime_error(
            result.error().message + " (code=" +
            std::to_string(static_cast<int>(result.error().code)) +
            ", native=" + std::to_string(result.error().nativeCode) + ")");
    }
    return std::move(*result);
}

void take(gfx::Result<void> result) {
    if (!result) {
        throw std::runtime_error(
            result.error().message + " (code=" +
            std::to_string(static_cast<int>(result.error().code)) +
            ", native=" + std::to_string(result.error().nativeCode) + ")");
    }
}

template <class T>
void expectError(const gfx::Result<T>& result, gfx::ErrorCode expected, const char* message) {
    check(!result && result.error().code == expected, message);
    ++rejections;
}

void expectFailure(const gfx::Result<void>& result, const char* message) {
    check(!result, message);
    ++rejections;
}

template <std::size_t N>
gfx::ShaderBinary spirvBinary(const std::array<std::uint32_t, N>& words, gfx::ShaderStage stage) {
    const auto bytes = std::as_bytes(std::span{words});
    return {stage, {bytes.begin(), bytes.end()}, gfx::ShaderFormat::SPIRV};
}

constexpr auto kHlslVertex = R"(
struct VsIn {
    float3 pos : POSITION;
    float4 col : COLOR;
};
struct VsOut {
    float4 pos : SV_Position;
    float4 col : COLOR;
};
VsOut main(VsIn input) {
    VsOut output;
    output.pos = float4(input.pos, 1.0);
    output.col = input.col;
    return output;
}
)";

constexpr auto kHlslFragment = R"(
struct PsIn {
    float4 pos : SV_Position;
    float4 col : COLOR;
};
float4 main(PsIn input) : SV_Target0 {
    return input.col;
}
)";

struct Vertex {
    float x, y, z;
    float r, g, b, a;
};

gfx::VertexLayout vertexLayout() {
    return {
        {{0, sizeof(Vertex), gfx::VertexInputRate::Vertex}},
        {
            {"POSITION", 0, gfx::VertexFormat::Float3, 0, 0},
            {"COLOR", 0, gfx::VertexFormat::Float4, 0, 12},
        },
    };
}

struct Shaders {
    gfx::ShaderBinary vertex;
    gfx::ShaderBinary fragment;
};

Shaders makeShaders(gfx::Backend backend) {
    if (backend == gfx::Backend::D3D12) {
        return {
            take(gfx::compileHlsl(kHlslVertex, "main", gfx::ShaderStage::Vertex)),
            take(gfx::compileHlsl(kHlslFragment, "main", gfx::ShaderStage::Fragment)),
        };
    }
    return {
        spirvBinary(vertexFixtures::position3, gfx::ShaderStage::Vertex),
        spirvBinary(vertexFixtures::color, gfx::ShaderStage::Fragment),
    };
}

float halfToFloat(std::uint16_t bits) {
    const std::uint32_t exp = (bits >> 10u) & 31u;
    const std::uint32_t frac = bits & 1023u;
    const float value = std::ldexp(
        exp ? (1.0f + static_cast<float>(frac) / 1024.0f) : (static_cast<float>(frac) / 1024.0f),
        exp ? (static_cast<int>(exp) - 15) : -14);
    return (bits & 0x8000u) ? -value : value;
}

gfx::BufferPtr makeVertexBuffer(gfx::Device& device, std::span<const Vertex> vertices) {
    const auto bytes = std::as_bytes(vertices);
    auto buf = take(device.createBuffer({bytes.size(), gfx::Memory::Upload}));
    take(buf->write(0, bytes));
    return buf;
}

gfx::GraphicsPipelineDesc makePipelineDesc(
    const Shaders& shaders,
    gfx::Format colorFormat,
    std::uint32_t sampleCount,
    bool withDepth = false,
    bool depthTest = false,
    bool depthWrite = false) {
    gfx::GraphicsPipelineDesc desc;
    desc.vertex = shaders.vertex;
    desc.fragment = shaders.fragment;
    desc.colorFormat = colorFormat;
    desc.vertexLayout = vertexLayout();
    desc.renderTargets = gfx::RenderTargetLayout{
        {colorFormat},
        withDepth ? std::optional<gfx::Format>{gfx::Format::D32Float} : std::nullopt,
        sampleCount};
    desc.depthState = {depthTest, depthWrite, gfx::CompareOp::Less};
    return desc;
}

std::vector<std::byte> renderAndReadback(
    gfx::Device& device,
    const Shaders& shaders,
    std::uint32_t width,
    std::uint32_t height,
    gfx::Format format,
    std::uint32_t sampleCount,
    std::span<const Vertex> vertices,
    gfx::Color clearColor,
    gfx::TextureFootprint& outFootprint) {
    auto vb = makeVertexBuffer(device, vertices);
    auto pipeline = take(device.createGraphicsPipeline(makePipelineDesc(shaders, format, sampleCount)));

    auto renderTex = take(device.createTexture({width, height, format, true, false, false, sampleCount}));
    gfx::TexturePtr readSource = renderTex;
    gfx::TexturePtr resolvedTex;
    if (sampleCount > 1u) {
        check(renderTex->footprint().size == 0u, "MSAA texture footprint must be zero");
        resolvedTex = take(device.createTexture({width, height, format, false, false, false, 1u}));
        readSource = resolvedTex;
    }
    outFootprint = readSource->footprint();
    auto rb = take(device.createBuffer({outFootprint.size, gfx::Memory::Readback}));

    auto cmd = take(device.createCommandList());
    take(cmd->begin());
    take(cmd->transition(vb, gfx::ResourceState::VertexBuffer));
    take(cmd->transition(renderTex, gfx::ResourceState::RenderTarget));
    take(cmd->beginRenderPass(renderTex, clearColor));
    take(cmd->setPipeline(pipeline));
    take(cmd->bindVertexBuffer(0, vb, sizeof(Vertex)));
    take(cmd->draw(static_cast<std::uint32_t>(vertices.size())));
    take(cmd->endRenderPass());
    if (sampleCount > 1u) {
        take(cmd->transition(renderTex, gfx::ResourceState::ResolveSource));
        take(cmd->transition(resolvedTex, gfx::ResourceState::ResolveDestination));
        take(cmd->resolveTexture(renderTex, resolvedTex));
        ++resolves;
    }
    take(cmd->transition(readSource, gfx::ResourceState::CopySource));
    take(cmd->copyTextureToBuffer(readSource, rb));
    take(cmd->end());

    const auto fence = take(device.queue().submit(*cmd));
    take(device.queue().wait(fence, std::chrono::seconds{10}));
    ++images;
    return take(rb->read(0, outFootprint.size));
}

void verifyEmptyStats(gfx::Device& device) {
    const auto stats = take(device.resourceStatistics());
    check(stats.trackedObjects == 0 &&
          stats.uniqueResources == 0 &&
          stats.resourceAllocationBytes == 0 &&
          stats.pendingSubmissions == 0 &&
          stats.pendingObjectReferences == 0,
          "All MSAA resources, views, pipelines, and submissions must reclaim to zero");
}

void verifyAdapter(gfx::Device& device, gfx::Device& otherDevice, gfx::Backend backend, bool validation) {
    const auto shaders = makeShaders(backend);

    // 1. FormatSupport::sampleCounts queries
    const auto rgba8Support = take(device.queryFormatSupport(gfx::Format::RGBA8UNorm));
    const auto bgra8Support = take(device.queryFormatSupport(gfx::Format::BGRA8UNorm));
    const auto rgba16fSupport = take(device.queryFormatSupport(gfx::Format::RGBA16Float));
    const auto d32Support = take(device.queryFormatSupport(gfx::Format::D32Float));
    for (const auto& s : {rgba8Support, bgra8Support, rgba16fSupport, d32Support}) {
        check((s.sampleCounts & 1u) != 0u, "Supported texture format must report 1x sample count");
        check((s.sampleCounts & ~15u) == 0u, "sampleCounts mask must only use bits 1/2/4/8");
    }
    check((rgba8Support.sampleCounts & 4u) != 0u, "RGBA8UNorm must support 4x MSAA on tested adapters");
    check((d32Support.sampleCounts & 4u) != 0u, "D32Float must support 4x MSAA on tested adapters");

    // 2. True 4x MSAA Anti-Aliasing vs 1x Aliased Diagonal Verification
    // Triangle covers lower-left half: (-1, -1), (1, -1), (-1, 1) -> edge x + y = 0 in NDC.
    constexpr std::uint32_t kSize = 32;
    const std::array<Vertex, 3> diagonalTri{{
        {-1.0f, -1.0f, 0.5f, 1.0f, 0.5f, 0.0f, 1.0f},
        { 1.0f, -1.0f, 0.5f, 1.0f, 0.5f, 0.0f, 1.0f},
        {-1.0f,  1.0f, 0.5f, 1.0f, 0.5f, 0.0f, 1.0f},
    }};
    gfx::TextureFootprint fp1x{}, fp4x{};
    const auto data1x = renderAndReadback(device, shaders, kSize, kSize, gfx::Format::RGBA8UNorm, 1u, diagonalTri, {0, 0, 0, 1}, fp1x);
    const auto data4x = renderAndReadback(device, shaders, kSize, kSize, gfx::Format::RGBA8UNorm, 4u, diagonalTri, {0, 0, 0, 1}, fp4x);

    unsigned intermediate1x = 0;
    unsigned intermediate4x = 0;
    unsigned fullInside4x = 0;
    unsigned fullOutside4x = 0;
    for (std::uint32_t y = 0; y < kSize; ++y) {
        for (std::uint32_t x = 0; x < kSize; ++x) {
            const auto* p1 = reinterpret_cast<const std::uint8_t*>(data1x.data() + static_cast<std::size_t>(y) * fp1x.rowPitch + static_cast<std::size_t>(x) * 4u);
            const auto* p4 = reinterpret_cast<const std::uint8_t*>(data4x.data() + static_cast<std::size_t>(y) * fp4x.rowPitch + static_cast<std::size_t>(x) * 4u);
            if (p1[0] > 0u && p1[0] < 255u) {
                ++intermediate1x;
            }
            if (p4[0] >= 32u && p4[0] <= 223u) {
                ++intermediate4x;
            }
            // In both D3D12 and Vulkan (with negative-height Y-flip viewport),
            // NDC (-1, +1) is top-left (x=0, y=0) and NDC (+1, -1) is bottom-right (x=31, y=31).
            // The diagonal edge runs along x == y, with x < y strictly inside and x > y strictly outside.
            const int signedDist = static_cast<int>(y) - static_cast<int>(x);
            if (signedDist >= 2) {
                check(p4[0] == 255u && std::abs(static_cast<int>(p4[1]) - 128) <= 1 && p4[2] == 0u && p4[3] == 255u,
                      "4x MSAA resolved interior pixel must match solid triangle color");
                ++fullInside4x;
            } else if (signedDist <= -2) {
                check(p4[0] == 0u && p4[1] == 0u && p4[2] == 0u && p4[3] == 255u,
                      "4x MSAA resolved exterior pixel must match clear color");
                ++fullOutside4x;
            }
            check(p4[3] == 255u, "Resolved alpha must remain 255");
        }
    }
    check(intermediate1x == 0u, "1x aliased diagonal must have zero intermediate coverage pixels");
    check(intermediate4x >= 24u, "4x MSAA resolved diagonal must produce anti-aliased intermediate pixels");
    check(fullInside4x == 465u && fullOutside4x == 465u, "Exact interior and exterior pixel counts verified");

    // 3. MSAA Color + MSAA Depth (D32Float) + TextureView (RTV & DSV) + Early View Destruction + CommandPool + GPU Timestamp
    {
        auto pool = take(device.createCommandPool());
        auto cmd = take(pool->createCommandList());
        auto msaaColor = take(device.createTexture({kSize, kSize, gfx::Format::RGBA8UNorm, true, false, false, 4u}));
        auto msaaDepth = take(device.createTexture({kSize, kSize, gfx::Format::D32Float, false, true, false, 4u}));
        auto resolved = take(device.createTexture({kSize, kSize, gfx::Format::RGBA8UNorm, false, false, false, 1u}));
        auto rtvView = take(device.createTextureView(msaaColor, {std::nullopt, gfx::TextureViewUsage::RenderTarget}));
        auto dsvView = take(device.createTextureView(msaaDepth, {std::nullopt, gfx::TextureViewUsage::DepthStencil}));

        // Near green center quad (z = 0.25) and far blue fullscreen quad (z = 0.75)
        const std::array<Vertex, 12> depthVerts{{
            // Near green quad: [-0.5, 0.5] x [-0.5, 0.5], z = 0.25
            {-0.5f, -0.5f, 0.25f, 0.0f, 1.0f, 0.0f, 1.0f},
            { 0.5f, -0.5f, 0.25f, 0.0f, 1.0f, 0.0f, 1.0f},
            {-0.5f,  0.5f, 0.25f, 0.0f, 1.0f, 0.0f, 1.0f},
            { 0.5f, -0.5f, 0.25f, 0.0f, 1.0f, 0.0f, 1.0f},
            { 0.5f,  0.5f, 0.25f, 0.0f, 1.0f, 0.0f, 1.0f},
            {-0.5f,  0.5f, 0.25f, 0.0f, 1.0f, 0.0f, 1.0f},
            // Far blue fullscreen quad: [-1, 1] x [-1, 1], z = 0.75
            {-1.0f, -1.0f, 0.75f, 0.0f, 0.0f, 1.0f, 1.0f},
            { 1.0f, -1.0f, 0.75f, 0.0f, 0.0f, 1.0f, 1.0f},
            {-1.0f,  1.0f, 0.75f, 0.0f, 0.0f, 1.0f, 1.0f},
            { 1.0f, -1.0f, 0.75f, 0.0f, 0.0f, 1.0f, 1.0f},
            { 1.0f,  1.0f, 0.75f, 0.0f, 0.0f, 1.0f, 1.0f},
            {-1.0f,  1.0f, 0.75f, 0.0f, 0.0f, 1.0f, 1.0f},
        }};
        auto vb = makeVertexBuffer(device, depthVerts);
        auto depthPipe = take(device.createGraphicsPipeline(
            makePipelineDesc(shaders, gfx::Format::RGBA8UNorm, 4u, true, true, true)));
        const auto fp = resolved->footprint();
        auto rb = take(device.createBuffer({fp.size, gfx::Memory::Readback}));

        take(cmd->begin());
        const bool timestamps = device.caps().timestampQueries;
        if (timestamps) {
            take(cmd->writeTimestamp(0));
        }
        take(cmd->transition(vb, gfx::ResourceState::VertexBuffer));
        take(cmd->transition(msaaColor, gfx::ResourceState::RenderTarget));
        take(cmd->transition(msaaDepth, gfx::ResourceState::DepthWrite));
        gfx::RenderPassDesc pass;
        pass.colors.push_back({nullptr, gfx::LoadOp::Clear, {0.0f, 0.0f, 0.0f, 1.0f}, rtvView});
        pass.depth = {nullptr, gfx::LoadOp::Clear, 1.0f, false, dsvView};
        take(cmd->beginRenderPass(pass));
        take(cmd->setPipeline(depthPipe));
        take(cmd->bindVertexBuffer(0, vb, sizeof(Vertex)));
        take(cmd->draw(6, 1, 0));
        take(cmd->draw(6, 1, 6));
        take(cmd->endRenderPass());
        // Drop views before submit to verify retained native lifetime
        rtvView.reset();
        dsvView.reset();

        take(cmd->transition(msaaColor, gfx::ResourceState::ResolveSource));
        take(cmd->transition(resolved, gfx::ResourceState::ResolveDestination));
        take(cmd->resolveTexture(msaaColor, resolved));
        ++resolves;
        take(cmd->transition(resolved, gfx::ResourceState::CopySource));
        take(cmd->copyTextureToBuffer(resolved, rb));
        if (timestamps) {
            take(cmd->writeTimestamp(1));
            take(cmd->resolveTimestamps());
        }
        take(cmd->end());

        const auto fence = take(device.queue().submit(*cmd));
        take(device.queue().wait(fence, std::chrono::seconds{10}));
        if (timestamps) {
            const double ms = take(cmd->elapsedGpuMilliseconds());
            check(std::isfinite(ms) && ms >= 0.0, "MSAA CommandPool GPU timestamp must be finite and non-negative");
        }
        take(pool->reset());

        const auto bytes = take(rb->read(0, fp.size));
        for (std::uint32_t y = 0; y < kSize; ++y) {
            for (std::uint32_t x = 0; x < kSize; ++x) {
                const auto* px = reinterpret_cast<const std::uint8_t*>(bytes.data() + static_cast<std::size_t>(y) * fp.rowPitch + static_cast<std::size_t>(x) * 4u);
                const bool inCenter = (x >= 9u && x <= 22u && y >= 9u && y <= 22u);
                const bool inOuter = (x <= 6u || x >= 25u || y <= 6u || y >= 25u);
                if (inCenter) {
                    check(px[0] == 0u && px[1] == 255u && px[2] == 0u && px[3] == 255u,
                          "MSAA depth test must keep near green center quad");
                } else if (inOuter) {
                    check(px[0] == 0u && px[1] == 0u && px[2] == 255u && px[3] == 255u,
                          "MSAA depth test must draw far blue background outside center quad");
                }
            }
        }
        ++images;
    }

    // 4. Additional supported sample counts (2x, 8x) & formats (BGRA8UNorm, RGBA16Float)
    const std::array<Vertex, 3> fullTri{{
        {-1.0f, -1.0f, 0.5f, 0.25f, 0.5f, 0.75f, 1.0f},
        { 3.0f, -1.0f, 0.5f, 0.25f, 0.5f, 0.75f, 1.0f},
        {-1.0f,  3.0f, 0.5f, 0.25f, 0.5f, 0.75f, 1.0f},
    }};
    for (const std::uint32_t extraCount : {2u, 8u}) {
        if ((rgba8Support.sampleCounts & extraCount) != 0u) {
            gfx::TextureFootprint fp{};
            const auto data = renderAndReadback(device, shaders, 16, 16, gfx::Format::RGBA8UNorm, extraCount, fullTri, {0, 0, 0, 1}, fp);
            for (std::uint32_t y = 0; y < 16; ++y) {
                for (std::uint32_t x = 0; x < 16; ++x) {
                    const auto* px = reinterpret_cast<const std::uint8_t*>(data.data() + static_cast<std::size_t>(y) * fp.rowPitch + static_cast<std::size_t>(x) * 4u);
                    check(std::abs(static_cast<int>(px[0]) - 64) <= 1 &&
                          std::abs(static_cast<int>(px[1]) - 128) <= 1 &&
                          std::abs(static_cast<int>(px[2]) - 191) <= 1 &&
                          px[3] == 255u,
                          "2x/8x MSAA RGBA8 resolved full pixels must match");
                }
            }
        }
    }
    if ((bgra8Support.sampleCounts & 4u) != 0u) {
        gfx::TextureFootprint fp{};
        const auto data = renderAndReadback(device, shaders, 16, 16, gfx::Format::BGRA8UNorm, 4u, fullTri, {0, 0, 0, 1}, fp);
        for (std::uint32_t y = 0; y < 16; ++y) {
            for (std::uint32_t x = 0; x < 16; ++x) {
                const auto* px = reinterpret_cast<const std::uint8_t*>(data.data() + static_cast<std::size_t>(y) * fp.rowPitch + static_cast<std::size_t>(x) * 4u);
                // BGRA byte order: B=0.75 (191), G=0.5 (128), R=0.25 (64), A=1.0 (255)
                check(std::abs(static_cast<int>(px[0]) - 191) <= 1 &&
                      std::abs(static_cast<int>(px[1]) - 128) <= 1 &&
                      std::abs(static_cast<int>(px[2]) - 64) <= 1 &&
                      px[3] == 255u,
                      "4x MSAA BGRA8UNorm resolved pixels must match BGRA channel order");
            }
        }
    }
    if ((rgba16fSupport.sampleCounts & 4u) != 0u) {
        gfx::TextureFootprint fp{};
        const auto data = renderAndReadback(device, shaders, 16, 16, gfx::Format::RGBA16Float, 4u, fullTri, {0, 0, 0, 1}, fp);
        for (std::uint32_t y = 0; y < 16; ++y) {
            for (std::uint32_t x = 0; x < 16; ++x) {
                std::uint16_t raw[4]{};
                std::memcpy(raw, data.data() + static_cast<std::size_t>(y) * fp.rowPitch + static_cast<std::size_t>(x) * 8u, sizeof(raw));
                check(std::abs(halfToFloat(raw[0]) - 0.25f) < 0.002f &&
                      std::abs(halfToFloat(raw[1]) - 0.50f) < 0.002f &&
                      std::abs(halfToFloat(raw[2]) - 0.75f) < 0.002f &&
                      std::abs(halfToFloat(raw[3]) - 1.00f) < 0.002f,
                      "4x MSAA RGBA16Float resolved pixels must match");
            }
        }
    }

    // 5. Pipeline cache separates sampleCount keys
    {
        take(device.clearGraphicsPipelineCache());
        const auto p1a = take(device.createCachedGraphicsPipeline(makePipelineDesc(shaders, gfx::Format::RGBA8UNorm, 1u)));
        check(!p1a.hit, "First 1x pipeline cache lookup must miss");
        const auto p1b = take(device.createCachedGraphicsPipeline(makePipelineDesc(shaders, gfx::Format::RGBA8UNorm, 1u)));
        check(p1b.hit && p1b.pipeline == p1a.pipeline, "Repeat 1x pipeline cache lookup must hit");
        const auto p4a = take(device.createCachedGraphicsPipeline(makePipelineDesc(shaders, gfx::Format::RGBA8UNorm, 4u)));
        check(!p4a.hit && p4a.pipeline != p1a.pipeline, "4x MSAA pipeline must not collide with 1x cache entry");
        const auto p4b = take(device.createCachedGraphicsPipeline(makePipelineDesc(shaders, gfx::Format::RGBA8UNorm, 4u)));
        check(p4b.hit && p4b.pipeline == p4a.pipeline, "Repeat 4x MSAA pipeline cache lookup must hit");
        take(device.clearGraphicsPipelineCache());
    }

    // 6. Comprehensive rejection & atomic recording contracts
    {
        expectError(device.createTexture({16, 16, gfx::Format::RGBA8UNorm, true, false, false, 0u}),
                    gfx::ErrorCode::InvalidArgument, "0x texture creation rejected");
        expectError(device.createTexture({16, 16, gfx::Format::RGBA8UNorm, true, false, false, 3u}),
                    gfx::ErrorCode::InvalidArgument, "3x texture creation rejected");
        expectError(device.createTexture({16, 16, gfx::Format::RGBA8UNorm, true, false, false, 16u}),
                    gfx::ErrorCode::InvalidArgument, "16x texture creation rejected");
        expectError(device.createTexture({16, 16, gfx::Format::RGBA8UNorm, false, false, false, 4u}),
                    gfx::ErrorCode::InvalidArgument, "Non-attachment 4x texture rejected");
        expectError(device.createTexture({16, 16, gfx::Format::RGBA8UNorm, true, false, true, 4u}),
                    gfx::ErrorCode::InvalidArgument, "Storage 4x texture rejected");

        auto msaaColor = take(device.createTexture({16, 16, gfx::Format::RGBA8UNorm, true, false, false, 4u}));
        auto msaaColor2 = take(device.createTexture({16, 16, gfx::Format::RGBA8UNorm, true, false, false, 4u}));
        auto msaaDepth = take(device.createTexture({16, 16, gfx::Format::D32Float, false, true, false, 4u}));
        auto single16 = take(device.createTexture({16, 16, gfx::Format::RGBA8UNorm, true, false, false, 1u}));
        auto single16Bgra = take(device.createTexture({16, 16, gfx::Format::BGRA8UNorm, false, false, false, 1u}));
        auto single8 = take(device.createTexture({8, 8, gfx::Format::RGBA8UNorm, false, false, false, 1u}));
        auto singleDepth = take(device.createTexture({16, 16, gfx::Format::D32Float, false, true, false, 1u}));
        auto foreignSingle = take(otherDevice.createTexture({16, 16, gfx::Format::RGBA8UNorm, false, false, false, 1u}));
        auto uploadBuf = take(device.createBuffer({4096, gfx::Memory::Upload}));
        auto readbackBuf = take(device.createBuffer({4096, gfx::Memory::Readback}));
        auto pipe1x = take(device.createGraphicsPipeline(makePipelineDesc(shaders, gfx::Format::RGBA8UNorm, 1u)));
        auto pipe4x = take(device.createGraphicsPipeline(makePipelineDesc(shaders, gfx::Format::RGBA8UNorm, 4u)));
        auto vb = makeVertexBuffer(device, fullTri);

        expectError(device.createTextureView(msaaColor, {std::nullopt, gfx::TextureViewUsage::ShaderRead}),
                    gfx::ErrorCode::Unsupported, "MSAA ShaderRead view creation rejected");

        auto cmd = take(device.createCommandList());
        take(cmd->begin());
        take(cmd->transition(vb, gfx::ResourceState::VertexBuffer));
        expectError(cmd->transition(msaaColor, gfx::ResourceState::ShaderRead),
                    gfx::ErrorCode::Unsupported, "MSAA ShaderRead transition rejected");
        expectError(cmd->transition(msaaColor, gfx::ResourceState::CopySource),
                    gfx::ErrorCode::InvalidArgument, "MSAA CopySource transition rejected");
        expectError(cmd->transition(msaaColor, gfx::ResourceState::CopyDestination),
                    gfx::ErrorCode::InvalidArgument, "MSAA CopyDestination transition rejected");
        expectError(cmd->transition(msaaColor, gfx::ResourceState::ResolveDestination),
                    gfx::ErrorCode::InvalidArgument, "MSAA ResolveDestination transition rejected");
        expectError(cmd->transition(msaaDepth, gfx::ResourceState::ResolveSource),
                    gfx::ErrorCode::Unsupported, "Depth ResolveSource transition rejected");
        expectError(cmd->transition(msaaDepth, gfx::ResourceState::ResolveDestination),
                    gfx::ErrorCode::Unsupported, "Depth ResolveDestination transition rejected");

        expectError(cmd->copyBufferToTexture(uploadBuf, msaaColor),
                    gfx::ErrorCode::InvalidArgument, "copyBufferToTexture on MSAA rejected");
        expectError(cmd->copyTextureToBuffer(msaaColor, readbackBuf),
                    gfx::ErrorCode::InvalidArgument, "copyTextureToBuffer on MSAA rejected");
        expectError(cmd->copyBufferToTextureRegion(uploadBuf, msaaColor, {0, 0, 0, 0, 8, 8}),
                    gfx::ErrorCode::InvalidArgument, "copyBufferToTextureRegion on MSAA rejected");
        expectError(cmd->copyTextureToBufferRegion(msaaColor, readbackBuf, {0, 0, 0, 0, 8, 8}),
                    gfx::ErrorCode::InvalidArgument, "copyTextureToBufferRegion on MSAA rejected");

        // Mismatched sample counts in render pass
        take(cmd->transition(msaaColor, gfx::ResourceState::RenderTarget));
        take(cmd->transition(singleDepth, gfx::ResourceState::DepthWrite));
        expectError(cmd->beginRenderPass({{{msaaColor}}, {singleDepth}}),
                    gfx::ErrorCode::InvalidArgument, "Mismatched 4x color + 1x depth pass rejected");

        // Render valid 4x pass, test pipeline mismatch & resolve-inside-pass rejection
        take(cmd->beginRenderPass(msaaColor, {0.0f, 0.0f, 0.0f, 1.0f}));
        // D3D12 validates sample count at draw time; Vulkan validates at setPipeline time.
        if (auto set1x = cmd->setPipeline(pipe1x); !set1x) {
            ++rejections;
        } else {
            take(cmd->bindVertexBuffer(0, vb, sizeof(Vertex)));
            expectFailure(cmd->draw(3), "Drawing with 1x pipeline in 4x MSAA pass must fail");
        }
        expectError(cmd->resolveTexture(msaaColor, single16),
                    gfx::ErrorCode::InvalidState, "resolveTexture inside render pass rejected");
        // Valid 4x draw still succeeds after rejected calls
        take(cmd->setPipeline(pipe4x));
        take(cmd->bindVertexBuffer(0, vb, sizeof(Vertex)));
        take(cmd->draw(3));
        take(cmd->endRenderPass());

        // Resolve rejections outside pass
        expectFailure(cmd->resolveTexture(nullptr, single16), "Null resolve source rejected");
        expectFailure(cmd->resolveTexture(msaaColor, nullptr), "Null resolve destination rejected");
        expectError(cmd->resolveTexture(msaaColor, foreignSingle),
                    gfx::ErrorCode::WrongDevice, "Foreign resolve destination rejected");
        expectError(cmd->resolveTexture(msaaColor, msaaColor),
                    gfx::ErrorCode::InvalidArgument, "Self resolve rejected");
        expectError(cmd->resolveTexture(msaaDepth, singleDepth),
                    gfx::ErrorCode::Unsupported, "Depth resolve rejected");
        expectError(cmd->resolveTexture(single16, single16Bgra),
                    gfx::ErrorCode::InvalidArgument, "Single-sampled source resolve rejected");
        expectError(cmd->resolveTexture(msaaColor, msaaColor2),
                    gfx::ErrorCode::InvalidArgument, "Multisampled destination resolve rejected");
        expectError(cmd->resolveTexture(msaaColor, single8),
                    gfx::ErrorCode::InvalidArgument, "Extent-mismatched resolve rejected");
        expectError(cmd->resolveTexture(msaaColor, single16Bgra),
                    gfx::ErrorCode::InvalidArgument, "Format-mismatched resolve rejected");
        expectError(cmd->resolveTexture(msaaColor, single16),
                    gfx::ErrorCode::InvalidState, "Wrong-state resolve rejected");

        // Valid resolve after all those rejections proves atomic rejection preservation!
        take(cmd->transition(msaaColor, gfx::ResourceState::ResolveSource));
        take(cmd->transition(single16, gfx::ResourceState::ResolveDestination));
        take(cmd->resolveTexture(msaaColor, single16));
        ++resolves;
        take(cmd->transition(single16, gfx::ResourceState::CopySource));
        take(cmd->copyTextureToBuffer(single16, readbackBuf));
        take(cmd->end());

        const auto fence = take(device.queue().submit(*cmd));
        take(device.queue().wait(fence, std::chrono::seconds{10}));
        const auto fp = single16->footprint();
        const auto bytes = take(readbackBuf->read(0, fp.size));
        const auto* px = reinterpret_cast<const std::uint8_t*>(bytes.data());
        check(std::abs(static_cast<int>(px[0]) - 64) <= 1 &&
              std::abs(static_cast<int>(px[1]) - 128) <= 1 &&
              std::abs(static_cast<int>(px[2]) - 191) <= 1 &&
              px[3] == 255u,
              "Atomic rejections must preserve valid MSAA render and resolve recording");
        ++images;
    }

    verifyEmptyStats(device);
    if (validation) {
        const auto errs = take(device.validationErrorCount());
        check(errs == 0u, "Validation layer must report zero MSAA errors");
    }
}

void verifyWarpLossRecovery() {
    auto adapters = take(gfx::enumerateAdapters(gfx::Backend::D3D12));
    auto it = std::find_if(adapters.begin(), adapters.end(), [](const gfx::AdapterInfo& info) {
        return info.softwareAdapter;
    });
    check(it != adapters.end(), "WARP adapter required for device loss/recovery MSAA test");

    gfx::DeviceDesc desc{gfx::Backend::D3D12, false, true, true, it->id};
    auto device = take(gfx::createDevice(desc));
    {
        auto other = take(gfx::createDevice({gfx::Backend::D3D12, false, true, false, it->id}));
        verifyAdapter(*device, *other, gfx::Backend::D3D12, false);
    }

    // Inject device loss and verify MSAA works cleanly on recovered device generation.
    auto staleMsaa = take(device->createTexture({16, 16, gfx::Format::RGBA8UNorm, true, false, false, 4u}));
    take(device->removeForTesting());
    expectError(device->createTexture({16, 16, gfx::Format::RGBA8UNorm, true, false, false, 4u}),
                gfx::ErrorCode::DeviceLost, "Lost device rejects MSAA texture creation");

    auto recovered = take(device->recover());
    auto recoveredCmd = take(recovered->createCommandList());
    auto freshSingle = take(recovered->createTexture({16, 16, gfx::Format::RGBA8UNorm, false, false, false, 1u}));
    take(recoveredCmd->begin());
    expectError(recoveredCmd->resolveTexture(staleMsaa, freshSingle),
                gfx::ErrorCode::WrongDevice, "Recovered device rejects lost-generation MSAA texture");
    take(recoveredCmd->end());
    recoveredCmd.reset();
    freshSingle.reset();
    staleMsaa.reset();

    auto recoveredOther = take(gfx::createDevice({gfx::Backend::D3D12, false, true, false, it->id}));
    verifyAdapter(*recovered, *recoveredOther, gfx::Backend::D3D12, false);
}

} // namespace

int main(int argc, char** argv) {
    try {
        bool useD3D12 = false;
        bool useVulkan = false;
        bool validation = false;
        bool warp = false;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg(argv[i]);
            if (arg == "--d3d12") useD3D12 = true;
            else if (arg == "--vulkan") useVulkan = true;
            else if (arg == "--validation") validation = true;
            else if (arg == "--warp") warp = true;
            else throw std::runtime_error("Unknown argument: " + std::string(arg));
        }
        if (useD3D12 == useVulkan) {
            throw std::runtime_error("Specify exactly one of --d3d12 or --vulkan");
        }
        if (warp) {
            if (!useD3D12) throw std::runtime_error("--warp is only valid with --d3d12");
            verifyWarpLossRecovery();
            std::cout << "PASS msaa adapters=1 assertions=" << assertions
                      << " resolves=" << resolves
                      << " images=" << images
                      << " rejections=" << rejections
                      << " errors=0\n";
            return 0;
        }

        const gfx::Backend backend = useD3D12 ? gfx::Backend::D3D12 : gfx::Backend::Vulkan;
        auto adapters = take(gfx::enumerateAdapters(backend));
        unsigned tested = 0;
        for (const auto& info : adapters) {
            if (info.softwareAdapter) continue;
            auto device = take(gfx::createDevice({backend, validation, false, false, info.id}));
            auto other = take(gfx::createDevice({backend, false, false, false, info.id}));
            verifyAdapter(*device, *other, backend, validation);
            ++tested;
        }
        if (!tested) {
            std::cout << "SKIP msaa no hardware adapters\n";
            return 77;
        }
        std::cout << "PASS msaa adapters=" << tested
                  << " assertions=" << assertions
                  << " resolves=" << resolves
                  << " images=" << images
                  << " rejections=" << rejections
                  << " errors=0\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL msaa: " << e.what() << "\n";
        return 1;
    }
}
