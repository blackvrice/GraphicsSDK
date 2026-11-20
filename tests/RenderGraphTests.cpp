#include <graphics/Graphics.hpp>
#include "fixtures/BindingFixtures.hpp"
#include "fixtures/ComputeFixtures.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Totals {
    std::uint64_t assertions{};
    std::uint64_t graphsCompiled{};
    std::uint64_t graphsExecuted{};
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

gfx::PipelinePtr makeBufferComputePipeline(gfx::Device& device, gfx::Backend backend, Totals& totals) {
    gfx::ShaderBinary cs{};
    if (backend == gfx::Backend::D3D12) {
        constexpr std::string_view hlsl = R"(
RWByteAddressBuffer buf : register(u0);
cbuffer Params : register(b0) { float4 params; };
[numthreads(64, 1, 1)]
void CS(uint3 gid : SV_DispatchThreadID) {
    uint byteOff = gid.x * 4u;
    uint val = buf.Load(byteOff);
    uint scale = (uint)params.x;
    uint add = (uint)params.y;
    buf.Store(byteOff, val * scale + add);
}
)";
        cs = take(gfx::compileHlsl(hlsl, "CS", gfx::ShaderStage::Compute), "compile HLSL buffer CS", totals);
    } else {
        cs = spirvBinary(gfx::ShaderStage::Compute, computeFixtures::buffer_scale_add);
    }
    gfx::ComputePipelineDesc desc{};
    desc.compute = std::move(cs);
    desc.bindingLayout.entries = {
        {gfx::BindingType::StorageBuffer, 0, 1, 0, gfx::ShaderVisibility::Compute},
        {gfx::BindingType::ConstantBuffer, 0, 1, 0, gfx::ShaderVisibility::Compute}
    };
    desc.threadgroupSizeX = 64;
    desc.threadgroupSizeY = 1;
    desc.threadgroupSizeZ = 1;
    return take(device.createComputePipeline(desc), "create buffer compute pipeline", totals);
}

gfx::PipelinePtr makeTextureComputePipeline(gfx::Device& device, gfx::Backend backend, Totals& totals) {
    gfx::ShaderBinary cs{};
    if (backend == gfx::Backend::D3D12) {
        constexpr std::string_view hlsl = R"(
RWTexture2D<unorm float4> img : register(u0);
cbuffer Params : register(b0) { float4 color; };
[numthreads(8, 8, 1)]
void CS(uint3 gid : SV_DispatchThreadID) {
    float nx = (float)gid.x / 15.0;
    float ny = (float)gid.y / 15.0;
    img[gid.xy] = float4(color.r * nx, color.g * ny, color.b, 1.0);
}
)";
        cs = take(gfx::compileHlsl(hlsl, "CS", gfx::ShaderStage::Compute), "compile HLSL texture CS", totals);
    } else {
        cs = spirvBinary(gfx::ShaderStage::Compute, computeFixtures::texture_generate);
    }
    gfx::ComputePipelineDesc desc{};
    desc.compute = std::move(cs);
    desc.bindingLayout.entries = {
        {gfx::BindingType::StorageTexture2D, 0, 1, 0, gfx::ShaderVisibility::Compute},
        {gfx::BindingType::ConstantBuffer, 0, 1, 0, gfx::ShaderVisibility::Compute}
    };
    desc.threadgroupSizeX = 8;
    desc.threadgroupSizeY = 8;
    desc.threadgroupSizeZ = 1;
    return take(device.createComputePipeline(desc), "create texture compute pipeline", totals);
}

gfx::PipelinePtr makeSampledGraphicsPipeline(gfx::Device& device, gfx::Backend backend, Totals& totals) {
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
    desc.colorFormat = gfx::Format::RGBA8UNorm;
    return take(device.createGraphicsPipeline(desc), "create sampled graphics pipeline", totals);
}

void verifyGradientPixels(const std::vector<std::byte>& pixels, const gfx::TextureFootprint& fp,
                          float kR, float kG, float kB, Totals& totals) {
    check(pixels.size() == fp.size, "gradient readback byte count", totals);
    for (std::uint32_t y = 0; y < fp.height; ++y) {
        const std::uint32_t sampleY = fp.height - 1u - y;
        for (std::uint32_t x = 0; x < fp.rowBytes / 4u; ++x) {
            const std::size_t off = static_cast<std::size_t>(y) * fp.rowPitch + static_cast<std::size_t>(x) * 4u;
            const int er = static_cast<int>(std::lround(kR * (static_cast<float>(x) / 15.0f) * 255.0f));
            const int eg = static_cast<int>(std::lround(kG * (static_cast<float>(sampleY) / 15.0f) * 255.0f));
            const int eb = static_cast<int>(std::lround(kB * 255.0f));
            const int gr = static_cast<int>(pixels[off + 0]);
            const int gg = static_cast<int>(pixels[off + 1]);
            const int gb = static_cast<int>(pixels[off + 2]);
            const int ga = static_cast<int>(pixels[off + 3]);
            if (std::abs(gr - er) > 2 || std::abs(gg - eg) > 2 || std::abs(gb - eb) > 2 || std::abs(ga - 255) > 2) {
                throw std::runtime_error("Gradient pixel mismatch at (" + std::to_string(x) + "," + std::to_string(y) +
                    "): expected (" + std::to_string(er) + "," + std::to_string(eg) + "," + std::to_string(eb) + ",255), got (" +
                    std::to_string(gr) + "," + std::to_string(gg) + "," + std::to_string(gb) + "," + std::to_string(ga) + ")");
            }
        }
    }
    ++totals.assertions;
}

void verifySolidPixels(const std::vector<std::byte>& pixels, const gfx::TextureFootprint& fp,
                       int r, int g, int b, int a, const char* label, Totals& totals) {
    check(pixels.size() == fp.size, "solid readback byte count", totals);
    for (std::uint32_t y = 0; y < fp.height; ++y) {
        for (std::uint32_t x = 0; x < fp.rowBytes / 4u; ++x) {
            const std::size_t off = static_cast<std::size_t>(y) * fp.rowPitch + static_cast<std::size_t>(x) * 4u;
            const int gr = static_cast<int>(pixels[off + 0]);
            const int gg = static_cast<int>(pixels[off + 1]);
            const int gb = static_cast<int>(pixels[off + 2]);
            const int ga = static_cast<int>(pixels[off + 3]);
            if (gr != r || gg != g || gb != b || ga != a) {
                throw std::runtime_error(std::string(label) + " solid pixel mismatch at (" +
                    std::to_string(x) + "," + std::to_string(y) + "): expected (" +
                    std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) + "," + std::to_string(a) +
                    "), got (" + std::to_string(gr) + "," + std::to_string(gg) + "," + std::to_string(gb) + "," + std::to_string(ga) + ")");
            }
        }
    }
    ++totals.assertions;
}

