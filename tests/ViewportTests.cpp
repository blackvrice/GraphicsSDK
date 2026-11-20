#include <graphics/Graphics.hpp>
#include "fixtures/SpirvFixtures.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
unsigned assertions{};
unsigned verifiedImages{};
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

template <std::size_t N>
gfx::ShaderBinary spirvBinary(gfx::ShaderStage stage, const std::array<std::uint32_t, N>& words) {
    const auto raw = std::as_bytes(std::span{words});
    return {stage, std::vector<std::byte>(raw.begin(), raw.end()), gfx::ShaderFormat::SPIRV};
}

struct PipelinePair {
    gfx::PipelinePtr red;
    gfx::PipelinePtr green;
};

PipelinePair makePipelines(gfx::Device& device, gfx::Backend backend) {
    gfx::ShaderBinary vs{}, redPs{}, greenPs{};
    if (backend == gfx::Backend::D3D12) {
        constexpr std::string_view hlsl = R"(
float4 VS(uint id : SV_VertexID) : SV_Position {
    float2 p[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
    return float4(p[id % 3], 0.5, 1.0);
}
float4 PSRed() : SV_Target0 { return float4(1.0, 0.0, 0.0, 1.0); }
float4 PSGreen() : SV_Target0 { return float4(0.0, 1.0, 0.0, 1.0); }
)";
        vs = take(gfx::compileHlsl(hlsl, "VS", gfx::ShaderStage::Vertex));
        redPs = take(gfx::compileHlsl(hlsl, "PSRed", gfx::ShaderStage::Fragment));
        greenPs = take(gfx::compileHlsl(hlsl, "PSGreen", gfx::ShaderStage::Fragment));
    } else {
        vs = spirvBinary(gfx::ShaderStage::Vertex, fixtures::fullscreen);
        redPs = spirvBinary(gfx::ShaderStage::Fragment, fixtures::red);
        greenPs = spirvBinary(gfx::ShaderStage::Fragment, fixtures::green);
    }

    gfx::GraphicsPipelineDesc desc{vs, redPs};
    desc.renderTargets = gfx::RenderTargetLayout{{gfx::Format::RGBA8UNorm}, gfx::Format::D32Float};
    desc.depthState = {true, true, gfx::CompareOp::Always};
    auto redPipeline = take(device.createGraphicsPipeline(desc));

    desc.fragment = greenPs;
    auto greenPipeline = take(device.createGraphicsPipeline(desc));
    return {std::move(redPipeline), std::move(greenPipeline)};
}

void verifyReclaimed(gfx::Device& device) {
    take(device.queue().waitIdle());
    const auto stats = take(device.resourceStatistics());
    check(stats.trackedObjects == 0 &&
          stats.uniqueResources == 0 &&
          stats.resourceAllocationBytes == 0 &&
          stats.pendingSubmissions == 0 &&
          stats.pendingObjectReferences == 0,
          "All native resources and pending submissions must be reclaimed");
}

