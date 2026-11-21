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
    std::uint64_t scopesVerified{};
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

gfx::GraphicsPipelineDesc makeSimpleGraphicsDesc(gfx::Backend backend, Totals& totals) {
    gfx::ShaderBinary vs{}, ps{};
    if (backend == gfx::Backend::D3D12) {
        constexpr std::string_view hlsl = R"(
float4 VS(uint id : SV_VertexID) : SV_Position {
    float2 p[3] = { float2(-0.5, -0.5), float2(0.5, -0.5), float2(0.0, 0.5) };
    return float4(p[id % 3], 0.5, 1.0);
}
float4 PS() : SV_Target0 { return float4(0.25, 0.75, 0.5, 1.0); }
)";
        vs = take(gfx::compileHlsl(hlsl, "VS", gfx::ShaderStage::Vertex), "compile HLSL simple VS", totals);
        ps = take(gfx::compileHlsl(hlsl, "PS", gfx::ShaderStage::Fragment), "compile HLSL simple PS", totals);
    } else {
        vs = spirvBinary(gfx::ShaderStage::Vertex, bindingFixtures::uv);
        ps = spirvBinary(gfx::ShaderStage::Fragment, bindingFixtures::sample);
    }
    gfx::GraphicsPipelineDesc desc{vs, ps};
    desc.sampledTexture = (backend == gfx::Backend::Vulkan);
    desc.colorFormat = gfx::Format::RGBA8UNorm;
    return desc;
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
    buf.Store(byteOff, val * (uint)params.x + (uint)params.y);
}
)";
        cs = take(gfx::compileHlsl(hlsl, "CS", gfx::ShaderStage::Compute), "compile HLSL CS", totals);
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
    return take(device.createComputePipeline(desc), "create compute pipeline", totals);
}

gfx::TexturePtr makeInitializedSampleTexture(gfx::Device& device, std::uint32_t w, std::uint32_t h, Totals& totals) {
    auto tex = take(device.createTexture({w, h, gfx::Format::RGBA8UNorm, true}), "init sampleTex", totals);
    auto cmd = take(device.createCommandList(), "init sampleTex cmd", totals);
    take(cmd->begin(), "init sampleTex begin", totals);
    take(cmd->transition(tex, gfx::ResourceState::RenderTarget), "init sampleTex -> RT", totals);
    take(cmd->beginRenderPass(tex, {0.5f, 0.25f, 0.75f, 1.0f}), "init sampleTex RP", totals);
    take(cmd->endRenderPass(), "init sampleTex endRP", totals);
    take(cmd->transition(tex, gfx::ResourceState::ShaderRead), "init sampleTex -> SR", totals);
    take(cmd->end(), "init sampleTex end", totals);
    auto f = take(device.queue().submit(*cmd), "init sampleTex submit", totals);
    take(device.queue().wait(f), "init sampleTex wait", totals);
    return tex;
}

void testInstrumentationOffNullContract(gfx::Device& device, gfx::Backend backend, Totals& totals) {
    check(!device.isProfilingEnabled(), "profiling must be disabled by default", totals);

    auto gfxDesc = makeSimpleGraphicsDesc(backend, totals);
    auto cached1 = take(device.createCachedGraphicsPipeline(gfxDesc), "cached miss while OFF", totals);
    auto cached2 = take(device.createCachedGraphicsPipeline(gfxDesc), "cached hit while OFF", totals);
    check(!cached1.hit && cached2.hit, "cache hit/miss behavior preserved when OFF", totals);

    auto rt = take(device.createTexture({16, 16, gfx::Format::RGBA8UNorm, true}), "off rt", totals);
    auto sampleTex = makeInitializedSampleTexture(device, 16, 16, totals);
    auto cmd = take(device.createCommandList(), "off cmd", totals);
    take(cmd->begin(), "off begin", totals);
    take(cmd->transition(rt, gfx::ResourceState::RenderTarget), "off rt -> RT", totals);
    take(cmd->beginRenderPass(rt, {0.1f, 0.2f, 0.3f, 1.0f}), "off beginRP", totals);
    take(cmd->setPipeline(cached1.pipeline), "off setPipe", totals);
    if (gfxDesc.sampledTexture) take(cmd->bindTexture(sampleTex), "off bindTex", totals);
    take(cmd->draw(3, 1, 0, 0), "off draw", totals);
    take(cmd->endRenderPass(), "off endRP", totals);
    take(cmd->end(), "off end", totals);
    auto fence = take(device.queue().submit(*cmd), "off submit", totals);
    take(device.queue().wait(fence), "off wait", totals);

    const auto snap = take(device.profilingSnapshot(), "snapshot while OFF", totals);
    check(!snap.enabled, "snap.enabled == false when OFF", totals);
    check(snap.counters.drawCalls == 0u, "OFF drawCalls == 0", totals);
    check(snap.counters.renderPasses == 0u, "OFF renderPasses == 0", totals);
    check(snap.counters.barriers == 0u, "OFF barriers == 0", totals);
    check(snap.counters.submissions == 0u, "OFF submissions == 0", totals);
    check(snap.counters.pipelineCacheHits == 0u && snap.counters.pipelineCacheMisses == 0u,
          "OFF pipeline cache counters == 0", totals);
    check(snap.counters.queueWaits == 0u && snap.counters.timelineWaits == 0u,
          "OFF wait counters == 0", totals);
    check(!snap.cpuStallMilliseconds.has_value(), "OFF cpuStallMilliseconds must be std::nullopt", totals);
    check(!snap.vramBudgetBytes.has_value() && !snap.vramUsageBytes.has_value(),
          "OFF VRAM fields must be std::nullopt", totals);
    check(snap.gpuTimestampScopes.empty(), "OFF gpuTimestampScopes must be empty", totals);
}