void testCpuCompileContracts(gfx::Device& device, Totals& totals) {
    auto dummyUpload = take(device.createBuffer({256, gfx::Memory::Upload}), "contract upload", totals);
    auto dummyReadback = take(device.createBuffer({256, gfx::Memory::Readback}), "contract readback", totals);
    auto dummyTex = take(device.createTexture({16, 16, gfx::Format::RGBA8UNorm, true}), "contract tex", totals);
    auto noop = [](gfx::CommandList&, const gfx::RenderGraphResources&) -> gfx::Result<void> { return {}; };

    // 1. Validation rejections
    {
        gfx::RenderGraph g;
        reject(g.createBuffer("", {256, gfx::Memory::Device}), gfx::ErrorCode::InvalidArgument, "empty buffer name", totals);
        reject(g.createBuffer("zero", {0, gfx::Memory::Device}), gfx::ErrorCode::InvalidArgument, "zero size buffer", totals);
        reject(g.createBuffer("up", {256, gfx::Memory::Upload}), gfx::ErrorCode::InvalidArgument, "upload transient buffer", totals);
        reject(g.createTexture("", {16, 16, gfx::Format::RGBA8UNorm, true}), gfx::ErrorCode::InvalidArgument, "empty tex name", totals);
        reject(g.createTexture("zero", {0, 16, gfx::Format::RGBA8UNorm, true}), gfx::ErrorCode::InvalidArgument, "zero dim tex", totals);
        reject(g.importBuffer("null", nullptr), gfx::ErrorCode::InvalidArgument, "null imported buffer", totals);
        reject(g.importTexture("null", nullptr), gfx::ErrorCode::InvalidArgument, "null imported texture", totals);
        reject(g.importBuffer("badInit", dummyUpload, gfx::ResourceState::Common), gfx::ErrorCode::InvalidArgument, "upload Common init", totals);
        reject(g.importBuffer("badFinal", dummyReadback, gfx::ResourceState::CopyDestination, gfx::ResourceState::Common),
               gfx::ErrorCode::InvalidArgument, "readback Common final", totals);
        reject(g.execute(device), gfx::ErrorCode::InvalidState, "execute before compile", totals);

        auto hUp = take(g.importBuffer("up", dummyUpload), "import upload", totals);
        auto hRb = take(g.importBuffer("rb", dummyReadback), "import readback", totals);
        auto hTex = take(g.createTexture("rt", {16, 16, gfx::Format::RGBA8UNorm, true}), "create rt", totals);

        reject(g.addPass({"", gfx::QueueType::Graphics, false, {}, {}, {}, noop}), gfx::ErrorCode::InvalidArgument, "empty pass name", totals);
        reject(g.addPass({"noFn", gfx::QueueType::Graphics, false, {}, {}, {}, {}}), gfx::ErrorCode::InvalidArgument, "null callback", totals);
        reject(g.addPass({"badHandle", gfx::QueueType::Graphics, false, {{gfx::RGResourceHandle{999}, gfx::ResourceState::ShaderRead}}, {}, {}, noop}),
               gfx::ErrorCode::InvalidArgument, "unknown resource handle", totals);
        reject(g.addPass({"writeUpload", gfx::QueueType::Copy, false, {}, {{hUp, gfx::ResourceState::CopySource}}, {}, noop}),
               gfx::ErrorCode::InvalidArgument, "write upload buffer", totals);
        reject(g.addPass({"readReadback", gfx::QueueType::Copy, false, {{hRb, gfx::ResourceState::CopyDestination}}, {}, {}, noop}),
               gfx::ErrorCode::InvalidArgument, "read readback buffer", totals);
        reject(g.addPass({"copyShaderRead", gfx::QueueType::Copy, false, {{hTex, gfx::ResourceState::ShaderRead}}, {}, {}, noop}),
               gfx::ErrorCode::InvalidArgument, "Copy queue ShaderRead", totals);
        reject(g.addPass({"nonStorageUav", gfx::QueueType::Compute, false, {}, {{hTex, gfx::ResourceState::UnorderedAccess}}, {}, noop}),
               gfx::ErrorCode::InvalidArgument, "non-storage UAV", totals);
        reject(g.addPass({"conflict", gfx::QueueType::Graphics, false,
                          {{hTex, gfx::ResourceState::ShaderRead}},
                          {{hTex, gfx::ResourceState::RenderTarget}}, {}, noop}),
               gfx::ErrorCode::InvalidArgument, "conflicting states in same pass", totals);
    }

    // 2. Cycle detection (mutual cycle & self-cycle)
    {
        gfx::RenderGraph g;
        (void)take(g.addPass({"A", gfx::QueueType::Graphics, true, {}, {}, {1}, noop}), "add A", totals);
        (void)take(g.addPass({"B", gfx::QueueType::Graphics, true, {}, {}, {0}, noop}), "add B", totals);
        reject(g.compile(), gfx::ErrorCode::InvalidState, "2-node dependency cycle", totals);
    }
    {
        gfx::RenderGraph g;
        (void)take(g.addPass({"Self", gfx::QueueType::Graphics, true, {}, {}, {0}, noop}), "add Self", totals);
        reject(g.compile(), gfx::ErrorCode::InvalidState, "self dependency cycle", totals);
    }

    // 3. Dead pass culling + transient culling + interval-coloring aliasing + diagnostics
    {
        gfx::RenderGraph g;
        const gfx::TextureDesc rtDesc{64, 64, gfx::Format::RGBA8UNorm, true};
        auto outTex = take(g.importTexture("outTex", dummyTex), "import outTex", totals);
        auto deadA = take(g.createTexture("deadA", rtDesc), "create deadA", totals);
        auto deadB = take(g.createTexture("deadB", rtDesc), "create deadB", totals);
        auto t0 = take(g.createTexture("t0", rtDesc), "create t0", totals);
        auto t1 = take(g.createTexture("t1", rtDesc), "create t1", totals);
        auto t2 = take(g.createTexture("t2", rtDesc), "create t2", totals);

        // Dead chain (2 passes, 2 transients)
        (void)take(g.addPass({"Dead0", gfx::QueueType::Graphics, false, {}, {{deadA, gfx::ResourceState::RenderTarget}}, {}, noop}), "Dead0", totals);
        (void)take(g.addPass({"Dead1", gfx::QueueType::Graphics, false, {{deadA, gfx::ResourceState::ShaderRead}}, {{deadB, gfx::ResourceState::RenderTarget}}, {}, noop}), "Dead1", totals);

        // Non-overlapping sequential chain where t0, t1, t2 have disjoint lifetimes:
        // Pass 2: writes t0 -> Pass 3: reads t0, writes outTex (t0 ends at exec 1)
        // Pass 4: writes t1 (starts at exec 2 > 1!) -> Pass 5: reads t1, writes outTex (t1 ends at exec 3)
        // Pass 6: writes t2 (starts at exec 4 > 3!) -> Pass 7: reads t2, writes outTex (t2 ends at exec 5)
        (void)take(g.addPass({"WriteT0", gfx::QueueType::Graphics, false, {}, {{t0, gfx::ResourceState::RenderTarget}}, {}, noop}), "WriteT0", totals);
        (void)take(g.addPass({"UseT0", gfx::QueueType::Graphics, false, {{t0, gfx::ResourceState::ShaderRead}}, {{outTex, gfx::ResourceState::RenderTarget}}, {}, noop}), "UseT0", totals);
        (void)take(g.addPass({"WriteT1", gfx::QueueType::Graphics, false, {}, {{t1, gfx::ResourceState::RenderTarget}}, {3}, noop}), "WriteT1", totals);
        (void)take(g.addPass({"UseT1", gfx::QueueType::Graphics, false, {{t1, gfx::ResourceState::ShaderRead}}, {{outTex, gfx::ResourceState::RenderTarget}}, {}, noop}), "UseT1", totals);
        (void)take(g.addPass({"WriteT2", gfx::QueueType::Graphics, false, {}, {{t2, gfx::ResourceState::RenderTarget}}, {5}, noop}), "WriteT2", totals);
        (void)take(g.addPass({"UseT2", gfx::QueueType::Graphics, false, {{t2, gfx::ResourceState::ShaderRead}}, {{outTex, gfx::ResourceState::RenderTarget}}, {}, noop}), "UseT2", totals);

        const auto st = take(g.compile(), "compile aliasing+culling graph", totals);
        ++totals.graphsCompiled;
        check(st.totalPasses == 8u, "totalPasses == 8", totals);
        check(st.executedPasses == 6u, "executedPasses == 6", totals);
        check(st.culledPasses == 2u, "culledPasses == 2", totals);
        check(st.transientResourcesDeclared == 5u, "transientResourcesDeclared == 5", totals);
        check(st.transientResourcesCulled == 2u, "transientResourcesCulled == 2", totals);
        check(st.physicalTransientSlots == 1u, "3 disjoint transients alias to 1 physical slot", totals);
        check(st.aliasedResources == 2u, "aliasedResources == 2", totals);

        const std::string text = g.dumpText();
        const std::string dot = g.dumpDot();
        check(text.find("compiled") != std::string::npos, "dumpText contains compiled", totals);
        check(text.find("[CULLED]") != std::string::npos, "dumpText lists culled passes", totals);
        check(text.find("slot#") != std::string::npos, "dumpText lists physical slots", totals);
        check(dot.find("digraph RenderGraph") != std::string::npos, "dumpDot header", totals);
        check(dot.find("CULLED") != std::string::npos, "dumpDot marks culled", totals);
        check(dot.find("->") != std::string::npos, "dumpDot contains edges", totals);
    }

    // 4. All-dead graph executes cleanly as a no-op
    {
        gfx::RenderGraph g;
        auto unused = take(g.createTexture("unused", {16, 16, gfx::Format::RGBA8UNorm, true}), "unused", totals);
        bool ran = false;
        (void)take(g.addPass({"AllDead", gfx::QueueType::Graphics, false, {}, {{unused, gfx::ResourceState::RenderTarget}}, {},
                              [&](gfx::CommandList&, const gfx::RenderGraphResources&) -> gfx::Result<void> {
                                  ran = true;
                                  return {};
                              }}), "AllDead", totals);
        const auto st = take(g.compile(), "compile all-dead", totals);
        ++totals.graphsCompiled;
        check(st.executedPasses == 0u && st.culledPasses == 1u, "all-dead culled", totals);
        take(g.execute(device), "execute all-dead", totals);
        ++totals.graphsExecuted;
        check(!ran, "all-dead pass never executed", totals);
    }
}

