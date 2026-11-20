#include <graphics/Graphics.hpp>
#include "fixtures/BindingFixtures.hpp"

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
#include <thread>
#include <vector>

namespace {
struct Totals {
    std::uint64_t assertions{};
    std::uint64_t viewsCreated{};
    std::uint64_t images{};
    std::uint64_t rejections{};
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

template <std::size_t N>
gfx::ShaderBinary spirvBinary(gfx::ShaderStage stage, const std::array<std::uint32_t, N>& words) {
    const auto raw = std::as_bytes(std::span{words});
    return {stage, std::vector<std::byte>(raw.begin(), raw.end()), gfx::ShaderFormat::SPIRV};
}

struct Pipelines {
    gfx::PipelinePtr depthWrite;
    gfx::PipelinePtr depthReadOnly;
};

Pipelines makeSampledPipelines(gfx::Device& device, gfx::Backend backend, Totals& totals) {
    gfx::ShaderBinary vs{}, ps{};
    if (backend == gfx::Backend::D3D12) {
        constexpr std::string_view hlsl = R"(
Texture2D tex0 : register(t0);
SamplerState samp0 : register(s0);
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut VS(uint id : SV_VertexID) {
    float2 p[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
    VSOut o;
    o.pos = float4(p[id % 3], 0.5, 1.0);
    o.uv = p[id % 3] * 0.5 + 0.5;
    o.uv.y = 1.0 - o.uv.y;
    return o;
}
float4 PS(VSOut i) : SV_Target0 { return tex0.Sample(samp0, i.uv); }
)";
        vs = take(gfx::compileHlsl(hlsl, "VS", gfx::ShaderStage::Vertex), "compile HLSL VS", totals);
        ps = take(gfx::compileHlsl(hlsl, "PS", gfx::ShaderStage::Fragment), "compile HLSL PS", totals);
    } else {
        vs = spirvBinary(gfx::ShaderStage::Vertex, bindingFixtures::uv);
        ps = spirvBinary(gfx::ShaderStage::Fragment, bindingFixtures::sample);
    }

    gfx::GraphicsPipelineDesc desc{vs, ps};
    desc.sampledTexture = true;
    desc.renderTargets = gfx::RenderTargetLayout{{gfx::Format::RGBA8UNorm}, gfx::Format::D32Float};
    desc.depthState = {true, true, gfx::CompareOp::Always};
    auto depthWrite = take(device.createGraphicsPipeline(desc), "create depthWrite pipeline", totals);

    desc.depthState = {true, false, gfx::CompareOp::LessEqual};
    auto depthReadOnly = take(device.createGraphicsPipeline(desc), "create depthReadOnly pipeline", totals);
    return {std::move(depthWrite), std::move(depthReadOnly)};
}

gfx::TexturePtr makeSolidTexture(
    gfx::Device& device,
    gfx::CommandList& cmd,
    std::uint8_t r,
    std::uint8_t g,
    std::uint8_t b,
    std::uint8_t a,
    Totals& totals) {
    auto tex = take(device.createTexture({4, 4, gfx::Format::RGBA8UNorm, false, false}), "create solid texture", totals);
    const auto fp = tex->footprint();
    std::vector<std::byte> bytes(static_cast<std::size_t>(fp.size), std::byte{0});
    for (std::uint32_t y = 0; y < 4; ++y) {
        for (std::uint32_t x = 0; x < 4; ++x) {
            const std::size_t off = static_cast<std::size_t>(y) * fp.rowPitch + static_cast<std::size_t>(x) * 4u;
            bytes[off + 0] = static_cast<std::byte>(r);
            bytes[off + 1] = static_cast<std::byte>(g);
            bytes[off + 2] = static_cast<std::byte>(b);
            bytes[off + 3] = static_cast<std::byte>(a);
        }
    }
    auto upload = take(device.createBuffer({fp.size, gfx::Memory::Upload}), "create solid upload", totals);
    take(upload->write(0, bytes), "write solid upload", totals);
    take(cmd.transition(tex, gfx::ResourceState::CopyDestination), "solid -> CopyDst", totals);
    take(cmd.copyBufferToTexture(upload, tex), "upload solid", totals);
    take(cmd.transition(tex, gfx::ResourceState::ShaderRead), "solid -> ShaderRead", totals);
    return tex;
}

void verifySolidRgba8(
    const gfx::BufferPtr& readback,
    const gfx::TextureFootprint& fp,
    std::uint8_t r,
    std::uint8_t g,
    std::uint8_t b,
    std::uint8_t a,
    Totals& totals) {
    const auto data = take(readback->read(0, fp.size), "readback color", totals);
    for (std::uint32_t y = 0; y < fp.height; ++y) {
        for (std::uint32_t x = 0; x < fp.rowBytes / 4u; ++x) {
            const std::size_t off = static_cast<std::size_t>(y) * fp.rowPitch + static_cast<std::size_t>(x) * 4u;
            const auto dr = std::abs(static_cast<int>(std::to_integer<std::uint8_t>(data[off + 0])) - static_cast<int>(r));
            const auto dg = std::abs(static_cast<int>(std::to_integer<std::uint8_t>(data[off + 1])) - static_cast<int>(g));
            const auto db = std::abs(static_cast<int>(std::to_integer<std::uint8_t>(data[off + 2])) - static_cast<int>(b));
            const auto da = std::abs(static_cast<int>(std::to_integer<std::uint8_t>(data[off + 3])) - static_cast<int>(a));
            if (dr > 1 || dg > 1 || db > 1 || da > 1) {
                throw std::runtime_error("Color pixel mismatch after TextureView rendering");
            }
        }
    }
    ++totals.assertions;
    ++totals.images;
}

void verifyUniformDepth(
    const gfx::BufferPtr& readback,
    const gfx::TextureFootprint& fp,
    float expectedDepth,
    Totals& totals) {
    const auto data = take(readback->read(0, fp.size), "readback depth", totals);
    for (std::uint32_t y = 0; y < fp.height; ++y) {
        for (std::uint32_t x = 0; x < fp.rowBytes / 4u; ++x) {
            float value{};
            std::memcpy(&value, data.data() + static_cast<std::size_t>(y) * fp.rowPitch + static_cast<std::size_t>(x) * 4u, 4);
            if (!std::isfinite(value) || std::abs(value - expectedDepth) > 0.0001f) {
                throw std::runtime_error("D32Float pixel mismatch after TextureView depth pass");
            }
        }
    }
    ++totals.assertions;
    ++totals.images;
}

void verifyReclaimed(gfx::Device& device, Totals& totals) {
    take(device.queue().waitIdle(), "waitIdle before reclamation check", totals);
    const auto stats = take(device.resourceStatistics(), "resourceStatistics", totals);
    check(stats.trackedObjects == 0, "All tracked objects must be reclaimed", totals);
    check(stats.uniqueResources == 0, "All unique resources must be reclaimed", totals);
    check(stats.resourceAllocationBytes == 0, "All resource allocation bytes must be reclaimed", totals);
    check(stats.pendingSubmissions == 0, "No pending submissions may remain", totals);
    check(stats.pendingObjectReferences == 0, "No pending object references may remain", totals);
}

void runTextureViewSuite(
    gfx::Device& device,
    gfx::Device& foreignDevice,
    gfx::Backend backend,
    bool validation,
    Totals& totals) {
    constexpr std::uint32_t kSize = 16;

    {
        auto pipelines = makeSampledPipelines(device, backend, totals);
        auto cmd = take(device.createCommandList(), "createCommandList", totals);

        auto colorTarget = take(device.createTexture({kSize, kSize, gfx::Format::RGBA8UNorm, true, false}), "create colorTarget", totals);
        auto depthTarget = take(device.createTexture({kSize, kSize, gfx::Format::D32Float, false, true}), "create depthTarget", totals);
        const auto colorFp = colorTarget->footprint();
        const auto depthFp = depthTarget->footprint();
        auto colorReadback = take(device.createBuffer({colorFp.size, gfx::Memory::Readback}), "create colorReadback", totals);
        auto depthReadback = take(device.createBuffer({depthFp.size, gfx::Memory::Readback}), "create depthReadback", totals);

        // Create validated RTV & DSV views (testing both default format inheritance and explicit format).
        auto rtvView = take(device.createTextureView(colorTarget, {std::nullopt, gfx::TextureViewUsage::RenderTarget}), "create rtvView", totals);
        ++totals.viewsCreated;
        check(rtvView->description().format == gfx::Format::RGBA8UNorm &&
              rtvView->description().usage == gfx::TextureViewUsage::RenderTarget &&
              rtvView->texture() == colorTarget,
              "RTV TextureView metadata must resolve format and retain parent texture", totals);

        auto dsvView = take(device.createTextureView(depthTarget, {gfx::Format::D32Float, gfx::TextureViewUsage::DepthStencil}), "create dsvView", totals);
        ++totals.viewsCreated;
        check(dsvView->description().format == gfx::Format::D32Float &&
              dsvView->description().usage == gfx::TextureViewUsage::DepthStencil &&
              dsvView->texture() == depthTarget,
              "DSV TextureView metadata must match", totals);

        // Create SRV views over two source textures and verify creation/binding rejections.
        take(cmd->begin(), "begin upload + pass 1", totals);
        auto src1 = makeSolidTexture(device, *cmd, 220, 40, 80, 255, totals);
        auto src2 = makeSolidTexture(device, *cmd, 30, 190, 110, 255, totals);
        auto srv1 = take(device.createTextureView(src1), "create default srv1", totals);
        ++totals.viewsCreated;
        check(srv1->description().format == gfx::Format::RGBA8UNorm &&
              srv1->description().usage == gfx::TextureViewUsage::ShaderRead,
              "Default SRV TextureView metadata", totals);
        auto srv2 = take(device.createTextureView(src2, {gfx::Format::RGBA8UNorm, gfx::TextureViewUsage::ShaderRead, 0, 1, 0, 1}),
                         "create explicit srv2", totals);
        ++totals.viewsCreated;

        // Creation rejections.
        reject(device.createTextureView(nullptr), gfx::ErrorCode::InvalidArgument, "null texture view", totals);
        reject(device.createTextureView(colorTarget, {std::nullopt, gfx::TextureViewUsage::Storage}),
               gfx::ErrorCode::InvalidArgument, "Storage/UAV view requires storage texture", totals);
        reject(device.createTextureView(colorTarget, {std::nullopt, static_cast<gfx::TextureViewUsage>(77)}),
               gfx::ErrorCode::InvalidArgument, "Unknown view usage", totals);
        reject(device.createTextureView(colorTarget, {gfx::Format::BGRA8UNorm, gfx::TextureViewUsage::ShaderRead}),
               gfx::ErrorCode::Unsupported, "Cross-format reinterpretation unsupported", totals);
        reject(device.createTextureView(colorTarget, {gfx::Format::D32Float, gfx::TextureViewUsage::ShaderRead}),
               gfx::ErrorCode::InvalidArgument, "Color-to-depth view rejected", totals);
        reject(device.createTextureView(depthTarget, {std::nullopt, gfx::TextureViewUsage::ShaderRead}),
               gfx::ErrorCode::Unsupported, "Depth SRV view unsupported", totals);
        reject(device.createTextureView(src1, {std::nullopt, gfx::TextureViewUsage::RenderTarget}),
               gfx::ErrorCode::InvalidArgument, "RTV on non-RT texture rejected", totals);
        reject(device.createTextureView(colorTarget, {std::nullopt, gfx::TextureViewUsage::DepthStencil}),
               gfx::ErrorCode::InvalidArgument, "DSV on color texture rejected", totals);
        reject(device.createTextureView(colorTarget, {std::nullopt, gfx::TextureViewUsage::ShaderRead, 1, 1, 0, 1}),
               gfx::ErrorCode::InvalidArgument, "Mip out of range rejected", totals);
        reject(device.createTextureView(colorTarget, {std::nullopt, gfx::TextureViewUsage::ShaderRead, 0, 1, 1, 1}),
               gfx::ErrorCode::InvalidArgument, "Array layer out of range rejected", totals);

        // Foreign device rejections.
        auto foreignColor = take(foreignDevice.createTexture({kSize, kSize, gfx::Format::RGBA8UNorm, true, false}), "foreign texture", totals);
        auto foreignSrv = take(foreignDevice.createTextureView(foreignColor, {std::nullopt, gfx::TextureViewUsage::ShaderRead}), "foreign srv", totals);
        auto foreignRtv = take(foreignDevice.createTextureView(foreignColor, {std::nullopt, gfx::TextureViewUsage::RenderTarget}), "foreign rtv", totals);
        reject(device.createTextureView(foreignColor), gfx::ErrorCode::WrongDevice, "foreign parent texture rejected", totals);

        // Off-thread createTextureView rejection.
        {
            gfx::Result<gfx::TextureViewPtr> offThread =
                std::unexpected(gfx::Error{gfx::ErrorCode::BackendFailure, "not run"});
            std::thread worker([&] {
                offThread = device.createTextureView(colorTarget);
            });
            worker.join();
            reject(offThread, gfx::ErrorCode::InvalidState, "off-thread createTextureView rejected", totals);
        }

        // Transition targets and test render-pass view validation rejections.
        take(cmd->transition(colorTarget, gfx::ResourceState::RenderTarget), "color -> RT", totals);
        take(cmd->transition(depthTarget, gfx::ResourceState::DepthWrite), "depth -> DepthWrite", totals);

        reject(cmd->beginRenderPass({{{colorTarget, gfx::LoadOp::Clear, {}, rtvView}}, {}}),
               gfx::ErrorCode::InvalidArgument, "both texture and view on ColorAttachment rejected", totals);
        reject(cmd->beginRenderPass({{{nullptr, gfx::LoadOp::Clear, {}, srv1}}, {}}),
               gfx::ErrorCode::InvalidArgument, "ShaderRead view as ColorAttachment rejected", totals);
        reject(cmd->beginRenderPass({{}, {nullptr, gfx::LoadOp::Clear, 1.0f, false, rtvView}}),
               gfx::ErrorCode::InvalidArgument, "RenderTarget view as DepthAttachment rejected", totals);
        reject(cmd->beginRenderPass({{{nullptr, gfx::LoadOp::Clear, {}, foreignRtv}}, {}}),
               gfx::ErrorCode::WrongDevice, "foreign RTV view rejected", totals);
        reject(cmd->beginRenderPass({{{colorTarget, gfx::LoadOp::Clear, {}}, {nullptr, gfx::LoadOp::Clear, {}, rtvView}}, {}}),
               gfx::ErrorCode::InvalidArgument, "duplicate underlying texture via raw+view rejected", totals);

        // Pass 1: Render using ONLY rtvView + dsvView + bindTextureView(srv1).
        gfx::RenderPassDesc pass1{
            {{nullptr, gfx::LoadOp::Clear, {0.0f, 0.0f, 0.0f, 1.0f}, rtvView}},
            {nullptr, gfx::LoadOp::Clear, 1.0f, false, dsvView}
        };
        take(cmd->beginRenderPass(pass1), "beginRenderPass pass1 with views", totals);
        take(cmd->setPipeline(pipelines.depthWrite), "setPipeline depthWrite", totals);

        // Binding rejections must not corrupt the open pass.
        reject(cmd->bindTextureView(nullptr), gfx::ErrorCode::InvalidArgument, "null bindTextureView rejected", totals);
        reject(cmd->bindTextureView(rtvView), gfx::ErrorCode::InvalidArgument, "RTV in bindTextureView rejected", totals);
        reject(cmd->bindTextureView(foreignSrv), gfx::ErrorCode::WrongDevice, "foreign SRV in bindTextureView rejected", totals);
        reject(cmd->bindResources({{{0, 0, src1, srv1}}, {}}), gfx::ErrorCode::InvalidArgument,
               "both texture and view in TextureBinding rejected", totals);

        take(cmd->bindTextureView(srv1), "bindTextureView srv1", totals);
        take(cmd->draw(3), "draw pass1", totals);
        take(cmd->endRenderPass(), "endRenderPass pass1", totals);

        take(cmd->transition(colorTarget, gfx::ResourceState::CopySource), "color -> CopySrc", totals);
        take(cmd->copyTextureToBuffer(colorTarget, colorReadback), "copy color pass1", totals);
        take(cmd->transition(depthTarget, gfx::ResourceState::CopySource), "depth -> CopySrc", totals);
        take(cmd->copyTextureToBuffer(depthTarget, depthReadback), "copy depth pass1", totals);
        take(cmd->end(), "end pass1", totals);

        const auto f1 = take(device.queue().submit(*cmd), "submit pass1", totals);
        take(device.queue().wait(f1), "wait pass1", totals);
        verifySolidRgba8(colorReadback, colorFp, 220, 40, 80, 255, totals);
        verifyUniformDepth(depthReadback, depthFp, 0.5f, totals);

        // Pass 2: Read-only depth view + bindResources with srv2, after dropping caller's src2 TexturePtr!
        src2.reset(); // srv2 must keep the underlying texture alive.
        check(srv2->texture() != nullptr, "TextureView must keep parent Texture alive after caller drops TexturePtr", totals);

        take(cmd->begin(), "begin pass2", totals);
        take(cmd->transition(colorTarget, gfx::ResourceState::RenderTarget), "color -> RT pass2", totals);
        take(cmd->transition(depthTarget, gfx::ResourceState::DepthRead), "depth -> DepthRead pass2", totals);
        gfx::RenderPassDesc pass2{
            {{nullptr, gfx::LoadOp::Load, {}, rtvView}},
            {nullptr, gfx::LoadOp::Load, 1.0f, true, dsvView}
        };
        take(cmd->beginRenderPass(pass2), "beginRenderPass pass2 read-only DSV view", totals);
        take(cmd->setPipeline(pipelines.depthReadOnly), "setPipeline depthReadOnly", totals);
        take(cmd->bindResources({{{0, 0, nullptr, srv2}}, {}}), "bindResources with srv2", totals);
        take(cmd->draw(3), "draw pass2", totals);
        take(cmd->endRenderPass(), "endRenderPass pass2", totals);
        take(cmd->transition(colorTarget, gfx::ResourceState::CopySource), "color -> CopySrc pass2", totals);
        take(cmd->copyTextureToBuffer(colorTarget, colorReadback), "copy color pass2", totals);
        take(cmd->transition(depthTarget, gfx::ResourceState::CopySource), "depth -> CopySrc pass2", totals);
        take(cmd->copyTextureToBuffer(depthTarget, depthReadback), "copy depth pass2", totals);
        take(cmd->end(), "end pass2", totals);

        const auto f2 = take(device.queue().submit(*cmd), "submit pass2", totals);
        take(device.queue().wait(f2), "wait pass2", totals);
        verifySolidRgba8(colorReadback, colorFp, 30, 190, 110, 255, totals);
        verifyUniformDepth(depthReadback, depthFp, 0.5f, totals);

        // Pass 3: Destroy ALL TexturePtrs and TextureViewPtrs BEFORE queue.wait() to test GPU retention.
        gfx::Fence f3{};
        {
            auto ephemeralColor = take(device.createTexture({kSize, kSize, gfx::Format::RGBA8UNorm, true, false}), "ephemeralColor", totals);
            auto ephemeralDepth = take(device.createTexture({kSize, kSize, gfx::Format::D32Float, false, true}), "ephemeralDepth", totals);
            auto ephemeralRtv = take(device.createTextureView(ephemeralColor, {std::nullopt, gfx::TextureViewUsage::RenderTarget}), "ephemeralRtv", totals);
            auto ephemeralDsv = take(device.createTextureView(ephemeralDepth, {std::nullopt, gfx::TextureViewUsage::DepthStencil}), "ephemeralDsv", totals);
            ++totals.viewsCreated;
            ++totals.viewsCreated;

            take(cmd->begin(), "begin pass3 ephemeral views", totals);
            auto ephemeralSrc = makeSolidTexture(device, *cmd, 90, 120, 240, 255, totals);
            auto ephemeralSrv = take(device.createTextureView(ephemeralSrc), "ephemeralSrv", totals);
            ++totals.viewsCreated;

            take(cmd->transition(ephemeralColor, gfx::ResourceState::RenderTarget), "ephemeralColor -> RT", totals);
            take(cmd->transition(ephemeralDepth, gfx::ResourceState::DepthWrite), "ephemeralDepth -> DepthWrite", totals);
            take(cmd->beginRenderPass({
                {{nullptr, gfx::LoadOp::Clear, {0, 0, 0, 1}, ephemeralRtv}},
                {nullptr, gfx::LoadOp::Clear, 1.0f, false, ephemeralDsv}
            }), "beginRenderPass pass3", totals);
            take(cmd->setPipeline(pipelines.depthWrite), "setPipeline pass3", totals);
            take(cmd->bindTextureView(ephemeralSrv), "bindTextureView ephemeralSrv", totals);
            take(cmd->draw(3), "draw pass3", totals);
            take(cmd->endRenderPass(), "endRenderPass pass3", totals);
            take(cmd->transition(ephemeralColor, gfx::ResourceState::CopySource), "ephemeralColor -> CopySrc", totals);
            take(cmd->copyTextureToBuffer(ephemeralColor, colorReadback), "copy ephemeralColor", totals);
            take(cmd->transition(ephemeralDepth, gfx::ResourceState::CopySource), "ephemeralDepth -> CopySrc", totals);
            take(cmd->copyTextureToBuffer(ephemeralDepth, depthReadback), "copy ephemeralDepth", totals);
            take(cmd->end(), "end pass3", totals);
            f3 = take(device.queue().submit(*cmd), "submit pass3", totals);
            // Intentionally drop all ephemeral textures and views while GPU batch is in flight!
        }
        take(device.queue().wait(f3), "wait pass3 after view/texture destruction", totals);
        verifySolidRgba8(colorReadback, colorFp, 90, 120, 240, 255, totals);
        verifyUniformDepth(depthReadback, depthFp, 0.5f, totals);
    }

    verifyReclaimed(device, totals);

    if (validation) {
        const auto errors = take(device.validationErrorCount(), "validationErrorCount", totals);
        check(errors == 0, "Validation error count must be zero", totals);
    }
}

void runWarpLossRecoveryCheck(Totals& totals) {
    gfx::DeviceDesc desc{};
    desc.backend = gfx::Backend::D3D12;
    desc.softwareAdapter = true;
    desc.enableDeviceLossTesting = true;
    const auto adapters = take(gfx::enumerateAdapters(gfx::Backend::D3D12), "enumerate D3D12 for loss check", totals);
    for (const auto& a : adapters) {
        if (a.softwareAdapter) {
            desc.adapter = a.id;
            break;
        }
    }
    check(desc.adapter.has_value(), "WARP adapter required for loss recovery", totals);

    auto device = take(gfx::createDevice(desc), "create WARP loss device", totals);
    auto tex = take(device->createTexture({16, 16, gfx::Format::RGBA8UNorm, true, false}), "loss tex", totals);
    auto oldView = take(device->createTextureView(tex, {std::nullopt, gfx::TextureViewUsage::RenderTarget}), "loss rtv", totals);
    ++totals.viewsCreated;

    take(device->removeForTesting(), "removeForTesting", totals);
    reject(device->createTextureView(tex), gfx::ErrorCode::DeviceLost, "createTextureView after loss", totals);
    check(oldView->description().format == gfx::Format::RGBA8UNorm &&
          oldView->description().usage == gfx::TextureViewUsage::RenderTarget,
          "Old TextureView keeps CPU description after loss", totals);

    auto recovered = take(device->recover(), "recover WARP device", totals);
    {
        auto cmd = take(recovered->createCommandList(), "recovered cmd", totals);
        take(cmd->begin(), "recovered begin", totals);
        reject(cmd->beginRenderPass({{{nullptr, gfx::LoadOp::Clear, {}, oldView}}, {}}),
               gfx::ErrorCode::WrongDevice, "old TextureView rejected on recovered device", totals);
        auto freshTex = take(recovered->createTexture({16, 16, gfx::Format::RGBA8UNorm, true, false}), "freshTex", totals);
        auto freshRtv = take(recovered->createTextureView(freshTex, {std::nullopt, gfx::TextureViewUsage::RenderTarget}), "freshRtv", totals);
        ++totals.viewsCreated;
        const auto fp = freshTex->footprint();
        auto rb = take(recovered->createBuffer({fp.size, gfx::Memory::Readback}), "fresh rb", totals);
        take(cmd->transition(freshTex, gfx::ResourceState::RenderTarget), "fresh -> RT", totals);
        take(cmd->beginRenderPass({{{nullptr, gfx::LoadOp::Clear, {0.2f, 0.4f, 0.8f, 1.0f}, freshRtv}}, {}}),
             "fresh pass with RTV view", totals);
        take(cmd->endRenderPass(), "fresh endRenderPass", totals);
        take(cmd->transition(freshTex, gfx::ResourceState::CopySource), "fresh -> CopySrc", totals);
        take(cmd->copyTextureToBuffer(freshTex, rb), "fresh copy", totals);
        take(cmd->end(), "fresh end", totals);
        const auto f = take(recovered->queue().submit(*cmd), "fresh submit", totals);
        take(recovered->queue().wait(f), "fresh wait", totals);
        verifySolidRgba8(rb, fp, 51, 102, 204, 255, totals);
    }
    verifyReclaimed(*recovered, totals);
}
} // namespace

int main(int argc, char** argv) {
    try {
        gfx::Backend backend = gfx::Backend::Vulkan;
        bool validation = false;
        bool warp = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--d3d12") backend = gfx::Backend::D3D12;
            else if (arg == "--vulkan") backend = gfx::Backend::Vulkan;
            else if (arg == "--metal") backend = gfx::Backend::Metal;
            else if (arg == "--validation") validation = true;
            else if (arg == "--warp") warp = true;
            else throw std::runtime_error("Unknown argument: " + arg);
        }

        const auto adapters = gfx::enumerateAdapters(backend);
        if (!adapters) {
            std::cerr << "Adapter enumeration failed: " << adapters.error().message << '\n';
            return 1;
        }
        std::vector<gfx::AdapterInfo> selected;
        for (const auto& adapter : *adapters) {
            if (warp ? adapter.softwareAdapter : !adapter.softwareAdapter) {
                selected.push_back(adapter);
            }
        }
        if (selected.empty()) {
            std::cout << "SKIP no matching adapters\n";
            return 77;
        }

        Totals totals;
        for (const auto& adapter : selected) {
            gfx::DeviceDesc desc{};
            desc.backend = backend;
            desc.adapter = adapter.id;
            desc.softwareAdapter = warp;
            desc.validation = validation;
            auto device = take(gfx::createDevice(desc), "createDevice", totals);
            gfx::DeviceDesc foreignDesc = desc;
            foreignDesc.validation = false;
            auto foreignDevice = take(gfx::createDevice(foreignDesc), "createForeignDevice", totals);
            runTextureViewSuite(*device, *foreignDevice, backend, validation, totals);
        }

        if (backend == gfx::Backend::D3D12 && warp && !validation) {
            runWarpLossRecoveryCheck(totals);
        }

        std::cout << "PASS texture_view adapters=" << selected.size()
                  << " assertions=" << totals.assertions
                  << " views=" << totals.viewsCreated
                  << " images=" << totals.images
                  << " rejections=" << totals.rejections
                  << " errors=0\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL texture_view: " << error.what() << '\n';
        return 1;
    }
}