void testExactCountersCacheStallAndVramContract(gfx::Device& device, gfx::Backend backend, Totals& totals) {
    auto sampleTex = makeInitializedSampleTexture(device, 16, 16, totals);
    take(device.clearGraphicsPipelineCache(), "clear cache", totals);
    take(device.setProfilingEnabled(true), "enable profiling", totals);
    take(device.resetProfilingStatistics(), "reset profiling", totals);
    check(device.isProfilingEnabled(), "profiling enabled", totals);

    // 1. Pipeline cache hit/miss accuracy (1 miss + 2 hits)
    auto gfxDesc = makeSimpleGraphicsDesc(backend, totals);
    auto c0 = take(device.createCachedGraphicsPipeline(gfxDesc), "cache miss", totals);
    auto c1 = take(device.createCachedGraphicsPipeline(gfxDesc), "cache hit 1", totals);
    auto c2 = take(device.createCachedGraphicsPipeline(gfxDesc), "cache hit 2", totals);
    check(!c0.hit && c1.hit && c2.hit, "cache hit flags", totals);

    // 2. Prepare resources for exact draw/indexed/instanced/dispatch/uavBarrier/copy/barrier/renderPass counts
    auto rt = take(device.createTexture({16, 16, gfx::Format::RGBA8UNorm, true}), "prof rt", totals);
    const auto fp = rt->footprint();
    auto rbTex = take(device.createBuffer({fp.size, gfx::Memory::Readback}), "prof rbTex", totals);

    constexpr std::uint32_t kCount = 256;
    constexpr std::uint64_t kBytes = kCount * sizeof(std::uint32_t);
    auto upBuf = take(device.createBuffer({kBytes, gfx::Memory::Upload}), "prof upBuf", totals);
    auto devBuf = take(device.createBuffer({kBytes, gfx::Memory::Device, true}), "prof devBuf", totals);
    auto cbUp = take(device.createBuffer({256, gfx::Memory::Upload}), "prof cbUp", totals);
    auto cbDev = take(device.createBuffer({256, gfx::Memory::Device}), "prof cbDev", totals);
    auto idxBuf = take(device.createBuffer({64, gfx::Memory::Upload}), "prof idxBuf", totals);
    std::array<std::uint16_t, 6> indices{0, 1, 2, 0, 1, 2};
    take(idxBuf->write(0, std::as_bytes(std::span{indices})), "write indices", totals);
    std::array<float, 4> params{2.0f, 1.0f, 0.0f, 0.0f};
    take(cbUp->write(0, std::as_bytes(std::span{params})), "write params", totals);
    std::vector<std::uint32_t> initData(kCount, 7u);
    take(upBuf->write(0, std::as_bytes(std::span{initData})), "write initData", totals);

    auto compPipe = makeBufferComputePipeline(device, backend, totals);
    auto cmd = take(device.createCommandList(), "prof cmd", totals);

    take(cmd->begin(), "prof begin", totals);

    // Rejected commands must NOT increment any counter!
    reject(cmd->draw(3, 1, 0, 0), gfx::ErrorCode::InvalidState, "draw outside pass rejected", totals);
    reject(cmd->dispatch(0, 1, 1), gfx::ErrorCode::InvalidState, "dispatch without pipeline rejected", totals);
    reject(cmd->copyBuffer(upBuf, upBuf, 16), gfx::ErrorCode::InvalidArgument, "self-copy rejected", totals);

    // 7 transitions before/between work:
    // 1: devBuf -> CopyDest, 2: cbDev -> CopyDest
    take(cmd->transition(devBuf, gfx::ResourceState::CopyDestination), "t1", totals);
    take(cmd->transition(cbDev, gfx::ResourceState::CopyDestination), "t2", totals);
    // 2 buffer copies
    take(cmd->copyBuffer(upBuf, devBuf, kBytes), "copy1", totals);
    take(cmd->copyBuffer(cbUp, cbDev, 256), "copy2", totals);
    // 3: devBuf -> UAV, 4: cbDev -> CB
    take(cmd->transition(devBuf, gfx::ResourceState::UnorderedAccess), "t3", totals);
    take(cmd->transition(cbDev, gfx::ResourceState::ConstantBuffer), "t4", totals);
    // 2 dispatches + 1 uavBarrier
    take(cmd->setComputePipeline(compPipe), "setCS", totals);
    gfx::ResourceBindings csBind{};
    csBind.storageBuffers = {{0, 0, devBuf, 0, kBytes}};
    csBind.constants = {{1, 0, cbDev, 0, 256}};
    take(cmd->bindComputeResources(csBind), "bindCS", totals);
    take(cmd->dispatch(kCount / 64u, 1, 1), "dispatch1", totals);
    take(cmd->uavBarrier(devBuf), "uavBarrier1", totals);
    take(cmd->dispatch(kCount / 64u, 1, 1), "dispatch2", totals);

    // 5: sampleTex -> ShaderRead, 6: rt -> RenderTarget, 7: idxBuf -> IndexBuffer
    take(cmd->transition(sampleTex, gfx::ResourceState::ShaderRead), "t5", totals);
    take(cmd->transition(rt, gfx::ResourceState::RenderTarget), "t6", totals);
    take(cmd->transition(idxBuf, gfx::ResourceState::IndexBuffer), "t7", totals);

    // RenderPass 1: 2 non-instanced draws + 1 instanced draw (instances=4)
    take(cmd->beginRenderPass(rt, {0.0f, 0.0f, 0.0f, 1.0f}), "rp1", totals);
    take(cmd->setPipeline(c0.pipeline), "setPipe1", totals);
    if (gfxDesc.sampledTexture) take(cmd->bindTexture(sampleTex), "bindTex1", totals);
    take(cmd->draw(3, 1, 0, 0), "draw1", totals);
    take(cmd->draw(3, 1, 0, 0), "draw2", totals);
    take(cmd->draw(3, 4, 0, 0), "draw3 instanced", totals);
    take(cmd->endRenderPass(), "endRP1", totals);

    // RenderPass 2: 1 indexed + instanced draw (instances=2)
    take(cmd->beginRenderPass(rt, {0.0f, 0.0f, 0.0f, 1.0f}), "rp2", totals);
    take(cmd->setPipeline(c0.pipeline), "setPipe2", totals);
    if (gfxDesc.sampledTexture) take(cmd->bindTexture(sampleTex), "bindTex2", totals);
    take(cmd->bindIndexBuffer(idxBuf, gfx::IndexFormat::UInt16), "bindIB", totals);
    take(cmd->drawIndexed(6, 2, 0, 0, 0), "drawIndexed1", totals);
    take(cmd->endRenderPass(), "endRP2", totals);

    // 8: rt -> CopySource + 1 texture readback copy
    take(cmd->transition(rt, gfx::ResourceState::CopySource), "t8", totals);
    take(cmd->copyTextureToBuffer(rt, rbTex), "copy3", totals);

    take(cmd->end(), "prof end", totals);
    auto fence = take(device.queue().submit(*cmd), "prof submit", totals);
    take(device.queue().wait(fence), "prof wait", totals);

    const auto snap = take(device.profilingSnapshot(), "snapshot while ON", totals);
    check(snap.enabled, "snap.enabled == true", totals);
    check(snap.counters.drawCalls == 4u, "drawCalls == 4", totals);
    check(snap.counters.indexedDrawCalls == 1u, "indexedDrawCalls == 1", totals);
    check(snap.counters.instancedDrawCalls == 2u, "instancedDrawCalls == 2", totals);
    check(snap.counters.dispatchCalls == 2u, "dispatchCalls == 2", totals);
    check(snap.counters.renderPasses == 2u, "renderPasses == 2", totals);
    check(snap.counters.copyCommands == 3u, "copyCommands == 3", totals);
    check(snap.counters.barriers == 8u, "barriers == 8", totals);
    check(snap.counters.uavBarriers == 1u, "uavBarriers == 1", totals);
    check(snap.counters.submissions == 1u, "submissions == 1", totals);
    check(snap.counters.pipelineCacheMisses == 1u, "pipelineCacheMisses == 1", totals);
    check(snap.counters.pipelineCacheHits == 2u, "pipelineCacheHits == 2", totals);
    check(snap.counters.queueWaits == 1u, "queueWaits == 1", totals);
    check(snap.cpuStallMilliseconds.has_value() && *snap.cpuStallMilliseconds >= 0.0 && std::isfinite(*snap.cpuStallMilliseconds),
          "cpuStallMilliseconds populated and finite when ON", totals);

    // Honest VRAM null contract: D3D12 supports queryMemoryBudget(Local), Vulkan returns Unsupported -> std::nullopt!
    const auto budgetProbe = device.queryMemoryBudget(gfx::MemorySegment::Local);
    if (budgetProbe) {
        check(snap.vramBudgetBytes.has_value() && snap.vramUsageBytes.has_value(),
              "VRAM fields populated when queryMemoryBudget is supported", totals);
    } else {
        check(!snap.vramBudgetBytes.has_value() && !snap.vramUsageBytes.has_value(),
              "VRAM fields must remain std::nullopt when queryMemoryBudget is Unsupported", totals);
    }

    // Reset clears all counters & scopes while keeping profiling enabled.
    take(device.resetProfilingStatistics(), "reset stats", totals);
    const auto afterReset = take(device.profilingSnapshot(), "snapshot after reset", totals);
    check(afterReset.enabled, "still enabled after reset", totals);
    check(afterReset.counters.drawCalls == 0u && afterReset.counters.dispatchCalls == 0u &&
          afterReset.counters.submissions == 0u && afterReset.counters.pipelineCacheHits == 0u,
          "all counters zero after reset", totals);
    check(afterReset.cpuStallMilliseconds.has_value() && *afterReset.cpuStallMilliseconds == 0.0,
          "cpuStallMilliseconds == 0.0 after reset", totals);
    check(afterReset.gpuTimestampScopes.empty(), "gpuTimestampScopes empty after reset", totals);

    // Turning OFF returns to std::nullopt contract.
    take(device.setProfilingEnabled(false), "disable profiling", totals);
    const auto afterOff = take(device.profilingSnapshot(), "snapshot after OFF", totals);
    check(!afterOff.enabled && !afterOff.cpuStallMilliseconds.has_value() &&
          !afterOff.vramBudgetBytes.has_value() && !afterOff.vramUsageBytes.has_value(),
          "OFF null contract restored", totals);
}