void testCompositeGraphicsComputeMultiQueueGraph(gfx::Device& device, gfx::Backend backend, Totals& totals) {
    constexpr float kR = 0.85f, kG = 0.55f, kB = 0.25f;
    auto cbUpload = take(device.createBuffer({256, gfx::Memory::Upload}), "rg cbUpload", totals);
    std::array<float, 4> colorParams{kR, kG, kB, 1.0f};
    take(cbUpload->write(0, std::as_bytes(std::span{colorParams})), "rg write colorParams", totals);

    gfx::TextureDesc storageDesc{16, 16, gfx::Format::RGBA8UNorm, false, false, true};
    gfx::TextureDesc rtDesc{16, 16, gfx::Format::RGBA8UNorm, true};
    auto probeTex = take(device.createTexture(rtDesc), "probe footprint", totals);
    const auto fp = probeTex->footprint();
    probeTex.reset();

    auto readbackBuf = take(device.createBuffer({fp.size, gfx::Memory::Readback}), "rg readbackBuf", totals);
    auto texComputePipe = makeTextureComputePipeline(device, backend, totals);
    auto gfxPipe = makeSampledGraphicsPipeline(device, backend, totals);
    auto transientPool = take(device.createTransientResourcePool(), "rg transientPool", totals);

    bool deadPassExecuted = false;
    bool undeclaredAccessRejected = false;

    gfx::RenderGraph graph;
    auto hCbUpload = take(graph.importBuffer("cbUpload", cbUpload), "import cbUpload", totals);
    auto hReadback = take(graph.importBuffer("readbackBuf", readbackBuf), "import readbackBuf", totals);
    auto hCbDevice = take(graph.createBuffer("cbDevice", {256, gfx::Memory::Device}), "create cbDevice", totals);
    auto hStorageTex = take(graph.createTexture("storageTex", storageDesc), "create storageTex", totals);
    auto hRenderTex = take(graph.createTexture("renderTex", rtDesc), "create renderTex", totals);
    auto hUnusedTex = take(graph.createTexture("unusedTex", rtDesc), "create unusedTex", totals);

    // Pass 0 (Copy): upload color constants
    (void)take(graph.addPass({
        "UploadConstants",
        gfx::QueueType::Copy,
        false,
        {{hCbUpload, gfx::ResourceState::CopySource}},
        {{hCbDevice, gfx::ResourceState::CopyDestination}},
        {},
        [&](gfx::CommandList& cmd, const gfx::RenderGraphResources& res) -> gfx::Result<void> {
            // Verify undeclared handle rejection inside pass callback.
            auto bad = res.texture(hStorageTex);
            if (!bad && bad.error().code == gfx::ErrorCode::InvalidArgument) {
                undeclaredAccessRejected = true;
            }
            auto src = res.buffer(hCbUpload); if (!src) return std::unexpected(src.error());
            auto dst = res.buffer(hCbDevice); if (!dst) return std::unexpected(dst.error());
            return cmd.copyBuffer(*src, *dst, 256);
        }
    }), "add UploadConstants", totals);

    // Pass 1 (Compute): generate 16x16 gradient into storageTex
    (void)take(graph.addPass({
        "ComputeGenerateGradient",
        gfx::QueueType::Compute,
        false,
        {{hCbDevice, gfx::ResourceState::ConstantBuffer}},
        {{hStorageTex, gfx::ResourceState::UnorderedAccess}},
        {},
        [&](gfx::CommandList& cmd, const gfx::RenderGraphResources& res) -> gfx::Result<void> {
            auto cb = res.buffer(hCbDevice); if (!cb) return std::unexpected(cb.error());
            auto st = res.texture(hStorageTex); if (!st) return std::unexpected(st.error());
            if (auto ok = cmd.setComputePipeline(texComputePipe); !ok) return ok;
            gfx::ResourceBindings bindings{};
            bindings.storageTextures = {{0, 0, *st}};
            bindings.constants = {{1, 0, *cb, 0, 256}};
            if (auto ok = cmd.bindComputeResources(bindings); !ok) return ok;
            return cmd.dispatch(2, 2, 1);
        }
    }), "add ComputeGenerateGradient", totals);

    // Pass 2 (Graphics, DEAD): must be culled and never executed
    (void)take(graph.addPass({
        "DeadShadowPass",
        gfx::QueueType::Graphics,
        false,
        {},
        {{hUnusedTex, gfx::ResourceState::RenderTarget}},
        {},
        [&](gfx::CommandList&, const gfx::RenderGraphResources&) -> gfx::Result<void> {
            deadPassExecuted = true;
            return std::unexpected(gfx::Error{gfx::ErrorCode::InvalidState, "Culled pass executed!"});
        }
    }), "add DeadShadowPass", totals);

    // Pass 3 (Graphics): sample compute-generated storageTex into renderTex
    (void)take(graph.addPass({
        "GraphicsSampleToRT",
        gfx::QueueType::Graphics,
        false,
        {{hStorageTex, gfx::ResourceState::ShaderRead}},
        {{hRenderTex, gfx::ResourceState::RenderTarget}},
        {},
        [&](gfx::CommandList& cmd, const gfx::RenderGraphResources& res) -> gfx::Result<void> {
            auto st = res.texture(hStorageTex); if (!st) return std::unexpected(st.error());
            auto rt = res.texture(hRenderTex); if (!rt) return std::unexpected(rt.error());
            if (auto ok = cmd.beginRenderPass(*rt, {0.0f, 0.0f, 0.0f, 1.0f}); !ok) return ok;
            if (auto ok = cmd.setPipeline(gfxPipe); !ok) return ok;
            if (auto ok = cmd.bindTexture(*st); !ok) return ok;
            if (auto ok = cmd.draw(3, 1, 0, 0); !ok) return ok;
            return cmd.endRenderPass();
        }
    }), "add GraphicsSampleToRT", totals);

    // Pass 4 (Copy): copy renderTex -> readbackBuf
    (void)take(graph.addPass({
        "CopyReadback",
        gfx::QueueType::Copy,
        false,
        {{hRenderTex, gfx::ResourceState::CopySource}},
        {{hReadback, gfx::ResourceState::CopyDestination}},
        {},
        [&](gfx::CommandList& cmd, const gfx::RenderGraphResources& res) -> gfx::Result<void> {
            auto rt = res.texture(hRenderTex); if (!rt) return std::unexpected(rt.error());
            auto rb = res.buffer(hReadback); if (!rb) return std::unexpected(rb.error());
            return cmd.copyTextureToBuffer(*rt, *rb);
        }
    }), "add CopyReadback", totals);

    const auto st = take(graph.compile(), "compile composite graph", totals);
    ++totals.graphsCompiled;
    check(st.totalPasses == 5u, "composite totalPasses == 5", totals);
    check(st.executedPasses == 4u, "composite executedPasses == 4", totals);
    check(st.culledPasses == 1u, "composite culledPasses == 1", totals);
    check(st.transientResourcesDeclared == 4u, "composite transientResourcesDeclared == 4", totals);
    check(st.transientResourcesCulled == 1u, "composite transientResourcesCulled == 1", totals);
    check(st.physicalTransientSlots == 3u, "composite physicalTransientSlots == 3", totals);
    check(st.queueBatches == 4u, "composite queueBatches == 4 (Copy->Compute->Graphics->Copy)", totals);
    check(st.crossQueueSyncs >= 3u, "composite crossQueueSyncs >= 3", totals);
    check(st.insertedBarriers >= 8u, "composite insertedBarriers >= 8", totals);

    // Execute twice to verify TransientResourcePool hits on second frame with exact pixel output.
    for (int iter = 0; iter < 2; ++iter) {
        take(graph.execute(device, transientPool.get()), "execute composite graph", totals);
        ++totals.graphsExecuted;
        check(!deadPassExecuted, "dead pass never executed", totals);
        check(undeclaredAccessRejected, "undeclared resource access rejected inside pass", totals);
        const auto pixels = take(readbackBuf->read(0, fp.size), "readback composite pixels", totals);
        verifyGradientPixels(pixels, fp, kR, kG, kB, totals);
    }

    const auto poolStats = take(transientPool->statistics(), "composite pool stats", totals);
    check(poolStats.misses == 3u, "composite pool misses == 3 (1 per physical slot)", totals);
    check(poolStats.hits == 3u, "composite pool hits == 3 on second execution", totals);
}