void runViewportSuite(gfx::Device& device, gfx::Backend backend) {
    constexpr std::uint32_t size = 64;
    auto pipelines = makePipelines(device, backend);
    auto colorTarget = take(device.createTexture({size, size, gfx::Format::RGBA8UNorm, true, false}));
    auto depthTarget = take(device.createTexture({size, size, gfx::Format::D32Float, false, true}));
    const auto colorFp = colorTarget->footprint();
    const auto depthFp = depthTarget->footprint();
    auto colorReadback = take(device.createBuffer({colorFp.size, gfx::Memory::Readback}));
    auto depthReadback = take(device.createBuffer({depthFp.size, gfx::Memory::Readback}));

    auto commands = take(device.createCommandList());
    // Reject outside recording.
    expectError(commands->setViewport({0.0f, 0.0f, 64.0f, 64.0f, 0.0f, 1.0f}),
                gfx::ErrorCode::InvalidState,
                "setViewport outside recording must fail");

    take(commands->begin());
    // Reject outside active render pass.
    expectError(commands->setViewport({0.0f, 0.0f, 64.0f, 64.0f, 0.0f, 1.0f}),
                gfx::ErrorCode::InvalidState,
                "setViewport outside active render pass must fail");

    take(commands->transition(colorTarget, gfx::ResourceState::RenderTarget));
    take(commands->transition(depthTarget, gfx::ResourceState::DepthWrite));

    gfx::RenderPassDesc pass{
        {{colorTarget, gfx::LoadOp::Clear, {0.0f, 0.0f, 1.0f, 1.0f}}},
        {depthTarget, gfx::LoadOp::Clear, 1.0f, false}
    };
    take(commands->beginRenderPass(pass));

    // Reject worker-thread setViewport while pass is open.
    bool workerRejected = false;
    std::thread worker([&]() {
        const auto r = commands->setViewport({0.0f, 0.0f, 32.0f, 32.0f, 0.0f, 1.0f});
        workerRejected = !r && r.error().code == gfx::ErrorCode::InvalidState;
    });
    worker.join();
    check(workerRejected, "setViewport on non-creating thread must fail");
    ++rejections;

    // 1. Top-left quadrant: red, depth [0.0, 0.25] -> z=0.5 maps to 0.125.
    take(commands->setPipeline(pipelines.red));
    take(commands->setViewport({0.0f, 0.0f, 32.0f, 32.0f, 0.0f, 0.25f}));
    take(commands->draw(3));

    // 2. Top-right quadrant: green, depth [0.20, 0.40] -> z=0.5 maps to 0.30.
    take(commands->setPipeline(pipelines.green));
    take(commands->setViewport({32.0f, 0.0f, 32.0f, 32.0f, 0.20f, 0.40f}));
    take(commands->draw(3));

    // 3. Bottom-right quadrant: green, depth [0.50, 1.00] -> z=0.5 maps to 0.75.
    take(commands->setViewport({32.0f, 32.0f, 32.0f, 32.0f, 0.50f, 1.00f}));
    take(commands->draw(3));

    // 4. Inset center-top-left box [16..32, 16..32]: set valid viewport, issue rejected viewports, then draw red.
    //    Rejected calls must preserve the previously active viewport.
    take(commands->setPipeline(pipelines.red));
    take(commands->setViewport({16.0f, 16.0f, 16.0f, 16.0f, 0.0f, 0.10f}));
    expectError(commands->setViewport({0.0f, 0.0f, 0.0f, 16.0f, 0.0f, 1.0f}), gfx::ErrorCode::InvalidArgument, "Zero width");
    expectError(commands->setViewport({-1.0f, 0.0f, 16.0f, 16.0f, 0.0f, 1.0f}), gfx::ErrorCode::InvalidArgument, "Negative x");
    expectError(commands->setViewport({49.0f, 0.0f, 16.0f, 16.0f, 0.0f, 1.0f}), gfx::ErrorCode::InvalidArgument, "Out-of-bounds x+width");
    expectError(commands->setViewport({0.0f, 0.0f, 16.0f, 16.0f, 0.8f, 0.2f}), gfx::ErrorCode::InvalidArgument, "minDepth > maxDepth");
    expectError(commands->setViewport({std::numeric_limits<float>::quiet_NaN(), 0.0f, 16.0f, 16.0f, 0.0f, 1.0f}),
                gfx::ErrorCode::InvalidArgument, "NaN viewport");
    take(commands->draw(3));

    take(commands->endRenderPass());
    expectError(commands->setViewport({0.0f, 0.0f, 64.0f, 64.0f, 0.0f, 1.0f}),
                gfx::ErrorCode::InvalidState,
                "setViewport after endRenderPass must fail");

    take(commands->transition(colorTarget, gfx::ResourceState::CopySource));
    take(commands->transition(depthTarget, gfx::ResourceState::CopySource));
    take(commands->copyTextureToBuffer(colorTarget, colorReadback));
    take(commands->copyTextureToBuffer(depthTarget, depthReadback));
    take(commands->end());

    const auto fence1 = take(device.queue().submit(*commands));
    take(device.queue().wait(fence1));

    const auto colorBytes = take(colorReadback->read(0, colorFp.size));
    const auto depthBytes = take(depthReadback->read(0, depthFp.size));

    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
            std::array<unsigned char, 4> expectedColor{0, 0, 255, 255};
            float expectedDepth = 1.0f;
            if (x < 32 && y < 32) {
                expectedColor = {255, 0, 0, 255};
                expectedDepth = (x >= 16 && y >= 16) ? 0.05f : 0.125f;
            } else if (x >= 32 && y < 32) {
                expectedColor = {0, 255, 0, 255};
                expectedDepth = 0.30f;
            } else if (x >= 32 && y >= 32) {
                expectedColor = {0, 255, 0, 255};
                expectedDepth = 0.75f;
            }

            const std::size_t colorOffset = static_cast<std::size_t>(y) * colorFp.rowPitch + static_cast<std::size_t>(x) * 4u;
            for (std::size_t c = 0; c < 4; ++c) {
                const auto actual = std::to_integer<unsigned char>(colorBytes[colorOffset + c]);
                check(actual == expectedColor[c], "Quadrant/inset viewport color pixel mismatch");
            }
            float actualDepth = 0.0f;
            std::memcpy(&actualDepth,
                        depthBytes.data() + static_cast<std::size_t>(y) * depthFp.rowPitch + static_cast<std::size_t>(x) * 4u,
                        sizeof(actualDepth));
            check(std::isfinite(actualDepth) && std::abs(actualDepth - expectedDepth) < 1e-4f,
                  "Quadrant/inset viewport depth value mismatch");
        }
    }
    verifiedImages += 2;

    // 5. Verify a fresh render pass resets viewport to the full attachment extent and [0, 1] depth range.
    take(commands->begin());
    take(commands->transition(colorTarget, gfx::ResourceState::RenderTarget));
    take(commands->transition(depthTarget, gfx::ResourceState::DepthWrite));
    take(commands->beginRenderPass(pass));
    take(commands->setPipeline(pipelines.green));
    take(commands->draw(3));
    take(commands->endRenderPass());
    take(commands->transition(colorTarget, gfx::ResourceState::CopySource));
    take(commands->transition(depthTarget, gfx::ResourceState::CopySource));
    take(commands->copyTextureToBuffer(colorTarget, colorReadback));
    take(commands->copyTextureToBuffer(depthTarget, depthReadback));
    take(commands->end());

    const auto fence2 = take(device.queue().submit(*commands));
    take(device.queue().wait(fence2));

    const auto resetColor = take(colorReadback->read(0, colorFp.size));
    const auto resetDepth = take(depthReadback->read(0, depthFp.size));
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
            const std::size_t colorOffset = static_cast<std::size_t>(y) * colorFp.rowPitch + static_cast<std::size_t>(x) * 4u;
            check(std::to_integer<unsigned char>(resetColor[colorOffset + 0]) == 0 &&
                  std::to_integer<unsigned char>(resetColor[colorOffset + 1]) == 255 &&
                  std::to_integer<unsigned char>(resetColor[colorOffset + 2]) == 0 &&
                  std::to_integer<unsigned char>(resetColor[colorOffset + 3]) == 255,
                  "Fresh render pass must reset viewport to full attachment extent");
            float actualDepth = 0.0f;
            std::memcpy(&actualDepth,
                        resetDepth.data() + static_cast<std::size_t>(y) * depthFp.rowPitch + static_cast<std::size_t>(x) * 4u,
                        sizeof(actualDepth));
            check(std::isfinite(actualDepth) && std::abs(actualDepth - 0.5f) < 1e-4f,
                  "Fresh render pass must reset viewport depth range to [0, 1]");
        }
    }
    verifiedImages += 2;

    pass = {};
    commands.reset();
    pipelines.red.reset();
    pipelines.green.reset();
    colorTarget.reset();
    depthTarget.reset();
    colorReadback.reset();
    depthReadback.reset();
    verifyReclaimed(device);
}
} // namespace