void testMultiScopeGpuTimestampsAndLegacyCoexistence(gfx::Device& device, gfx::Backend backend, Totals& totals) {
    // 1. Copy queue must reject timestamp scopes with Unsupported.
    {
        auto copyCmd = take(device.createCommandList(gfx::QueueType::Copy), "copy cmd for ts reject", totals);
        take(copyCmd->begin(), "copy begin", totals);
        reject(copyCmd->beginTimestampScope("CopyScope"), gfx::ErrorCode::Unsupported, "Copy beginTimestampScope Unsupported", totals);
        reject(copyCmd->endTimestampScope(), gfx::ErrorCode::Unsupported, "Copy endTimestampScope Unsupported", totals);
        reject(copyCmd->resolveTimestampScopes(), gfx::ErrorCode::Unsupported, "Copy resolveTimestampScopes Unsupported", totals);
        take(copyCmd->end(), "copy end", totals);
        reject(copyCmd->timestampScopes(), gfx::ErrorCode::Unsupported, "Copy timestampScopes Unsupported", totals);
    }

    if (!device.caps().timestampQueries) {
        return;
    }

    // 2. Contract rejections on Graphics queue
    {
        auto cmd = take(device.createCommandList(), "reject ts cmd", totals);
        take(cmd->begin(), "reject ts begin", totals);
        reject(cmd->beginTimestampScope(""), gfx::ErrorCode::InvalidArgument, "empty scope name rejected", totals);
        reject(cmd->beginTimestampScope(std::string(257, 'a')), gfx::ErrorCode::InvalidArgument, "oversized scope name rejected", totals);
        std::string withNul("abc", 3);
        withNul.push_back('\0');
        withNul.push_back('z');
        reject(cmd->beginTimestampScope(withNul), gfx::ErrorCode::InvalidArgument, "NUL scope name rejected", totals);
        reject(cmd->endTimestampScope(), gfx::ErrorCode::InvalidState, "end without begin rejected", totals);
        reject(cmd->resolveTimestampScopes(), gfx::ErrorCode::InvalidState, "resolve with 0 scopes rejected", totals);

        take(cmd->beginTimestampScope("OpenScope"), "open scope", totals);
        reject(cmd->resolveTimestampScopes(), gfx::ErrorCode::InvalidState, "resolve with unclosed scope rejected", totals);
        reject(cmd->end(), gfx::ErrorCode::InvalidState, "end() with unclosed scope rejected", totals);
        take(cmd->endTimestampScope(), "close scope", totals);
        take(cmd->resolveTimestampScopes(), "resolve scope", totals);
        reject(cmd->beginTimestampScope("AfterResolve"), gfx::ErrorCode::InvalidState, "begin after resolve rejected", totals);
        reject(cmd->resolveTimestampScopes(), gfx::ErrorCode::InvalidState, "double resolve rejected", totals);
        take(cmd->end(), "reject ts end", totals);
        reject(cmd->timestampScopes(), gfx::ErrorCode::InvalidState, "timestampScopes before submit rejected", totals);
    }

    // 3. Legacy 2-slot + nested & sequential multi-scope coexistence + GPU timing consistency
    take(device.setProfilingEnabled(true), "enable profiling for scopes", totals);
    take(device.resetProfilingStatistics(), "reset for scopes", totals);

    auto gfxDesc = makeSimpleGraphicsDesc(backend, totals);
    auto gfxPipe = take(device.createGraphicsPipeline(gfxDesc), "scope gfxPipe", totals);
    auto compPipe = makeBufferComputePipeline(device, backend, totals);

    constexpr std::uint32_t kCount = 4096;
    constexpr std::uint64_t kBytes = kCount * sizeof(std::uint32_t);
    auto upBuf = take(device.createBuffer({kBytes, gfx::Memory::Upload}), "scope upBuf", totals);
    auto devBuf = take(device.createBuffer({kBytes, gfx::Memory::Device, true}), "scope devBuf", totals);
    auto cbUp = take(device.createBuffer({256, gfx::Memory::Upload}), "scope cbUp", totals);
    auto cbDev = take(device.createBuffer({256, gfx::Memory::Device}), "scope cbDev", totals);
    auto rt = take(device.createTexture({64, 64, gfx::Format::RGBA8UNorm, true}), "scope rt", totals);
    auto sampleTex = makeInitializedSampleTexture(device, 64, 64, totals);
    const auto fp = rt->footprint();
    auto rbTex = take(device.createBuffer({fp.size, gfx::Memory::Readback}), "scope rbTex", totals);

    std::array<float, 4> params{3.0f, 5.0f, 0.0f, 0.0f};
    take(cbUp->write(0, std::as_bytes(std::span{params})), "scope write params", totals);
    std::vector<std::uint32_t> initData(kCount, 1u);
    take(upBuf->write(0, std::as_bytes(std::span{initData})), "scope write init", totals);

    auto cmd = take(device.createCommandList(), "scope main cmd", totals);
    take(cmd->begin(), "scope begin", totals);
    take(cmd->writeTimestamp(0), "legacy writeTimestamp(0)", totals);
    take(cmd->beginTimestampScope("Frame"), "begin Frame", totals);

    take(cmd->beginTimestampScope("Upload"), "begin Upload", totals);
    take(cmd->transition(devBuf, gfx::ResourceState::CopyDestination), "s t1", totals);
    take(cmd->transition(cbDev, gfx::ResourceState::CopyDestination), "s t2", totals);
    take(cmd->copyBuffer(upBuf, devBuf, kBytes), "s copy1", totals);
    take(cmd->copyBuffer(cbUp, cbDev, 256), "s copy2", totals);
    take(cmd->endTimestampScope(), "end Upload", totals);

    take(cmd->beginTimestampScope("Compute"), "begin Compute", totals);
    take(cmd->transition(devBuf, gfx::ResourceState::UnorderedAccess), "s t3", totals);
    take(cmd->transition(cbDev, gfx::ResourceState::ConstantBuffer), "s t4", totals);
    take(cmd->setComputePipeline(compPipe), "s setCS", totals);
    gfx::ResourceBindings csBind{};
    csBind.storageBuffers = {{0, 0, devBuf, 0, kBytes}};
    csBind.constants = {{1, 0, cbDev, 0, 256}};
    take(cmd->bindComputeResources(csBind), "s bindCS", totals);
    for (int i = 0; i < 4; ++i) {
        take(cmd->dispatch(kCount / 64u, 1, 1), "s dispatch", totals);
        if (i < 3) take(cmd->uavBarrier(devBuf), "s uavBarrier", totals);
    }
    take(cmd->endTimestampScope(), "end Compute", totals);

    take(cmd->beginTimestampScope("GraphicsPass"), "begin GraphicsPass", totals);
    take(cmd->transition(sampleTex, gfx::ResourceState::ShaderRead), "s t5", totals);
    take(cmd->transition(rt, gfx::ResourceState::RenderTarget), "s t6", totals);
    take(cmd->beginRenderPass(rt, {0.2f, 0.4f, 0.6f, 1.0f}), "s beginRP", totals);
    take(cmd->setPipeline(gfxPipe), "s setPipe", totals);
    if (gfxDesc.sampledTexture) take(cmd->bindTexture(sampleTex), "s bindTex", totals);
    for (int d = 0; d < 8; ++d) take(cmd->draw(3, 1, 0, 0), "s draw", totals);
    take(cmd->endRenderPass(), "s endRP", totals);
    take(cmd->endTimestampScope(), "end GraphicsPass", totals);

    take(cmd->beginTimestampScope("Readback"), "begin Readback", totals);
    take(cmd->transition(rt, gfx::ResourceState::CopySource), "s t7", totals);
    take(cmd->copyTextureToBuffer(rt, rbTex), "s copyTex", totals);
    take(cmd->endTimestampScope(), "end Readback", totals);

    take(cmd->endTimestampScope(), "end Frame", totals);
    take(cmd->writeTimestamp(1), "legacy writeTimestamp(1)", totals);
    take(cmd->resolveTimestamps(), "legacy resolveTimestamps", totals);
    take(cmd->resolveTimestampScopes(), "resolveTimestampScopes", totals);
    take(cmd->end(), "scope end", totals);

    auto fence = take(device.queue().submit(*cmd), "scope submit", totals);
    take(device.queue().wait(fence), "scope wait", totals);

    const double legacyMs = take(cmd->elapsedGpuMilliseconds(), "legacy elapsedGpuMilliseconds", totals);
    check(std::isfinite(legacyMs) && legacyMs >= 0.0, "legacy GPU ms finite and >= 0", totals);

    const auto scopes = take(cmd->timestampScopes(), "timestampScopes", totals);
    check(scopes.size() == 5u, "5 recorded scopes returned", totals);
    const std::array<const char*, 5> expectedNames{"Frame", "Upload", "Compute", "GraphicsPass", "Readback"};
    const std::array<std::uint32_t, 5> expectedDepths{0u, 1u, 1u, 1u, 1u};
    for (std::size_t i = 0; i < 5u; ++i) {
        check(scopes[i].name == expectedNames[i], "scope name matches", totals);
        check(scopes[i].depth == expectedDepths[i], "scope depth matches", totals);
        check(scopes[i].queue == gfx::QueueType::Graphics, "scope queue matches", totals);
        check(scopes[i].gpuMilliseconds.has_value() && std::isfinite(*scopes[i].gpuMilliseconds) &&
              *scopes[i].gpuMilliseconds >= 0.0, "scope GPU ms present, finite, and >= 0", totals);
        ++totals.scopesVerified;
    }
    // Outer "Frame" duration must cover each child (within small 0.5 ms tolerance for pipeline stage overlap).
    const double frameMs = *scopes[0].gpuMilliseconds;
    for (std::size_t i = 1; i < 5u; ++i) {
        check(*scopes[i].gpuMilliseconds <= frameMs + 0.5, "child GPU scope bounded by Frame scope", totals);
    }
    // Legacy 2-slot and outer Frame scope bracket the same command sequence and should agree closely.
    check(std::abs(legacyMs - frameMs) <= std::max(1.0, legacyMs * 0.5 + 0.5),
          "legacy elapsedGpuMilliseconds agrees with outer Frame scope", totals);

    // Calling timestampScopes() again returns cached values without duplicating into profilingSnapshot().
    const auto scopes2 = take(cmd->timestampScopes(), "timestampScopes cached", totals);
    check(scopes2.size() == 5u && *scopes2[0].gpuMilliseconds == frameMs, "cached timestampScopes stable", totals);
    const auto snap = take(device.profilingSnapshot(), "snapshot with scopes", totals);
    check(snap.gpuTimestampScopes.size() == 5u, "profilingSnapshot aggregated exactly 5 scopes without duplication", totals);

    // 4. Max capacity test: exactly kMaxTimestampScopesPerCommandList (32) scopes succeed; 33rd rejected!
    {
        auto maxCmd = take(device.createCommandList(), "max scope cmd", totals);
        take(maxCmd->begin(), "max scope begin", totals);
        take(maxCmd->transition(rt, gfx::ResourceState::RenderTarget), "max scope rt -> RT", totals);
        take(maxCmd->beginRenderPass(rt, {0.0f, 0.0f, 0.0f, 1.0f}), "max scope RP", totals);
        take(maxCmd->setPipeline(gfxPipe), "max scope pipe", totals);
        if (gfxDesc.sampledTexture) take(maxCmd->bindTexture(sampleTex), "max scope tex", totals);
        for (std::uint32_t i = 0; i < gfx::kMaxTimestampScopesPerCommandList; ++i) {
            take(maxCmd->beginTimestampScope("S" + std::to_string(i)), "max beginScope", totals);
            take(maxCmd->draw(3, 1, 0, 0), "max draw", totals);
            take(maxCmd->endTimestampScope(), "max endScope", totals);
        }
        reject(maxCmd->beginTimestampScope("Overflow33"), gfx::ErrorCode::InvalidState,
               "33rd timestamp scope exceeds capacity", totals);
        take(maxCmd->endRenderPass(), "max endRP", totals);
        take(maxCmd->resolveTimestampScopes(), "max resolve", totals);
        take(maxCmd->end(), "max end", totals);
        auto mf = take(device.queue().submit(*maxCmd), "max submit", totals);
        take(device.queue().wait(mf), "max wait", totals);
        const auto maxScopes = take(maxCmd->timestampScopes(), "max timestampScopes", totals);
        check(maxScopes.size() == gfx::kMaxTimestampScopesPerCommandList, "all 32 scopes resolved", totals);
        for (std::uint32_t i = 0; i < gfx::kMaxTimestampScopesPerCommandList; ++i) {
            check(maxScopes[i].name == ("S" + std::to_string(i)), "32-scope name exact", totals);
            check(maxScopes[i].gpuMilliseconds.has_value() && *maxScopes[i].gpuMilliseconds >= 0.0,
                  "32-scope GPU ms valid", totals);
            ++totals.scopesVerified;
        }
    }

    take(device.setProfilingEnabled(false), "disable profiling after scopes", totals);
}