void testTransientAliasingAndUavBarrierGpu(gfx::Device& device, gfx::Backend backend, Totals& totals) {
    // Part A: 3 disjoint transient 32x32 render targets aliased to 1 physical GPU texture!
    {
        gfx::TextureDesc rtDesc{32, 32, gfx::Format::RGBA8UNorm, true};
        auto probe = take(device.createTexture(rtDesc), "alias probe", totals);
        const auto fp = probe->footprint();
        probe.reset();

        auto out0 = take(device.createBuffer({fp.size, gfx::Memory::Readback}), "alias out0", totals);
        auto out1 = take(device.createBuffer({fp.size, gfx::Memory::Readback}), "alias out1", totals);
        auto out2 = take(device.createBuffer({fp.size, gfx::Memory::Readback}), "alias out2", totals);
        auto pool = take(device.createTransientResourcePool(), "alias pool", totals);

        gfx::RenderGraph g;
        auto hOut0 = take(g.importBuffer("out0", out0), "import out0", totals);
        auto hOut1 = take(g.importBuffer("out1", out1), "import out1", totals);
        auto hOut2 = take(g.importBuffer("out2", out2), "import out2", totals);
        auto hRt0 = take(g.createTexture("rt0", rtDesc), "create rt0", totals);
        auto hRt1 = take(g.createTexture("rt1", rtDesc), "create rt1", totals);
        auto hRt2 = take(g.createTexture("rt2", rtDesc), "create rt2", totals);

        auto addClearAndCopy = [&](const char* clearName, const char* copyName,
                                   gfx::RGResourceHandle hRt, gfx::RGResourceHandle hOut,
                                   gfx::Color color, std::vector<std::uint32_t> deps) -> std::uint32_t {
            (void)take(g.addPass({
                clearName,
                gfx::QueueType::Graphics,
                false,
                {},
                {{hRt, gfx::ResourceState::RenderTarget}},
                std::move(deps),
                [hRt, color](gfx::CommandList& cmd, const gfx::RenderGraphResources& res) -> gfx::Result<void> {
                    auto rt = res.texture(hRt); if (!rt) return std::unexpected(rt.error());
                    if (auto ok = cmd.beginRenderPass(*rt, color); !ok) return ok;
                    return cmd.endRenderPass();
                }
            }), clearName, totals);
            return take(g.addPass({
                copyName,
                gfx::QueueType::Graphics,
                false,
                {{hRt, gfx::ResourceState::CopySource}},
                {{hOut, gfx::ResourceState::CopyDestination}},
                {},
                [hRt, hOut](gfx::CommandList& cmd, const gfx::RenderGraphResources& res) -> gfx::Result<void> {
                    auto rt = res.texture(hRt); if (!rt) return std::unexpected(rt.error());
                    auto out = res.buffer(hOut); if (!out) return std::unexpected(out.error());
                    return cmd.copyTextureToBuffer(*rt, *out);
                }
            }), copyName, totals);
        };

        const auto c0 = addClearAndCopy("ClearRed", "CopyRed", hRt0, hOut0, {1.0f, 0.0f, 0.0f, 1.0f}, {});
        const auto c1 = addClearAndCopy("ClearGreen", "CopyGreen", hRt1, hOut1, {0.0f, 1.0f, 0.0f, 1.0f}, {c0});
        (void)addClearAndCopy("ClearBlue", "CopyBlue", hRt2, hOut2, {0.0f, 0.0f, 1.0f, 1.0f}, {c1});

        const auto st = take(g.compile(), "compile aliased GPU graph", totals);
        ++totals.graphsCompiled;
        check(st.transientResourcesDeclared == 3u, "alias declared == 3", totals);
        check(st.physicalTransientSlots == 1u, "alias physicalTransientSlots == 1", totals);
        check(st.aliasedResources == 2u, "alias aliasedResources == 2", totals);
        check(st.queueBatches == 1u, "alias single Graphics batch", totals);

        take(g.execute(device, pool.get()), "execute aliased GPU graph", totals);
        ++totals.graphsExecuted;

        const auto pStats = take(pool->statistics(), "alias pool stats", totals);
        check(pStats.misses == 1u, "only 1 physical GPU texture created for 3 logical render targets", totals);

        verifySolidPixels(take(out0->read(0, fp.size), "read out0", totals), fp, 255, 0, 0, 255, "Red", totals);
        verifySolidPixels(take(out1->read(0, fp.size), "read out1", totals), fp, 0, 255, 0, 255, "Green", totals);
        verifySolidPixels(take(out2->read(0, fp.size), "read out2", totals), fp, 0, 0, 255, 255, "Blue", totals);
    }

    // Part B: Back-to-back Compute UAV-to-UAV automatic uavBarrier insertion
    {
        constexpr std::uint32_t kCount = 256;
        constexpr std::uint64_t kBytes = kCount * sizeof(std::uint32_t);
        auto inUpload = take(device.createBuffer({kBytes, gfx::Memory::Upload}), "uav inUpload", totals);
        auto cbUpload = take(device.createBuffer({256, gfx::Memory::Upload}), "uav cbUpload", totals);
        auto outReadback = take(device.createBuffer({kBytes, gfx::Memory::Readback}), "uav outReadback", totals);

        std::vector<std::uint32_t> input(kCount);
        for (std::uint32_t i = 0; i < kCount; ++i) input[i] = i + 10u;
        take(inUpload->write(0, std::as_bytes(std::span{input})), "uav write input", totals);
        std::array<float, 4> params{2.0f, 3.0f, 0.0f, 0.0f};
        take(cbUpload->write(0, std::as_bytes(std::span{params})), "uav write params", totals);

        auto computePipe = makeBufferComputePipeline(device, backend, totals);

        gfx::RenderGraph g;
        auto hIn = take(g.importBuffer("in", inUpload), "import in", totals);
        auto hCbUp = take(g.importBuffer("cbUp", cbUpload), "import cbUp", totals);
        auto hOut = take(g.importBuffer("out", outReadback), "import out", totals);
        auto hBuf = take(g.createBuffer("storage", {kBytes, gfx::Memory::Device, true}), "create storage", totals);
        auto hCb = take(g.createBuffer("cb", {256, gfx::Memory::Device}), "create cb", totals);

        (void)take(g.addPass({
            "Upload", gfx::QueueType::Copy, false,
            {{hIn, gfx::ResourceState::CopySource}, {hCbUp, gfx::ResourceState::CopySource}},
            {{hBuf, gfx::ResourceState::CopyDestination}, {hCb, gfx::ResourceState::CopyDestination}},
            {},
            [&](gfx::CommandList& cmd, const gfx::RenderGraphResources& res) -> gfx::Result<void> {
                auto src = res.buffer(hIn); if (!src) return std::unexpected(src.error());
                auto dst = res.buffer(hBuf); if (!dst) return std::unexpected(dst.error());
                auto cbS = res.buffer(hCbUp); if (!cbS) return std::unexpected(cbS.error());
                auto cbD = res.buffer(hCb); if (!cbD) return std::unexpected(cbD.error());
                if (auto ok = cmd.copyBuffer(*src, *dst, kBytes); !ok) return ok;
                return cmd.copyBuffer(*cbS, *cbD, 256);
            }
        }), "uav Upload", totals);

        auto makeComputePass = [&](const char* name) {
            return gfx::RGPassDesc{
                name, gfx::QueueType::Compute, false,
                {{hCb, gfx::ResourceState::ConstantBuffer}, {hBuf, gfx::ResourceState::UnorderedAccess}},
                {{hBuf, gfx::ResourceState::UnorderedAccess}},
                {},
                [&](gfx::CommandList& cmd, const gfx::RenderGraphResources& res) -> gfx::Result<void> {
                    auto buf = res.buffer(hBuf); if (!buf) return std::unexpected(buf.error());
                    auto cb = res.buffer(hCb); if (!cb) return std::unexpected(cb.error());
                    if (auto ok = cmd.setComputePipeline(computePipe); !ok) return ok;
                    gfx::ResourceBindings bindings{};
                    bindings.storageBuffers = {{0, 0, *buf, 0, kBytes}};
                    bindings.constants = {{1, 0, *cb, 0, 256}};
                    if (auto ok = cmd.bindComputeResources(bindings); !ok) return ok;
                    return cmd.dispatch(kCount / 64u, 1, 1);
                }
            };
        };

        (void)take(g.addPass(makeComputePass("ComputePass1")), "ComputePass1", totals);
        (void)take(g.addPass(makeComputePass("ComputePass2")), "ComputePass2", totals);

        (void)take(g.addPass({
            "Readback", gfx::QueueType::Copy, false,
            {{hBuf, gfx::ResourceState::CopySource}},
            {{hOut, gfx::ResourceState::CopyDestination}},
            {},
            [&](gfx::CommandList& cmd, const gfx::RenderGraphResources& res) -> gfx::Result<void> {
                auto src = res.buffer(hBuf); if (!src) return std::unexpected(src.error());
                auto dst = res.buffer(hOut); if (!dst) return std::unexpected(dst.error());
                return cmd.copyBuffer(*src, *dst, kBytes);
            }
        }), "uav Readback", totals);

        const auto st = take(g.compile(), "compile UAV barrier graph", totals);
        ++totals.graphsCompiled;
        check(st.insertedUavBarriers == 1u, "automatic UAV barrier inserted between ComputePass1 and ComputePass2", totals);
        check(st.queueBatches == 3u, "UAV graph grouped into 3 batches (Copy -> Compute[2 passes] -> Copy)", totals);

        take(g.execute(device), "execute UAV barrier graph", totals);
        ++totals.graphsExecuted;

        const auto raw = take(outReadback->read(0, kBytes), "read UAV result", totals);
        for (std::uint32_t i = 0; i < kCount; ++i) {
            std::uint32_t got{};
            std::memcpy(&got, raw.data() + i * sizeof(std::uint32_t), sizeof(std::uint32_t));
            const std::uint32_t expected = (input[i] * 2u + 3u) * 2u + 3u;
            if (got != expected) {
                throw std::runtime_error("UAV chained compute mismatch at " + std::to_string(i) +
                    ": expected " + std::to_string(expected) + ", got " + std::to_string(got));
            }
        }
        ++totals.assertions;
    }
}