int main(int argc, char** argv) try {
    auto backend = gfx::Backend::Vulkan;
    bool validation = false;
    bool warp = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--d3d12") == 0) {
            backend = gfx::Backend::D3D12;
        } else if (std::strcmp(argv[i], "--validation") == 0) {
            validation = true;
        } else if (std::strcmp(argv[i], "--warp") == 0) {
            warp = true;
        } else {
            throw std::runtime_error("Usage: gfx_viewport_tests [--d3d12] [--validation] [--warp]");
        }
    }

    check(gfx::backendCompiled(backend), "Requested backend must be compiled");
    const auto adapters = take(gfx::enumerateAdapters(backend));
    if (backend == gfx::Backend::D3D12 && !warp &&
        !std::any_of(adapters.begin(), adapters.end(), [](const auto& info) { return !info.softwareAdapter; })) {
        std::cout << "SKIP no hardware D3D12 adapter\n";
        return 77;
    }

    unsigned testedAdapters = 0;
    for (const auto& adapter : adapters) {
        if (backend == gfx::Backend::D3D12 && adapter.softwareAdapter != warp) {
            continue;
        }
        gfx::DeviceDesc desc;
        desc.backend = backend;
        desc.validation = validation;
        desc.softwareAdapter = adapter.softwareAdapter;
        desc.adapter = adapter.id;

        auto device = take(gfx::createDevice(desc));
        runViewportSuite(*device, backend);
        if (validation) {
            check(take(device->validationErrorCount()) == 0,
                  "Validation error count must remain zero");
        }
        ++testedAdapters;
    }

    check(testedAdapters > 0, "At least one real adapter must execute");
    std::cout << "{\"event\":\"viewport\""
              << ",\"backend\":" << std::quoted(backend == gfx::Backend::D3D12 ? "d3d12" : "vulkan")
              << ",\"source\":" << std::quoted(gfx::buildFingerprint())
              << ",\"adapters\":" << testedAdapters
              << ",\"validation\":" << (validation ? "true" : "false")
              << ",\"assertions\":" << assertions
              << ",\"images\":" << verifiedImages
              << ",\"rejections\":" << rejections
              << ",\"errors\":0}\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