void testRenderGraphAutoProfilingIntegration(gfx::Device& device, gfx::Backend backend, Totals& totals) {
    constexpr std::uint32_t kCount = 256;
    constexpr std::uint64_t kBytes = kCount * sizeof(std::uint32_t);
    auto inUp = take(device.createBuffer({kBytes, gfx::Memory::Upload}), "rg inUp", totals);
    auto cbUp = take(device.createBuffer({256, gfx::Memory::Upload}), "rg cbUp", totals);
    auto outRb = take(device.createBuffer({kBytes, gfx::Memory::Readback}), "rg outRb", totals);
    std::vector<std::uint32_t> data(kCount, 5u);
    take(inUp->write(0, std::as_bytes(std::span{data})), "rg write data", totals);
    std::array<float, 4> params{2.0f, 3.0f, 0.0f, 0.0f};
    take(cbUp->write(0, std::as_bytes(std::span{params})), "rg write params", totals);
    auto compPipe = makeBufferComputePipeline(device, backend, totals);

    gfx::RenderGraph g;
    auto hIn = take(g.importBuffer("in", inUp), "rg import in", totals);
    auto hCbUp = take(g.importBuffer("cbUp", cbUp), "rg import cbUp", totals);
    auto hOut = take(g.importBuffer("out", outRb), "rg import out", totals);
    auto hBuf = take(g.createBuffer("buf", {kBytes, gfx::Memory::Device, true}), "rg buf", totals);
    auto hCb = take(g.createBuffer("cb", {256, gfx::Memory::Device}), "rg cb", totals);

    (void)take(g.addPass({
        "RG_Upload", gfx::QueueType::Copy, false,
        {{hIn, gfx::ResourceState::CopySource}, {hCbUp, gfx::ResourceState::CopySource}},
        {{hBuf, gfx::ResourceState::CopyDestination}, {hCb, gfx::ResourceState::CopyDestination}}, {},
        [&](gfx::CommandList& cmd, const gfx::RenderGraphResources& res) -> gfx::Result<void> {
            auto s = res.buffer(hIn); if (!s) return std::unexpected(s.error());
            auto d = res.buffer(hBuf); if (!d) return std::unexpected(d.error());
            auto cs = res.buffer(hCbUp); if (!cs) return std::unexpected(cs.error());
            auto cd = res.buffer(hCb); if (!cd) return std::unexpected(cd.error());
            if (auto ok = cmd.copyBuffer(*s, *d, kBytes); !ok) return ok;
            return cmd.copyBuffer(*cs, *cd, 256);
        }
    }), "rg add upload", totals);

    (void)take(g.addPass({
        "RG_Compute", gfx::QueueType::Compute, false,
        {{hCb, gfx::ResourceState::ConstantBuffer}, {hBuf, gfx::ResourceState::UnorderedAccess}},
        {{hBuf, gfx::ResourceState::UnorderedAccess}}, {},
        [&](gfx::CommandList& cmd, const gfx::RenderGraphResources& res) -> gfx::Result<void> {
            auto b = res.buffer(hBuf); if (!b) return std::unexpected(b.error());
            auto c = res.buffer(hCb); if (!c) return std::unexpected(c.error());
            if (auto ok = cmd.setComputePipeline(compPipe); !ok) return ok;
            gfx::ResourceBindings bind{};
            bind.storageBuffers = {{0, 0, *b, 0, kBytes}};
            bind.constants = {{1, 0, *c, 0, 256}};
            if (auto ok = cmd.bindComputeResources(bind); !ok) return ok;
            return cmd.dispatch(kCount / 64u, 1, 1);
        }
    }), "rg add compute", totals);

    (void)take(g.addPass({
        "RG_Readback", gfx::QueueType::Copy, false,
        {{hBuf, gfx::ResourceState::CopySource}},
        {{hOut, gfx::ResourceState::CopyDestination}}, {},
        [&](gfx::CommandList& cmd, const gfx::RenderGraphResources& res) -> gfx::Result<void> {
            auto s = res.buffer(hBuf); if (!s) return std::unexpected(s.error());
            auto d = res.buffer(hOut); if (!d) return std::unexpected(d.error());
            return cmd.copyBuffer(*s, *d, kBytes);
        }
    }), "rg add readback", totals);

    (void)take(g.compile(), "rg compile", totals);

    // Run with profiling ON -> counters + timelineWaits + automatic RG_Compute GPU scope recorded!
    take(device.setProfilingEnabled(true), "rg prof ON", totals);
    take(device.resetProfilingStatistics(), "rg prof reset", totals);
    take(g.execute(device), "rg execute ON", totals);

    const auto onSnap = take(device.profilingSnapshot(), "rg snapshot ON", totals);
    check(onSnap.enabled, "rg snap enabled", totals);
    check(onSnap.counters.dispatchCalls == 1u, "rg dispatchCalls == 1", totals);
    check(onSnap.counters.copyCommands == 3u, "rg copyCommands == 3", totals);
    check(onSnap.counters.submissions == 3u, "rg submissions == 3", totals);
    check(onSnap.counters.timelineWaits >= 1u, "rg timelineWaits >= 1", totals);
    check(onSnap.cpuStallMilliseconds.has_value() && *onSnap.cpuStallMilliseconds >= 0.0,
          "rg cpuStallMilliseconds present", totals);
    if (device.caps().timestampQueries) {
        bool foundComputeScope = false;
        for (const auto& sc : onSnap.gpuTimestampScopes) {
            if (sc.name == "RG_Compute" && sc.queue == gfx::QueueType::Compute &&
                sc.gpuMilliseconds.has_value() && *sc.gpuMilliseconds >= 0.0) {
                foundComputeScope = true;
            }
        }
        check(foundComputeScope, "RenderGraph automatically recorded RG_Compute GPU timestamp scope", totals);
    }

    // Run with profiling OFF -> null contract preserved!
    take(device.setProfilingEnabled(false), "rg prof OFF", totals);
    take(g.execute(device), "rg execute OFF", totals);
    const auto offSnap = take(device.profilingSnapshot(), "rg snapshot OFF", totals);
    check(!offSnap.enabled && !offSnap.cpuStallMilliseconds.has_value() && offSnap.gpuTimestampScopes.empty(),
          "RenderGraph OFF null contract preserved", totals);
}