void testCleanlinessAndWarpRecovery(gfx::Device& device, bool warp, Totals& totals) {
    // Verify RenderGraph leaves zero leaked buffers/textures when executing without a pool.
    {
        gfx::RenderGraph g;
        gfx::TextureDesc rtDesc{16, 16, gfx::Format::RGBA8UNorm, true};
        auto hRt = take(g.createTexture("ephemeralRT", rtDesc), "ephemeralRT", totals);
        (void)take(g.addPass({
            "ClearEphemeral",
            gfx::QueueType::Graphics,
            true,
            {},
            {{hRt, gfx::ResourceState::RenderTarget}},
            {},
            [hRt](gfx::CommandList& cmd, const gfx::RenderGraphResources& res) -> gfx::Result<void> {
                auto rt = res.texture(hRt); if (!rt) return std::unexpected(rt.error());
                if (auto ok = cmd.beginRenderPass(*rt, {0.25f, 0.5f, 0.75f, 1.0f}); !ok) return ok;
                return cmd.endRenderPass();
            }
        }), "ClearEphemeral", totals);
        (void)take(g.compile(), "compile ephemeral", totals);
        ++totals.graphsCompiled;
        take(g.execute(device), "execute ephemeral", totals);
        ++totals.graphsExecuted;
    }
    const auto stats = take(device.resourceStatistics(), "resourceStatistics after graph", totals);
    check(stats.uniqueResources == 0u && stats.resourceAllocationBytes == 0u,
          "device resourceStatistics must report 0 leaked resources after RenderGraph", totals);

    if (!warp) return;

    // WARP Device Loss -> RenderGraph returns DeviceLost -> Recover -> execute succeeds on fresh device.
    gfx::RenderGraph g;
    auto hRt = take(g.createTexture("lossRT", {16, 16, gfx::Format::RGBA8UNorm, true}), "lossRT", totals);
    (void)take(g.addPass({
        "ClearLoss",
        gfx::QueueType::Graphics,
        true,
        {},
        {{hRt, gfx::ResourceState::RenderTarget}},
        {},
        [hRt](gfx::CommandList& cmd, const gfx::RenderGraphResources& res) -> gfx::Result<void> {
            auto rt = res.texture(hRt); if (!rt) return std::unexpected(rt.error());
            if (auto ok = cmd.beginRenderPass(*rt, {1.0f, 1.0f, 0.0f, 1.0f}); !ok) return ok;
            return cmd.endRenderPass();
        }
    }), "ClearLoss", totals);
    (void)take(g.compile(), "compile loss graph", totals);
    ++totals.graphsCompiled;

    take(device.removeForTesting(), "WARP removeForTesting", totals);
    reject(g.execute(device), gfx::ErrorCode::DeviceLost, "RenderGraph::execute after WARP loss", totals);

    auto recovered = take(device.recover(), "WARP recover", totals);
    take(g.execute(*recovered), "RenderGraph::execute on recovered WARP device", totals);
    ++totals.graphsExecuted;
}

void runManualVsRenderGraphBenchmark(gfx::Device& device, gfx::Backend backend, Totals& totals) {
    constexpr int kFrames = 24;
    constexpr int kWarmup = 2;
    constexpr int kMeasured = 5;
    constexpr float kR = 0.85f, kG = 0.55f, kB = 0.25f;

    auto cbUpload = take(device.createBuffer({256, gfx::Memory::Upload}), "bench cbUpload", totals);
    std::array<float, 4> colorParams{kR, kG, kB, 1.0f};
    take(cbUpload->write(0, std::as_bytes(std::span{colorParams})), "bench write colorParams", totals);

    gfx::TextureDesc storageDesc{16, 16, gfx::Format::RGBA8UNorm, false, false, true};
    gfx::TextureDesc rtDesc{16, 16, gfx::Format::RGBA8UNorm, true};
    auto probeTex = take(device.createTexture(rtDesc), "bench probe", totals);
    const auto fp = probeTex->footprint();
    probeTex.reset();

    auto readbackBuf = take(device.createBuffer({fp.size, gfx::Memory::Readback}), "bench readback", totals);
    auto texComputePipe = makeTextureComputePipeline(device, backend, totals);
    auto gfxPipe = makeSampledGraphicsPipeline(device, backend, totals);

    // 1. Manual barriers baseline (reused pools, reused resources, explicit transitions + TimelineFence)
    auto* copyQ = take(device.queue(gfx::QueueType::Copy), "bench copyQ", totals);
    auto* computeQ = take(device.queue(gfx::QueueType::Compute), "bench computeQ", totals);
    auto* graphicsQ = take(device.queue(gfx::QueueType::Graphics), "bench graphicsQ", totals);
    auto manCbDev = take(device.createBuffer({256, gfx::Memory::Device}), "man cbDev", totals);
    auto manStorage = take(device.createTexture(storageDesc), "man storage", totals);
    auto manRt = take(device.createTexture(rtDesc), "man rt", totals);
    auto manCopyPool0 = take(device.createCommandPool(gfx::QueueType::Copy), "man copyPool0", totals);
    auto manCopyPool1 = take(device.createCommandPool(gfx::QueueType::Copy), "man copyPool1", totals);
    auto manCompPool = take(device.createCommandPool(gfx::QueueType::Compute), "man compPool", totals);
    auto manGfxPool = take(device.createCommandPool(gfx::QueueType::Graphics), "man gfxPool", totals);
    auto manCopy0 = take(manCopyPool0->createCommandList(), "man copy0", totals);
    auto manCopy1 = take(manCopyPool1->createCommandList(), "man copy1", totals);
    auto manComp = take(manCompPool->createCommandList(), "man comp", totals);
    auto manGfx = take(manGfxPool->createCommandList(), "man gfx", totals);
    auto manTimeline = take(device.createTimelineFence(0), "man timeline", totals);
    std::uint64_t manVal = 0;

    auto runManual24 = [&]() -> double {
        const auto t0 = std::chrono::steady_clock::now();
        for (int f = 0; f < kFrames; ++f) {
            take(manCopyPool0->reset(), "man reset copy0", totals);
            take(manCopy0->begin(), "man copy0 begin", totals);
            take(manCopy0->transition(manCbDev, gfx::ResourceState::CopyDestination), "man cb->CopyDst", totals);
            take(manCopy0->copyBuffer(cbUpload, manCbDev, 256), "man copy cb", totals);
            take(manCopy0->transition(manCbDev, gfx::ResourceState::Common), "man cb->Common", totals);
            take(manCopy0->end(), "man copy0 end", totals);
            const std::uint64_t s1 = ++manVal;
            {
                gfx::CommandList* l[]{manCopy0.get()};
                gfx::TimelineSignal sig[]{{manTimeline, s1}};
                take(copyQ->submit({l, {}, sig}), "man submit copy0", totals);
            }

            take(manCompPool->reset(), "man reset comp", totals);
            take(manComp->begin(), "man comp begin", totals);
            take(manComp->transition(manCbDev, gfx::ResourceState::ConstantBuffer), "man cb->CB", totals);
            take(manComp->transition(manStorage, gfx::ResourceState::UnorderedAccess), "man st->UAV", totals);
            take(manComp->setComputePipeline(texComputePipe), "man setCS", totals);
            gfx::ResourceBindings bindings{};
            bindings.storageTextures = {{0, 0, manStorage}};
            bindings.constants = {{1, 0, manCbDev, 0, 256}};
            take(manComp->bindComputeResources(bindings), "man bindCS", totals);
            take(manComp->dispatch(2, 2, 1), "man dispatch", totals);
            take(manComp->transition(manCbDev, gfx::ResourceState::Common), "man cb->Common2", totals);
            take(manComp->transition(manStorage, gfx::ResourceState::Common), "man st->Common", totals);
            take(manComp->end(), "man comp end", totals);
            const std::uint64_t s2 = ++manVal;
            {
                gfx::CommandList* l[]{manComp.get()};
                gfx::TimelineWait w[]{{manTimeline, s1}};
                gfx::TimelineSignal sig[]{{manTimeline, s2}};
                take(computeQ->submit({l, w, sig}), "man submit comp", totals);
            }

            take(manGfxPool->reset(), "man reset gfx", totals);
            take(manGfx->begin(), "man gfx begin", totals);
            take(manGfx->transition(manStorage, gfx::ResourceState::ShaderRead), "man st->SR", totals);
            take(manGfx->transition(manRt, gfx::ResourceState::RenderTarget), "man rt->RT", totals);
            take(manGfx->beginRenderPass(manRt, {0.0f, 0.0f, 0.0f, 1.0f}), "man beginRP", totals);
            take(manGfx->setPipeline(gfxPipe), "man setGfx", totals);
            take(manGfx->bindTexture(manStorage), "man bindTex", totals);
            take(manGfx->draw(3, 1, 0, 0), "man draw", totals);
            take(manGfx->endRenderPass(), "man endRP", totals);
            take(manGfx->transition(manStorage, gfx::ResourceState::Common), "man st->Common2", totals);
            take(manGfx->transition(manRt, gfx::ResourceState::Common), "man rt->Common", totals);
            take(manGfx->end(), "man gfx end", totals);
            const std::uint64_t s3 = ++manVal;
            {
                gfx::CommandList* l[]{manGfx.get()};
                gfx::TimelineWait w[]{{manTimeline, s2}};
                gfx::TimelineSignal sig[]{{manTimeline, s3}};
                take(graphicsQ->submit({l, w, sig}), "man submit gfx", totals);
            }

            take(manCopyPool1->reset(), "man reset copy1", totals);
            take(manCopy1->begin(), "man copy1 begin", totals);
            take(manCopy1->transition(manRt, gfx::ResourceState::CopySource), "man rt->CopySrc", totals);
            take(manCopy1->copyTextureToBuffer(manRt, readbackBuf), "man copyTex", totals);
            take(manCopy1->transition(manRt, gfx::ResourceState::Common), "man rt->Common2", totals);
            take(manCopy1->end(), "man copy1 end", totals);
            const std::uint64_t s4 = ++manVal;
            {
                gfx::CommandList* l[]{manCopy1.get()};
                gfx::TimelineWait w[]{{manTimeline, s3}};
                gfx::TimelineSignal sig[]{{manTimeline, s4}};
                take(copyQ->submit({l, w, sig}), "man submit copy1", totals);
            }
            take(manTimeline->wait(s4, std::chrono::seconds(10)), "man wait", totals);
        }
        const auto t1 = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(t1 - t0).count();
    };

    // Helper to build the identical 4-stage RenderGraph (+ 1 dead culled pass)
    auto buildGraph = [&](gfx::RenderGraph& g) {
        g.reset();
        auto hCbUp = take(g.importBuffer("cbUpload", cbUpload), "b import cbUp", totals);
        auto hRb = take(g.importBuffer("readback", readbackBuf), "b import rb", totals);
        auto hCb = take(g.createBuffer("cbDev", {256, gfx::Memory::Device}), "b cbDev", totals);
        auto hSt = take(g.createTexture("storage", storageDesc), "b storage", totals);
        auto hRt = take(g.createTexture("rt", rtDesc), "b rt", totals);
        auto hDead = take(g.createTexture("dead", rtDesc), "b dead", totals);

        (void)take(g.addPass({
            "Upload", gfx::QueueType::Copy, false,
            {{hCbUp, gfx::ResourceState::CopySource}},
            {{hCb, gfx::ResourceState::CopyDestination}}, {},
            [hCbUp, hCb](gfx::CommandList& cmd, const gfx::RenderGraphResources& res) -> gfx::Result<void> {
                auto s = res.buffer(hCbUp); if (!s) return std::unexpected(s.error());
                auto d = res.buffer(hCb); if (!d) return std::unexpected(d.error());
                return cmd.copyBuffer(*s, *d, 256);
            }
        }), "b Upload", totals);

        (void)take(g.addPass({
            "Compute", gfx::QueueType::Compute, false,
            {{hCb, gfx::ResourceState::ConstantBuffer}},
            {{hSt, gfx::ResourceState::UnorderedAccess}}, {},
            [hCb, hSt, &texComputePipe](gfx::CommandList& cmd, const gfx::RenderGraphResources& res) -> gfx::Result<void> {
                auto cb = res.buffer(hCb); if (!cb) return std::unexpected(cb.error());
                auto st = res.texture(hSt); if (!st) return std::unexpected(st.error());
                if (auto ok = cmd.setComputePipeline(texComputePipe); !ok) return ok;
                gfx::ResourceBindings bindings{};
                bindings.storageTextures = {{0, 0, *st}};
                bindings.constants = {{1, 0, *cb, 0, 256}};
                if (auto ok = cmd.bindComputeResources(bindings); !ok) return ok;
                return cmd.dispatch(2, 2, 1);
            }
        }), "b Compute", totals);

        (void)take(g.addPass({
            "DeadPass", gfx::QueueType::Graphics, false,
            {}, {{hDead, gfx::ResourceState::RenderTarget}}, {},
            [](gfx::CommandList&, const gfx::RenderGraphResources&) -> gfx::Result<void> { return {}; }
        }), "b DeadPass", totals);

        (void)take(g.addPass({
            "Graphics", gfx::QueueType::Graphics, false,
            {{hSt, gfx::ResourceState::ShaderRead}},
            {{hRt, gfx::ResourceState::RenderTarget}}, {},
            [hSt, hRt, &gfxPipe](gfx::CommandList& cmd, const gfx::RenderGraphResources& res) -> gfx::Result<void> {
                auto st = res.texture(hSt); if (!st) return std::unexpected(st.error());
                auto rt = res.texture(hRt); if (!rt) return std::unexpected(rt.error());
                if (auto ok = cmd.beginRenderPass(*rt, {0.0f, 0.0f, 0.0f, 1.0f}); !ok) return ok;
                if (auto ok = cmd.setPipeline(gfxPipe); !ok) return ok;
                if (auto ok = cmd.bindTexture(*st); !ok) return ok;
                if (auto ok = cmd.draw(3, 1, 0, 0); !ok) return ok;
                return cmd.endRenderPass();
            }
        }), "b Graphics", totals);

        (void)take(g.addPass({
            "Readback", gfx::QueueType::Copy, false,
            {{hRt, gfx::ResourceState::CopySource}},
            {{hRb, gfx::ResourceState::CopyDestination}}, {},
            [hRt, hRb](gfx::CommandList& cmd, const gfx::RenderGraphResources& res) -> gfx::Result<void> {
                auto rt = res.texture(hRt); if (!rt) return std::unexpected(rt.error());
                auto rb = res.buffer(hRb); if (!rb) return std::unexpected(rb.error());
                return cmd.copyTextureToBuffer(*rt, *rb);
            }
        }), "b Readback", totals);
    };

    auto pool = take(device.createTransientResourcePool(), "bench transient pool", totals);
    gfx::RenderGraph precompiledGraph;
    buildGraph(precompiledGraph);
    (void)take(precompiledGraph.compile(), "bench compile precompiled", totals);

    auto runPrecompiled24 = [&]() -> double {
        const auto t0 = std::chrono::steady_clock::now();
        for (int f = 0; f < kFrames; ++f) {
            take(precompiledGraph.execute(device, pool.get()), "bench exec precompiled", totals);
        }
        const auto t1 = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(t1 - t0).count();
    };

    gfx::RenderGraph dynamicGraph;
    double compileUsAccum = 0.0;
    std::uint64_t compileSamples = 0;
    auto runRebuilt24 = [&](bool recordCompile) -> double {
        const auto t0 = std::chrono::steady_clock::now();
        for (int f = 0; f < kFrames; ++f) {
            buildGraph(dynamicGraph);
            const auto c0 = std::chrono::steady_clock::now();
            (void)take(dynamicGraph.compile(), "bench compile dynamic", totals);
            const auto c1 = std::chrono::steady_clock::now();
            if (recordCompile) {
                compileUsAccum += std::chrono::duration<double, std::micro>(c1 - c0).count();
                ++compileSamples;
            }
            take(dynamicGraph.execute(device, pool.get()), "bench exec dynamic", totals);
        }
        const auto t1 = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(t1 - t0).count();
    };

    auto median = [](std::vector<double> v) {
        std::ranges::sort(v);
        return v[v.size() / 2u];
    };

    for (int w = 0; w < kWarmup; ++w) {
        (void)runManual24();
        (void)runPrecompiled24();
        (void)runRebuilt24(false);
    }

    std::vector<double> manTimes, preTimes, dynTimes;
    for (int m = 0; m < kMeasured; ++m) {
        manTimes.push_back(runManual24());
        preTimes.push_back(runPrecompiled24());
        dynTimes.push_back(runRebuilt24(true));
    }

    const double manMed = median(manTimes);
    const double preMed = median(preTimes);
    const double dynMed = median(dynTimes);
    const double avgCompileUs = compileSamples ? (compileUsAccum / static_cast<double>(compileSamples)) : 0.0;
    const double preOverheadPct = ((preMed - manMed) / std::max(0.001, manMed)) * 100.0;
    const double dynOverheadPct = ((dynMed - manMed) / std::max(0.001, manMed)) * 100.0;

    // Verify final readback pixels are still exact after benchmark runs.
    verifyGradientPixels(take(readbackBuf->read(0, fp.size), "bench verify pixels", totals), fp, kR, kG, kB, totals);
    const auto pStats = take(pool->statistics(), "bench pool stats", totals);

    std::cout << std::fixed << std::setprecision(3)
              << "BENCHMARK_24F: manual_ms=" << manMed
              << " rg_precompiled_ms=" << preMed
              << " (delta=" << (preOverheadPct >= 0.0 ? "+" : "") << preOverheadPct << "%)"
              << " rg_rebuilt_ms=" << dynMed
              << " (delta=" << (dynOverheadPct >= 0.0 ? "+" : "") << dynOverheadPct << "%)"
              << " compile_avg_us=" << avgCompileUs
              << " pool_hits=" << pStats.hits
              << " pool_misses=" << pStats.misses << "\n";
}
} // namespace