void testCleanlinessAndWarpRecovery(gfx::Device& device, bool warp, Totals& totals) {
    // Verify lazy scopeReadback and all temporary resources are cleanly freed.
    const auto stats = take(device.resourceStatistics(), "resourceStatistics after profiling tests", totals);
    check(stats.uniqueResources == 0u && stats.resourceAllocationBytes == 0u,
          "0 leaked resources after profiling tests", totals);

    if (!warp) return;

    take(device.removeForTesting(), "WARP removeForTesting", totals);
    reject(device.setProfilingEnabled(true), gfx::ErrorCode::DeviceLost, "setProfilingEnabled after loss", totals);
    reject(device.profilingSnapshot(), gfx::ErrorCode::DeviceLost, "profilingSnapshot after loss", totals);
    reject(device.resetProfilingStatistics(), gfx::ErrorCode::DeviceLost, "resetProfilingStatistics after loss", totals);

    auto recovered = take(device.recover(), "WARP recover", totals);
    check(!recovered->isProfilingEnabled(), "recovered device starts with profiling OFF", totals);
    take(recovered->setProfilingEnabled(true), "recovered setProfilingEnabled(true)", totals);
    const auto snap = take(recovered->profilingSnapshot(), "recovered profilingSnapshot", totals);
    check(snap.enabled && snap.counters.drawCalls == 0u, "recovered profilingSnapshot clean", totals);
}