int main(int argc, char** argv) {
    gfx::Backend backend = gfx::Backend::Vulkan;
    bool validation = false;
    bool warp = false;
    bool benchmark = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--d3d12") backend = gfx::Backend::D3D12;
        else if (arg == "--vulkan") backend = gfx::Backend::Vulkan;
        else if (arg == "--validation") validation = true;
        else if (arg == "--warp") { backend = gfx::Backend::D3D12; warp = true; }
        else if (arg == "--benchmark") benchmark = true;
    }

    Totals totals{};
    try {
        gfx::DeviceDesc desc{};
        desc.backend = backend;
        desc.validation = validation;
        desc.softwareAdapter = warp;
        desc.enableDeviceLossTesting = warp;
        const auto adapters = gfx::enumerateAdapters(backend);
        if (!adapters || adapters->empty()) {
            std::cout << "SKIP: no adapter available\n";
            return 77;
        }
        if (!warp) {
            for (const auto& a : *adapters) {
                if (!a.softwareAdapter) {
                    desc.adapter = a.id;
                    break;
                }
            }
            if (!desc.adapter) desc.adapter = (*adapters)[0].id;
        } else {
            for (const auto& a : *adapters) {
                if (a.softwareAdapter) {
                    desc.adapter = a.id;
                    break;
                }
            }
        }

        auto devRes = gfx::createDevice(desc);
        if (!devRes) {
            if (!warp && devRes.error().code == gfx::ErrorCode::Unsupported) {
                std::cout << "SKIP: " << devRes.error().message << "\n";
                return 77;
            }
            throw std::runtime_error("createDevice failed: " + devRes.error().message);
        }
        auto device = std::move(*devRes);
        const auto& caps = device->caps();

        testCpuCompileContracts(*device, totals);
        testCompositeGraphicsComputeMultiQueueGraph(*device, backend, totals);
        if (validation) {
            const auto e1 = take(device->validationErrorCount(), "val after composite", totals);
            if (e1 != 0u) throw std::runtime_error("validationErrorCount after testCompositeGraphicsComputeMultiQueueGraph = " + std::to_string(e1));
        }
        testTransientAliasingAndUavBarrierGpu(*device, backend, totals);
        if (validation) {
            const auto e2 = take(device->validationErrorCount(), "val after aliasing/uav", totals);
            if (e2 != 0u) throw std::runtime_error("validationErrorCount after testTransientAliasingAndUavBarrierGpu = " + std::to_string(e2));
        }
        if (benchmark) {
            runManualVsRenderGraphBenchmark(*device, backend, totals);
        }

        std::uint64_t valErrors = 0;
        if (validation) {
            valErrors = take(device->validationErrorCount(), "validationErrorCount", totals);
            check(valErrors == 0u, "validationErrorCount must be 0", totals);
        }

        // Run cleanliness & optional WARP device-loss/recovery last (since WARP loss retires the device).
        testCleanlinessAndWarpRecovery(*device, warp, totals);

        std::cout << "PASS render_graph backend="
                  << (backend == gfx::Backend::D3D12 ? "D3D12" : "Vulkan")
                  << " adapter=\"" << caps.adapterName << "\""
                  << " validation=" << (validation ? "on" : "off")
                  << " warp=" << (warp ? "on" : "off")
                  << " val_errors=" << valErrors
                  << " graphs_compiled=" << totals.graphsCompiled
                  << " graphs_executed=" << totals.graphsExecuted
                  << " rejections=" << totals.rejections
                  << " assertions=" << totals.assertions << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "FAIL render_graph: " << ex.what() << "\n";
        return 1;
    }
}