void runProfilingOverheadBenchmark(gfx::Device& device, gfx::Backend backend, Totals& totals) {
    constexpr int kFrames = 20;
    constexpr int kDrawsPerFrame = 64;
    constexpr int kWarmup = 2;
    constexpr int kMeasured = 5;

    auto gfxDesc = makeSimpleGraphicsDesc(backend, totals);
    auto gfxPipe = take(device.createGraphicsPipeline(gfxDesc), "bench pipe", totals);
    auto rt = take(device.createTexture({32, 32, gfx::Format::RGBA8UNorm, true}), "bench rt", totals);
    auto sampleTex = makeInitializedSampleTexture(device, 32, 32, totals);
    auto pool = take(device.createCommandPool(), "bench pool", totals);
    auto cmd = take(pool->createCommandList(), "bench cmd", totals);

    // Prime initial states once.
    take(cmd->begin(), "prime begin", totals);
    take(cmd->transition(sampleTex, gfx::ResourceState::ShaderRead), "prime SR", totals);
    take(cmd->transition(rt, gfx::ResourceState::RenderTarget), "prime RT", totals);
    take(cmd->end(), "prime end", totals);
    auto pf = take(device.queue().submit(*cmd), "prime submit", totals);
    take(device.queue().wait(pf), "prime wait", totals);

    auto runBatch = [&](bool profOn) -> double {
        take(device.setProfilingEnabled(profOn), "bench setProf", totals);
        if (profOn) take(device.resetProfilingStatistics(), "bench resetProf", totals);
        const auto t0 = std::chrono::steady_clock::now();
        for (int f = 0; f < kFrames; ++f) {
            take(pool->reset(), "bench pool reset", totals);
            take(cmd->begin(), "bench begin", totals);
            if (profOn && device.caps().timestampQueries) {
                take(cmd->beginTimestampScope("BenchFrame"), "bench scope begin", totals);
            }
            take(cmd->beginRenderPass(rt, {0.1f, 0.2f, 0.3f, 1.0f}), "bench RP", totals);
            take(cmd->setPipeline(gfxPipe), "bench setPipe", totals);
            if (gfxDesc.sampledTexture) take(cmd->bindTexture(sampleTex), "bench bindTex", totals);
            for (int d = 0; d < kDrawsPerFrame; ++d) {
                take(cmd->draw(3, 1, 0, 0), "bench draw", totals);
            }
            take(cmd->endRenderPass(), "bench endRP", totals);
            if (profOn && device.caps().timestampQueries) {
                take(cmd->endTimestampScope(), "bench scope end", totals);
                take(cmd->resolveTimestampScopes(), "bench scope resolve", totals);
            }
            take(cmd->end(), "bench end", totals);
            auto fnc = take(device.queue().submit(*cmd), "bench submit", totals);
            take(device.queue().wait(fnc), "bench wait", totals);
            if (profOn && device.caps().timestampQueries) {
                (void)take(cmd->timestampScopes(), "bench read scopes", totals);
            }
        }
        const auto t1 = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(t1 - t0).count();
    };

    auto median = [](std::vector<double> v) {
        std::ranges::sort(v);
        return v[v.size() / 2u];
    };

    for (int w = 0; w < kWarmup; ++w) {
        (void)runBatch(false);
        (void)runBatch(true);
    }
    std::vector<double> offTimes, onTimes;
    for (int m = 0; m < kMeasured; ++m) {
        offTimes.push_back(runBatch(false));
        onTimes.push_back(runBatch(true));
    }
    const double offMed = median(offTimes);
    const double onMed = median(onTimes);
    const double deltaPct = ((onMed - offMed) / std::max(0.001, offMed)) * 100.0;

    const auto finalSnap = take(device.profilingSnapshot(), "bench final snap", totals);
    check(finalSnap.counters.drawCalls == static_cast<std::uint64_t>(kFrames * kDrawsPerFrame),
          "bench draw count exact", totals);
    double avgGpuMs = 0.0;
    if (!finalSnap.gpuTimestampScopes.empty()) {
        for (const auto& s : finalSnap.gpuTimestampScopes) {
            if (s.gpuMilliseconds) avgGpuMs += *s.gpuMilliseconds;
        }
        avgGpuMs /= static_cast<double>(finalSnap.gpuTimestampScopes.size());
    }
    take(device.setProfilingEnabled(false), "bench restore OFF", totals);

    std::cout << std::fixed << std::setprecision(3)
              << "BENCHMARK_PROF_20F: off_ms=" << offMed
              << " on_with_gpu_scopes_ms=" << onMed
              << " (delta=" << (deltaPct >= 0.0 ? "+" : "") << deltaPct << "%)"
              << " avg_gpu_scope_ms=" << avgGpuMs
              << " draws=" << finalSnap.counters.drawCalls << "\n";
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

        testInstrumentationOffNullContract(*device, backend, totals);
        testExactCountersCacheStallAndVramContract(*device, backend, totals);
        testMultiScopeGpuTimestampsAndLegacyCoexistence(*device, backend, totals);
        testRenderGraphAutoProfilingIntegration(*device, backend, totals);
        if (benchmark) {
            runProfilingOverheadBenchmark(*device, backend, totals);
        }

        std::uint64_t valErrors = 0;
        if (validation) {
            valErrors = take(device->validationErrorCount(), "validationErrorCount", totals);
            check(valErrors == 0u, "validationErrorCount must be 0", totals);
        }

        testCleanlinessAndWarpRecovery(*device, warp, totals);

        std::cout << "PASS profiling backend="
                  << (backend == gfx::Backend::D3D12 ? "D3D12" : "Vulkan")
                  << " adapter=\"" << caps.adapterName << "\""
                  << " validation=" << (validation ? "on" : "off")
                  << " warp=" << (warp ? "on" : "off")
                  << " val_errors=" << valErrors
                  << " scopes_verified=" << totals.scopesVerified
                  << " rejections=" << totals.rejections
                  << " assertions=" << totals.assertions << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "FAIL profiling: " << ex.what() << "\n";
        return 1;
    }
}
