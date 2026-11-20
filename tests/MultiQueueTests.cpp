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
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Totals {
    std::uint64_t assertions{};
    std::uint64_t pipelines{};
    std::uint64_t submits{};
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

gfx::ShaderBinary makeBufferComputeShader(gfx::Backend backend, Totals& totals) {
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
        return take(gfx::compileHlsl(hlsl, "CS", gfx::ShaderStage::Compute), "compile HLSL buffer CS", totals);
    }
    return spirvBinary(gfx::ShaderStage::Compute, computeFixtures::buffer_scale_add);
}

gfx::ShaderBinary makeTextureComputeShader(gfx::Backend backend, Totals& totals) {
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
        return take(gfx::compileHlsl(hlsl, "CS", gfx::ShaderStage::Compute), "compile HLSL texture CS", totals);
    }
    return spirvBinary(gfx::ShaderStage::Compute, computeFixtures::texture_generate);
}

gfx::PipelinePtr makeBufferComputePipeline(gfx::Device& device, gfx::Backend backend, Totals& totals) {
    gfx::ComputePipelineDesc desc{};
    desc.compute = makeBufferComputeShader(backend, totals);
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
    gfx::ComputePipelineDesc desc{};
    desc.compute = makeTextureComputeShader(backend, totals);
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

void testTimelineCpuContract(gfx::Device& device, Totals& totals) {
    auto tf = take(device.createTimelineFence(10), "createTimelineFence(10)", totals);
    check(take(tf->completedValue(), "completedValue initial", totals) == 10u,
          "initial timeline value must be 10", totals);
    take(tf->wait(10, std::chrono::milliseconds(0)), "wait(10, 0ms)", totals);
    take(tf->wait(0, std::chrono::milliseconds(0)), "wait(0, 0ms)", totals);
    reject(tf->wait(11, std::chrono::milliseconds(0)), gfx::ErrorCode::Timeout,
           "wait(11, 0ms) must time out", totals);
    reject(tf->wait(11, std::chrono::milliseconds(2)), gfx::ErrorCode::Timeout,
           "wait(11, 2ms) must time out", totals);
    take(tf->signal(15), "signal(15)", totals);
    check(take(tf->completedValue(), "completedValue after signal(15)", totals) == 15u,
          "timeline value must be 15 after CPU signal", totals);
    take(tf->wait(15, std::chrono::milliseconds(0)), "wait(15, 0ms)", totals);
    reject(tf->signal(15), gfx::ErrorCode::InvalidArgument, "duplicate signal(15) rejected", totals);
    reject(tf->signal(14), gfx::ErrorCode::InvalidArgument, "decreasing signal(14) rejected", totals);
    reject(tf->signal(std::numeric_limits<std::uint64_t>::max()), gfx::ErrorCode::InvalidArgument,
           "UINT64_MAX signal rejected", totals);
    reject(device.createTimelineFence(std::numeric_limits<std::uint64_t>::max()), gfx::ErrorCode::InvalidArgument,
           "UINT64_MAX initial timeline value rejected", totals);
}

void testCopyComputeCopyPipeline(gfx::Device& device, gfx::Backend backend, Totals& totals) {
    auto* copyQ = take(device.queue(gfx::QueueType::Copy), "queue(Copy)", totals);
    auto* computeQ = take(device.queue(gfx::QueueType::Compute), "queue(Compute)", totals);

    constexpr std::uint32_t kCount = 1024;
    constexpr std::uint64_t kBytes = kCount * sizeof(std::uint32_t);
    auto uploadBuf = take(device.createBuffer({kBytes, gfx::Memory::Upload}), "upload buf", totals);
    auto storageBuf = take(device.createBuffer({kBytes, gfx::Memory::Device, true}), "storage buf", totals);
    auto cbUpload = take(device.createBuffer({256, gfx::Memory::Upload}), "cb upload", totals);
    auto cbDevice = take(device.createBuffer({256, gfx::Memory::Device}), "cb device", totals);
    auto readbackBuf = take(device.createBuffer({kBytes, gfx::Memory::Readback}), "readback buf", totals);

    std::vector<std::uint32_t> input(kCount);
    for (std::uint32_t i = 0; i < kCount; ++i) input[i] = i * 5u + 11u;
    take(uploadBuf->write(0, std::as_bytes(std::span{input})), "write input", totals);

    std::array<float, 4> params{3.0f, 17.0f, 0.0f, 0.0f};
    take(cbUpload->write(0, std::as_bytes(std::span{params})), "write cb params", totals);

    auto computePipe = makeBufferComputePipeline(device, backend, totals);
    auto timeline = take(device.createTimelineFence(0), "timeline(0)", totals);

    auto copyUploadCmd = take(device.createCommandList(gfx::QueueType::Copy), "copy upload cmd", totals);
    auto computeCmd = take(device.createCommandList(gfx::QueueType::Compute), "compute cmd", totals);
    auto copyReadbackCmd = take(device.createCommandList(gfx::QueueType::Copy), "copy readback cmd", totals);
    check(copyUploadCmd->queueType() == gfx::QueueType::Copy, "copy cmd queueType", totals);
    check(computeCmd->queueType() == gfx::QueueType::Compute, "compute cmd queueType", totals);

    // Stage 1 (Copy queue): upload input & constants -> signal timeline=1.
    take(copyUploadCmd->begin(), "copy1 begin", totals);
    take(copyUploadCmd->transition(storageBuf, gfx::ResourceState::CopyDestination), "storage -> CopyDst", totals);
    take(copyUploadCmd->transition(cbDevice, gfx::ResourceState::CopyDestination), "cb -> CopyDst", totals);
    take(copyUploadCmd->copyBuffer(uploadBuf, storageBuf, kBytes), "copy upload -> storage", totals);
    take(copyUploadCmd->copyBuffer(cbUpload, cbDevice, 256), "copy cbUpload -> cbDevice", totals);
    take(copyUploadCmd->transition(storageBuf, gfx::ResourceState::Common), "storage -> Common", totals);
    take(copyUploadCmd->transition(cbDevice, gfx::ResourceState::Common), "cb -> Common", totals);
    take(copyUploadCmd->end(), "copy1 end", totals);

    gfx::CommandList* stage1Lists[]{copyUploadCmd.get()};
    gfx::TimelineSignal stage1Signals[]{{timeline, 1}};
    take(copyQ->submit({stage1Lists, {}, stage1Signals}), "submit stage1 Copy", totals);
    ++totals.submits;

    // Stage 2 (Compute queue): wait timeline=1 -> scale & add -> signal timeline=2.
    take(computeCmd->begin(), "compute begin", totals);
    take(computeCmd->transition(storageBuf, gfx::ResourceState::UnorderedAccess), "storage -> UAV", totals);
    take(computeCmd->transition(cbDevice, gfx::ResourceState::ConstantBuffer), "cb -> CB", totals);
    take(computeCmd->setComputePipeline(computePipe), "setComputePipeline", totals);
    gfx::ResourceBindings bindings{};
    bindings.storageBuffers = {{0, 0, storageBuf, 0, kBytes}};
    bindings.constants = {{1, 0, cbDevice, 0, 256}};
    take(computeCmd->bindComputeResources(bindings), "bindComputeResources", totals);
    take(computeCmd->dispatch(kCount / 64u, 1, 1), "dispatch", totals);
    take(computeCmd->transition(storageBuf, gfx::ResourceState::Common), "storage -> Common after CS", totals);
    take(computeCmd->end(), "compute end", totals);

    gfx::CommandList* stage2Lists[]{computeCmd.get()};
    gfx::TimelineWait stage2Waits[]{{timeline, 1}};
    gfx::TimelineSignal stage2Signals[]{{timeline, 2}};
    take(computeQ->submit({stage2Lists, stage2Waits, stage2Signals}), "submit stage2 Compute", totals);
    ++totals.submits;

    // Stage 3 (Copy queue): wait timeline=2 -> readback -> signal timeline=3.
    take(copyReadbackCmd->begin(), "copy2 begin", totals);
    take(copyReadbackCmd->transition(storageBuf, gfx::ResourceState::CopySource), "storage -> CopySrc", totals);
    take(copyReadbackCmd->copyBuffer(storageBuf, readbackBuf, kBytes), "copy storage -> readback", totals);
    take(copyReadbackCmd->end(), "copy2 end", totals);

    gfx::CommandList* stage3Lists[]{copyReadbackCmd.get()};
    gfx::TimelineWait stage3Waits[]{{timeline, 2}};
    gfx::TimelineSignal stage3Signals[]{{timeline, 3}};
    take(copyQ->submit({stage3Lists, stage3Waits, stage3Signals}), "submit stage3 Copy", totals);
    ++totals.submits;

    // Single CPU wait at the very end of the 3-stage GPU-only cross-queue chain!
    take(timeline->wait(3, std::chrono::seconds(10)), "timeline->wait(3)", totals);
    check(take(timeline->completedValue(), "timeline completed == 3", totals) == 3u,
          "timeline must report 3 after wait(3)", totals);

    const auto outBytes = take(readbackBuf->read(0, kBytes), "readback bytes immediately after timeline->wait(3)", totals);
    check(outBytes.size() == kBytes, "readback byte size matches", totals);
    for (std::uint32_t i = 0; i < kCount; ++i) {
        std::uint32_t got{};
        std::memcpy(&got, outBytes.data() + i * sizeof(std::uint32_t), sizeof(std::uint32_t));
        const std::uint32_t expected = input[i] * 3u + 17u;
        if (got != expected) {
            throw std::runtime_error("Copy->Compute->Copy mismatch at index " + std::to_string(i) +
                ": expected " + std::to_string(expected) + ", got " + std::to_string(got));
        }
    }
    ++totals.assertions;
    ++totals.pipelines;
}

void testFourStageCopyComputeGraphicsCopyPipeline(gfx::Device& device, gfx::Backend backend, Totals& totals) {
    auto* copyQ = take(device.queue(gfx::QueueType::Copy), "queue(Copy)", totals);
    auto* computeQ = take(device.queue(gfx::QueueType::Compute), "queue(Compute)", totals);
    auto* graphicsQ = take(device.queue(gfx::QueueType::Graphics), "queue(Graphics)", totals);

    auto cbUpload = take(device.createBuffer({256, gfx::Memory::Upload}), "img cbUpload", totals);
    auto cbDevice = take(device.createBuffer({256, gfx::Memory::Device}), "img cbDevice", totals);
    constexpr float kR = 0.85f, kG = 0.55f, kB = 0.25f;
    std::array<float, 4> colorParams{kR, kG, kB, 1.0f};
    take(cbUpload->write(0, std::as_bytes(std::span{colorParams})), "write colorParams", totals);

    gfx::TextureDesc storageDesc{16, 16, gfx::Format::RGBA8UNorm, false, false, true};
    auto storageTex = take(device.createTexture(storageDesc), "create storage texture", totals);
    gfx::TextureDesc rtDesc{16, 16, gfx::Format::RGBA8UNorm, true};
    auto renderTex = take(device.createTexture(rtDesc), "create render target", totals);
    const auto fp = renderTex->footprint();
    auto readbackBuf = take(device.createBuffer({fp.size, gfx::Memory::Readback}), "create img readback", totals);

    auto texComputePipe = makeTextureComputePipeline(device, backend, totals);
    auto gfxPipe = makeSampledGraphicsPipeline(device, backend, totals);
    auto timeline = take(device.createTimelineFence(0), "4-stage timeline", totals);

    auto copy1 = take(device.createCommandList(gfx::QueueType::Copy), "4-stage copy1", totals);
    auto compute = take(device.createCommandList(gfx::QueueType::Compute), "4-stage compute", totals);
    auto graphics = take(device.createCommandList(gfx::QueueType::Graphics), "4-stage graphics", totals);
    auto copy2 = take(device.createCommandList(gfx::QueueType::Copy), "4-stage copy2", totals);

    // Stage 1 (Copy): upload color constants -> signal 1
    take(copy1->begin(), "4s copy1 begin", totals);
    take(copy1->transition(cbDevice, gfx::ResourceState::CopyDestination), "4s cb -> CopyDst", totals);
    take(copy1->copyBuffer(cbUpload, cbDevice, 256), "4s copy cb", totals);
    take(copy1->transition(cbDevice, gfx::ResourceState::Common), "4s cb -> Common", totals);
    take(copy1->end(), "4s copy1 end", totals);
    {
        gfx::CommandList* lists[]{copy1.get()};
        gfx::TimelineSignal sigs[]{{timeline, 1}};
        take(copyQ->submit({lists, {}, sigs}), "4s submit Copy1", totals);
        ++totals.submits;
    }

    // Stage 2 (Compute): wait 1 -> generate 16x16 gradient into storageTex -> signal 2
    take(compute->begin(), "4s compute begin", totals);
    take(compute->transition(cbDevice, gfx::ResourceState::ConstantBuffer), "4s cb -> CB", totals);
    take(compute->transition(storageTex, gfx::ResourceState::UnorderedAccess), "4s storageTex -> UAV", totals);
    take(compute->setComputePipeline(texComputePipe), "4s setComputePipeline", totals);
    gfx::ResourceBindings csBindings{};
    csBindings.storageTextures = {{0, 0, storageTex}};
    csBindings.constants = {{1, 0, cbDevice, 0, 256}};
    take(compute->bindComputeResources(csBindings), "4s bindComputeResources", totals);
    take(compute->dispatch(2, 2, 1), "4s dispatch texture_generate", totals);
    take(compute->transition(storageTex, gfx::ResourceState::Common), "4s storageTex -> Common", totals);
    take(compute->end(), "4s compute end", totals);
    {
        gfx::CommandList* lists[]{compute.get()};
        gfx::TimelineWait waits[]{{timeline, 1}};
        gfx::TimelineSignal sigs[]{{timeline, 2}};
        take(computeQ->submit({lists, waits, sigs}), "4s submit Compute", totals);
        ++totals.submits;
    }

    // Stage 3 (Graphics): wait 2 -> sample storageTex into renderTex -> transition renderTex -> Common -> signal 3
    take(graphics->begin(), "4s graphics begin", totals);
    take(graphics->transition(storageTex, gfx::ResourceState::ShaderRead), "4s storageTex -> ShaderRead", totals);
    take(graphics->transition(renderTex, gfx::ResourceState::RenderTarget), "4s renderTex -> RT", totals);
    take(graphics->beginRenderPass(renderTex, {0.0f, 0.0f, 0.0f, 1.0f}), "4s beginRenderPass", totals);
    take(graphics->setPipeline(gfxPipe), "4s setPipeline", totals);
    take(graphics->bindTexture(storageTex), "4s bindTexture", totals);
    take(graphics->draw(3, 1, 0, 0), "4s draw fullscreen", totals);
    take(graphics->endRenderPass(), "4s endRenderPass", totals);
    take(graphics->transition(renderTex, gfx::ResourceState::Common), "4s renderTex -> Common", totals);
    take(graphics->end(), "4s graphics end", totals);
    {
        gfx::CommandList* lists[]{graphics.get()};
        gfx::TimelineWait waits[]{{timeline, 2}};
        gfx::TimelineSignal sigs[]{{timeline, 3}};
        take(graphicsQ->submit({lists, waits, sigs}), "4s submit Graphics", totals);
        ++totals.submits;
    }

    // Stage 4 (Copy): wait 3 -> copy renderTex -> readbackBuf -> signal 4
    take(copy2->begin(), "4s copy2 begin", totals);
    take(copy2->transition(renderTex, gfx::ResourceState::CopySource), "4s renderTex -> CopySrc", totals);
    take(copy2->copyTextureToBuffer(renderTex, readbackBuf), "4s copyTextureToBuffer", totals);
    take(copy2->end(), "4s copy2 end", totals);
    {
        gfx::CommandList* lists[]{copy2.get()};
        gfx::TimelineWait waits[]{{timeline, 3}};
        gfx::TimelineSignal sigs[]{{timeline, 4}};
        take(copyQ->submit({lists, waits, sigs}), "4s submit Copy2", totals);
        ++totals.submits;
    }

    // Single CPU wait for the entire 4-stage Copy -> Compute -> Graphics -> Copy pipeline!
    take(timeline->wait(4, std::chrono::seconds(10)), "4s timeline->wait(4)", totals);
    check(take(timeline->completedValue(), "4s completed == 4", totals) == 4u,
          "4-stage timeline must reach 4", totals);

    const auto pixels = take(readbackBuf->read(0, fp.size), "4s readback pixels", totals);
    const bool flipY = true;
    for (std::uint32_t y = 0; y < fp.height; ++y) {
        const std::uint32_t sampleY = flipY ? (fp.height - 1u - y) : y;
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
                throw std::runtime_error("4-stage pixel mismatch at (" + std::to_string(x) + "," + std::to_string(y) +
                    "): expected (" + std::to_string(er) + "," + std::to_string(eg) + "," + std::to_string(eb) + ",255), got (" +
                    std::to_string(gr) + "," + std::to_string(gg) + "," + std::to_string(gb) + "," + std::to_string(ga) + ")");
            }
        }
    }
    ++totals.assertions;
    ++totals.pipelines;
}

void testOverlappedFramesInFlight(gfx::Device& device, gfx::Backend backend, Totals& totals) {
    auto* copyQ = take(device.queue(gfx::QueueType::Copy), "overlap queue(Copy)", totals);
    auto* computeQ = take(device.queue(gfx::QueueType::Compute), "overlap queue(Compute)", totals);

    constexpr std::uint32_t kSlots = 2;
    constexpr std::uint32_t kFrames = 8;
    constexpr std::uint32_t kCount = 512;
    constexpr std::uint64_t kBytes = kCount * sizeof(std::uint32_t);

    auto computePipe = makeBufferComputePipeline(device, backend, totals);
    auto cbBuf = take(device.createBuffer({256, gfx::Memory::Upload}), "overlap cb", totals);
    std::array<float, 4> params{2.0f, 5.0f, 0.0f, 0.0f};
    take(cbBuf->write(0, std::as_bytes(std::span{params})), "overlap cb write", totals);

    auto uploadTimeline = take(device.createTimelineFence(0), "uploadTimeline", totals);
    auto computeTimeline = take(device.createTimelineFence(0), "computeTimeline", totals);
    auto readbackTimeline = take(device.createTimelineFence(0), "readbackTimeline", totals);

    struct Slot {
        gfx::BufferPtr upload;
        gfx::BufferPtr deviceBuf;
        gfx::BufferPtr readback;
        std::unique_ptr<gfx::CommandPool> copyUpPool;
        std::unique_ptr<gfx::CommandList> copyUpCmd;
        std::unique_ptr<gfx::CommandPool> computePool;
        std::unique_ptr<gfx::CommandList> computeCmd;
        std::unique_ptr<gfx::CommandPool> copyDownPool;
        std::unique_ptr<gfx::CommandList> copyDownCmd;
        std::uint64_t pendingReadbackVal{};
        std::uint32_t pendingFrame{};
    };
    std::array<Slot, kSlots> slots;
    for (std::uint32_t s = 0; s < kSlots; ++s) {
        slots[s].upload = take(device.createBuffer({kBytes, gfx::Memory::Upload}), "slot upload", totals);
        slots[s].deviceBuf = take(device.createBuffer({kBytes, gfx::Memory::Device, true}), "slot dev", totals);
        slots[s].readback = take(device.createBuffer({kBytes, gfx::Memory::Readback}), "slot readback", totals);
        slots[s].copyUpPool = take(device.createCommandPool(gfx::QueueType::Copy), "slot copyUpPool", totals);
        check(slots[s].copyUpPool->queueType() == gfx::QueueType::Copy, "copyUpPool queueType", totals);
        slots[s].copyUpCmd = take(slots[s].copyUpPool->createCommandList(), "slot copyUpCmd", totals);
        slots[s].computePool = take(device.createCommandPool(gfx::QueueType::Compute), "slot computePool", totals);
        check(slots[s].computePool->queueType() == gfx::QueueType::Compute, "computePool queueType", totals);
        slots[s].computeCmd = take(slots[s].computePool->createCommandList(), "slot computeCmd", totals);
        slots[s].copyDownPool = take(device.createCommandPool(gfx::QueueType::Copy), "slot copyDownPool", totals);
        slots[s].copyDownCmd = take(slots[s].copyDownPool->createCommandList(), "slot copyDownCmd", totals);
    }

    auto verifySlot = [&](const Slot& slot) {
        const auto bytes = take(slot.readback->read(0, kBytes), "overlap readback", totals);
        for (std::uint32_t i = 0; i < kCount; ++i) {
            std::uint32_t got{};
            std::memcpy(&got, bytes.data() + i * 4u, 4u);
            const std::uint32_t expected = (slot.pendingFrame * 1000u + i) * 2u + 5u;
            if (got != expected) {
                throw std::runtime_error("Overlap frame " + std::to_string(slot.pendingFrame) +
                    " mismatch at " + std::to_string(i));
            }
        }
        ++totals.assertions;
    };

    for (std::uint32_t frame = 1; frame <= kFrames; ++frame) {
        Slot& slot = slots[(frame - 1u) % kSlots];
        if (slot.pendingReadbackVal > 0) {
            take(readbackTimeline->wait(slot.pendingReadbackVal, std::chrono::seconds(10)), "wait prior slot", totals);
            verifySlot(slot);
            take(slot.copyUpPool->reset(), "reset copyUpPool", totals);
            take(slot.computePool->reset(), "reset computePool", totals);
            take(slot.copyDownPool->reset(), "reset copyDownPool", totals);
        }

        std::vector<std::uint32_t> data(kCount);
        for (std::uint32_t i = 0; i < kCount; ++i) data[i] = frame * 1000u + i;
        take(slot.upload->write(0, std::as_bytes(std::span{data})), "slot upload write", totals);

        // Copy up -> uploadTimeline = frame
        take(slot.copyUpCmd->begin(), "slot copyUp begin", totals);
        take(slot.copyUpCmd->transition(slot.deviceBuf, gfx::ResourceState::CopyDestination), "slot -> CopyDst", totals);
        take(slot.copyUpCmd->copyBuffer(slot.upload, slot.deviceBuf, kBytes), "slot copyUp", totals);
        take(slot.copyUpCmd->transition(slot.deviceBuf, gfx::ResourceState::Common), "slot -> Common", totals);
        take(slot.copyUpCmd->end(), "slot copyUp end", totals);
        {
            gfx::CommandList* lists[]{slot.copyUpCmd.get()};
            gfx::TimelineSignal sigs[]{{uploadTimeline, frame}};
            take(copyQ->submit({lists, {}, sigs}), "submit overlap CopyUp", totals);
            ++totals.submits;
        }

        // Compute wait uploadTimeline = frame -> signal computeTimeline = frame
        take(slot.computeCmd->begin(), "slot compute begin", totals);
        take(slot.computeCmd->transition(cbBuf, gfx::ResourceState::ConstantBuffer), "slot cb -> CB", totals);
        take(slot.computeCmd->transition(slot.deviceBuf, gfx::ResourceState::UnorderedAccess), "slot -> UAV", totals);
        take(slot.computeCmd->setComputePipeline(computePipe), "slot setComputePipeline", totals);
        gfx::ResourceBindings b{};
        b.storageBuffers = {{0, 0, slot.deviceBuf, 0, kBytes}};
        b.constants = {{1, 0, cbBuf, 0, 256}};
        take(slot.computeCmd->bindComputeResources(b), "slot bindComputeResources", totals);
        take(slot.computeCmd->dispatch(kCount / 64u, 1, 1), "slot dispatch", totals);
        take(slot.computeCmd->transition(slot.deviceBuf, gfx::ResourceState::Common), "slot -> Common after CS", totals);
        take(slot.computeCmd->end(), "slot compute end", totals);
        {
            gfx::CommandList* lists[]{slot.computeCmd.get()};
            gfx::TimelineWait waits[]{{uploadTimeline, frame}};
            gfx::TimelineSignal sigs[]{{computeTimeline, frame}};
            take(computeQ->submit({lists, waits, sigs}), "submit overlap Compute", totals);
            ++totals.submits;
        }

        // Copy down wait computeTimeline = frame -> signal readbackTimeline = frame
        take(slot.copyDownCmd->begin(), "slot copyDown begin", totals);
        take(slot.copyDownCmd->transition(slot.deviceBuf, gfx::ResourceState::CopySource), "slot -> CopySrc", totals);
        take(slot.copyDownCmd->copyBuffer(slot.deviceBuf, slot.readback, kBytes), "slot copyDown", totals);
        take(slot.copyDownCmd->transition(slot.deviceBuf, gfx::ResourceState::Common), "slot -> Common after down", totals);
        take(slot.copyDownCmd->end(), "slot copyDown end", totals);
        {
            gfx::CommandList* lists[]{slot.copyDownCmd.get()};
            gfx::TimelineWait waits[]{{computeTimeline, frame}};
            gfx::TimelineSignal sigs[]{{readbackTimeline, frame}};
            take(copyQ->submit({lists, waits, sigs}), "submit overlap CopyDown", totals);
            ++totals.submits;
        }
        slot.pendingReadbackVal = frame;
        slot.pendingFrame = frame;
    }

    for (std::uint32_t s = 0; s < kSlots; ++s) {
        if (slots[s].pendingReadbackVal > 0) {
            take(readbackTimeline->wait(slots[s].pendingReadbackVal, std::chrono::seconds(10)), "drain final slot", totals);
            verifySlot(slots[s]);
        }
    }
    ++totals.pipelines;
}

void testRejections(gfx::Device& device, gfx::Backend backend, bool softwareAdapter, Totals& totals) {
    auto* gfxQ = take(device.queue(gfx::QueueType::Graphics), "rej queue(Graphics)", totals);
    auto* cmpQ = take(device.queue(gfx::QueueType::Compute), "rej queue(Compute)", totals);
    auto* cpyQ = take(device.queue(gfx::QueueType::Copy), "rej queue(Copy)", totals);

    check(gfxQ == &device.queue(), "queue(Graphics) must equal device.queue()", totals);
    check(gfxQ->type() == gfx::QueueType::Graphics, "gfxQ type", totals);
    check(cmpQ->type() == gfx::QueueType::Compute, "cmpQ type", totals);
    check(cpyQ->type() == gfx::QueueType::Copy, "cpyQ type", totals);

    const auto badType = static_cast<gfx::QueueType>(99);
    reject(device.queue(badType), gfx::ErrorCode::InvalidArgument, "unknown queue type", totals);
    reject(device.createCommandList(badType), gfx::ErrorCode::InvalidArgument, "unknown cmdList queue type", totals);
    reject(device.createCommandPool(badType), gfx::ErrorCode::InvalidArgument, "unknown cmdPool queue type", totals);

    auto storageBuf = take(device.createBuffer({256, gfx::Memory::Device, true}), "rej storage buf", totals);
    auto rtTex = take(device.createTexture({16, 16, gfx::Format::RGBA8UNorm, true}), "rej rt tex", totals);
    auto storageTex = take(device.createTexture({16, 16, gfx::Format::RGBA8UNorm, false, false, true}), "rej storage tex", totals);
    auto computePipe = makeBufferComputePipeline(device, backend, totals);
    auto gfxPipe = makeSampledGraphicsPipeline(device, backend, totals);

    // Copy command list operation & transition rejections.
    auto copyCmd = take(device.createCommandList(gfx::QueueType::Copy), "rej copyCmd", totals);
    take(copyCmd->begin(), "rej copyCmd begin", totals);
    reject(copyCmd->beginRenderPass(rtTex, {}), gfx::ErrorCode::InvalidArgument, "Copy beginRenderPass", totals);
    reject(copyCmd->setPipeline(gfxPipe), gfx::ErrorCode::InvalidArgument, "Copy setPipeline", totals);
    reject(copyCmd->setComputePipeline(computePipe), gfx::ErrorCode::InvalidArgument, "Copy setComputePipeline", totals);
    reject(copyCmd->bindComputeResources({}), gfx::ErrorCode::InvalidArgument, "Copy bindComputeResources", totals);
    reject(copyCmd->dispatch(1, 1, 1), gfx::ErrorCode::InvalidArgument, "Copy dispatch", totals);
    reject(copyCmd->uavBarrier(storageBuf), gfx::ErrorCode::InvalidArgument, "Copy uavBarrier(buf)", totals);
    reject(copyCmd->uavBarrier(storageTex), gfx::ErrorCode::InvalidArgument, "Copy uavBarrier(tex)", totals);
    reject(copyCmd->resolveTexture(rtTex, storageTex), gfx::ErrorCode::InvalidArgument, "Copy resolveTexture", totals);
    reject(copyCmd->transition(storageBuf, gfx::ResourceState::UnorderedAccess), gfx::ErrorCode::InvalidArgument, "Copy transition buf UAV", totals);
    reject(copyCmd->transition(storageBuf, gfx::ResourceState::VertexBuffer), gfx::ErrorCode::InvalidArgument, "Copy transition buf VB", totals);
    reject(copyCmd->transition(rtTex, gfx::ResourceState::RenderTarget), gfx::ErrorCode::InvalidArgument, "Copy transition tex RT", totals);
    reject(copyCmd->transition(storageTex, gfx::ResourceState::ShaderRead), gfx::ErrorCode::InvalidArgument, "Copy transition tex ShaderRead", totals);
    reject(copyCmd->writeTimestamp(0), gfx::ErrorCode::Unsupported, "Copy writeTimestamp unsupported", totals);
    take(copyCmd->end(), "rej copyCmd end", totals);

    // Compute command list graphics operation & transition rejections.
    auto cmpCmd = take(device.createCommandList(gfx::QueueType::Compute), "rej cmpCmd", totals);
    take(cmpCmd->begin(), "rej cmpCmd begin", totals);
    reject(cmpCmd->beginRenderPass(rtTex, {}), gfx::ErrorCode::InvalidArgument, "Compute beginRenderPass", totals);
    reject(cmpCmd->setPipeline(gfxPipe), gfx::ErrorCode::InvalidArgument, "Compute setPipeline", totals);
    reject(cmpCmd->bindVertexBuffer(0, storageBuf, 16), gfx::ErrorCode::InvalidArgument, "Compute bindVertexBuffer", totals);
    reject(cmpCmd->bindIndexBuffer(storageBuf, gfx::IndexFormat::UInt16), gfx::ErrorCode::InvalidArgument, "Compute bindIndexBuffer", totals);
    reject(cmpCmd->resolveTexture(rtTex, storageTex), gfx::ErrorCode::InvalidArgument, "Compute resolveTexture", totals);
    reject(cmpCmd->transition(storageBuf, gfx::ResourceState::VertexBuffer), gfx::ErrorCode::InvalidArgument, "Compute transition buf VB", totals);
    reject(cmpCmd->transition(rtTex, gfx::ResourceState::RenderTarget), gfx::ErrorCode::InvalidArgument, "Compute transition tex RT", totals);
    take(cmpCmd->end(), "rej cmpCmd end", totals);

    auto gfxCmd = take(device.createCommandList(gfx::QueueType::Graphics), "rej gfxCmd", totals);
    take(gfxCmd->begin(), "rej gfxCmd begin", totals);
    take(gfxCmd->transition(rtTex, gfx::ResourceState::RenderTarget), "rej rtTex -> RT", totals);
    take(gfxCmd->end(), "rej gfxCmd end", totals);

    // Wrong-queue submit rejections.
    reject(gfxQ->submit(*copyCmd), gfx::ErrorCode::InvalidArgument, "Copy cmd on Graphics queue", totals);
    reject(cmpQ->submit(*copyCmd), gfx::ErrorCode::InvalidArgument, "Copy cmd on Compute queue", totals);
    reject(gfxQ->submit(*cmpCmd), gfx::ErrorCode::InvalidArgument, "Compute cmd on Graphics queue", totals);
    reject(cpyQ->submit(*cmpCmd), gfx::ErrorCode::InvalidArgument, "Compute cmd on Copy queue", totals);
    reject(cmpQ->submit(*gfxCmd), gfx::ErrorCode::InvalidArgument, "Graphics cmd on Compute queue", totals);
    reject(cpyQ->submit(*gfxCmd), gfx::ErrorCode::InvalidArgument, "Graphics cmd on Copy queue", totals);

    // Submit gfxCmd so rtTex is now in RenderTarget state, then verify Copy list rejects transitioning from RenderTarget!
    auto gfxFence = take(gfxQ->submit(*gfxCmd), "submit gfxCmd for RT state", totals);
    reject(cpyQ->wait(gfxFence, std::chrono::seconds(1)), gfx::ErrorCode::WrongDevice, "wait Graphics fence on Copy queue", totals);
    reject(cmpQ->wait(gfxFence, std::chrono::seconds(1)), gfx::ErrorCode::WrongDevice, "wait Graphics fence on Compute queue", totals);
    take(gfxQ->wait(gfxFence, std::chrono::seconds(5)), "wait gfxFence on Graphics queue", totals);

    auto copyBadState = take(device.createCommandList(gfx::QueueType::Copy), "rej copyBadState", totals);
    take(copyBadState->begin(), "rej copyBadState begin", totals);
    reject(copyBadState->transition(rtTex, gfx::ResourceState::CopySource), gfx::ErrorCode::InvalidState,
           "Copy list cannot transition texture from RenderTarget state", totals);
    take(copyBadState->end(), "rej copyBadState end", totals);

    // SubmitDesc validation rejections.
    auto tf = take(device.createTimelineFence(5), "rej timeline(5)", totals);
    reject(cpyQ->submit(gfx::SubmitDesc{}), gfx::ErrorCode::InvalidArgument, "empty SubmitDesc", totals);
    {
        gfx::CommandList* lists[]{copyCmd.get()};
        gfx::TimelineWait nullWaits[]{{nullptr, 1}};
        reject(cpyQ->submit({lists, nullWaits, {}}), gfx::ErrorCode::InvalidArgument, "null wait fence", totals);
        gfx::TimelineSignal nullSigs[]{{nullptr, 6}};
        reject(cpyQ->submit({lists, {}, nullSigs}), gfx::ErrorCode::InvalidArgument, "null signal fence", totals);
        gfx::TimelineSignal nonAdvSigs[]{{tf, 5}};
        reject(cpyQ->submit({lists, {}, nonAdvSigs}), gfx::ErrorCode::InvalidArgument, "non-advancing signal (5<=5)", totals);
        gfx::TimelineSignal decSigs[]{{tf, 3}};
        reject(cpyQ->submit({lists, {}, decSigs}), gfx::ErrorCode::InvalidArgument, "decreasing signal (3<=5)", totals);
        gfx::TimelineSignal dupSigs[]{{tf, 7}, {tf, 7}};
        reject(cpyQ->submit({lists, {}, dupSigs}), gfx::ErrorCode::InvalidArgument, "duplicate signal in same SubmitDesc", totals);
        gfx::TimelineSignal maxSigs[]{{tf, std::numeric_limits<std::uint64_t>::max()}};
        reject(cpyQ->submit({lists, {}, maxSigs}), gfx::ErrorCode::InvalidArgument, "UINT64_MAX signal in SubmitDesc", totals);
    }

    // Foreign device TimelineFence rejection (scoped so WARP singleton is clean before device-loss testing!).
    {
        gfx::DeviceDesc otherDesc{};
        otherDesc.backend = backend;
        otherDesc.softwareAdapter = softwareAdapter;
        auto other = take(gfx::createDevice(otherDesc), "create foreign device", totals);
        auto foreignTf = take(other->createTimelineFence(0), "create foreign timeline", totals);
        gfx::CommandList* lists[]{copyCmd.get()};
        gfx::TimelineWait foreignWaits[]{{foreignTf, 0}};
        gfx::TimelineSignal foreignSigs[]{{foreignTf, 1}};
        reject(cpyQ->submit({lists, foreignWaits, {}}), gfx::ErrorCode::WrongDevice, "foreign wait timeline fence", totals);
        reject(cpyQ->submit({lists, {}, foreignSigs}), gfx::ErrorCode::WrongDevice, "foreign signal timeline fence", totals);
    }

    take(gfxQ->waitIdle(), "rej gfx waitIdle", totals);
    take(cmpQ->waitIdle(), "rej cmp waitIdle", totals);
    take(cpyQ->waitIdle(), "rej cpy waitIdle", totals);
}

void runBenchmark(gfx::Device& device, gfx::Backend backend, Totals& totals) {
    auto* gfxQ = take(device.queue(gfx::QueueType::Graphics), "bench queue(Graphics)", totals);
    auto* cmpQ = take(device.queue(gfx::QueueType::Compute), "bench queue(Compute)", totals);
    auto* cpyQ = take(device.queue(gfx::QueueType::Copy), "bench queue(Copy)", totals);

    constexpr std::uint32_t kFrames = 12;
    constexpr std::uint32_t kElements = 1024u * 1024u; // 4 MiB per frame (48 MiB total per run)
    constexpr std::uint64_t kBytes = kElements * sizeof(std::uint32_t);
    constexpr std::uint32_t kComputePasses = 4;
    constexpr std::uint32_t kWarmupRuns = 2;
    constexpr std::uint32_t kMeasuredRuns = 7;

    auto computePipe = makeBufferComputePipeline(device, backend, totals);
    auto cbBuf = take(device.createBuffer({256, gfx::Memory::Upload}), "bench cb", totals);
    std::array<float, 4> params{1.0f, 1.0f, 0.0f, 0.0f};
    take(cbBuf->write(0, std::as_bytes(std::span{params})), "bench cb write", totals);

    std::vector<std::uint32_t> seedData(kElements);
    for (std::uint32_t i = 0; i < kElements; ++i) seedData[i] = i;

    // 1) Sequential single-queue (Graphics queue does upload + compute serially, 1 slot).
    auto runSequential = [&]() -> double {
        auto upload = take(device.createBuffer({kBytes, gfx::Memory::Upload}), "seq upload", totals);
        auto devBuf = take(device.createBuffer({kBytes, gfx::Memory::Device, true}), "seq dev", totals);
        auto readback = take(device.createBuffer({kBytes, gfx::Memory::Readback}), "seq readback", totals);
        take(upload->write(0, std::as_bytes(std::span{seedData})), "seq seed write", totals);
        auto pool = take(device.createCommandPool(gfx::QueueType::Graphics), "seq pool", totals);
        auto cmd = take(pool->createCommandList(), "seq cmd", totals);

        const auto t0 = std::chrono::steady_clock::now();
        for (std::uint32_t f = 0; f < kFrames; ++f) {
            if (f > 0) take(pool->reset(), "seq pool reset", totals);
            take(cmd->begin(), "seq begin", totals);
            take(cmd->transition(devBuf, gfx::ResourceState::CopyDestination), "seq -> CopyDst", totals);
            take(cmd->copyBuffer(upload, devBuf, kBytes), "seq copy", totals);
            take(cmd->transition(cbBuf, gfx::ResourceState::ConstantBuffer), "seq cb", totals);
            take(cmd->transition(devBuf, gfx::ResourceState::UnorderedAccess), "seq -> UAV", totals);
            take(cmd->setComputePipeline(computePipe), "seq setPipe", totals);
            gfx::ResourceBindings b{};
            b.storageBuffers = {{0, 0, devBuf, 0, kBytes}};
            b.constants = {{1, 0, cbBuf, 0, 256}};
            take(cmd->bindComputeResources(b), "seq bind", totals);
            for (std::uint32_t p = 0; p < kComputePasses; ++p) {
                if (p > 0) take(cmd->uavBarrier(devBuf), "seq uavBarrier", totals);
                take(cmd->dispatch(kElements / 64u, 1, 1), "seq dispatch", totals);
            }
            if (f + 1 == kFrames) {
                take(cmd->transition(devBuf, gfx::ResourceState::CopySource), "seq -> CopySrc", totals);
                take(cmd->copyBuffer(devBuf, readback, kBytes), "seq readback copy", totals);
            }
            take(cmd->end(), "seq end", totals);
            auto fence = take(gfxQ->submit(*cmd), "seq submit", totals);
            take(gfxQ->wait(fence, std::chrono::seconds(30)), "seq wait", totals);
        }
        const auto t1 = std::chrono::steady_clock::now();
        const auto sample = take(readback->read(0, 16), "seq verify sample", totals);
        std::uint32_t v0{};
        std::memcpy(&v0, sample.data(), 4);
        check(v0 == kComputePasses, "seq benchmark output value matches", totals);
        return std::chrono::duration<double, std::milli>(t1 - t0).count();
    };

    // 2) Multi-queue overlapped async (Copy uploads frame N+1 while Compute processes frame N, 2 slots).
    auto runOverlapped = [&]() -> double {
        constexpr std::uint32_t kSlots = 2;
        struct BenchSlot {
            gfx::BufferPtr upload;
            gfx::BufferPtr devBuf;
            std::unique_ptr<gfx::CommandPool> copyPool;
            std::unique_ptr<gfx::CommandList> copyCmd;
            std::unique_ptr<gfx::CommandPool> cmpPool;
            std::unique_ptr<gfx::CommandList> cmpCmd;
            std::uint64_t pendingComputeVal{};
        };
        std::array<BenchSlot, kSlots> slots;
        for (std::uint32_t s = 0; s < kSlots; ++s) {
            slots[s].upload = take(device.createBuffer({kBytes, gfx::Memory::Upload}), "ov upload", totals);
            slots[s].devBuf = take(device.createBuffer({kBytes, gfx::Memory::Device, true}), "ov dev", totals);
            take(slots[s].upload->write(0, std::as_bytes(std::span{seedData})), "ov seed write", totals);
            slots[s].copyPool = take(device.createCommandPool(gfx::QueueType::Copy), "ov copyPool", totals);
            slots[s].copyCmd = take(slots[s].copyPool->createCommandList(), "ov copyCmd", totals);
            slots[s].cmpPool = take(device.createCommandPool(gfx::QueueType::Compute), "ov cmpPool", totals);
            slots[s].cmpCmd = take(slots[s].cmpPool->createCommandList(), "ov cmpCmd", totals);
        }
        auto readback = take(device.createBuffer({kBytes, gfx::Memory::Readback}), "ov readback", totals);
        auto copyDownCmd = take(device.createCommandList(gfx::QueueType::Copy), "ov copyDown", totals);
        auto upTf = take(device.createTimelineFence(0), "ov upTf", totals);
        auto cmpTf = take(device.createTimelineFence(0), "ov cmpTf", totals);
        auto downTf = take(device.createTimelineFence(0), "ov downTf", totals);

        const auto t0 = std::chrono::steady_clock::now();
        for (std::uint32_t f = 1; f <= kFrames; ++f) {
            auto& slot = slots[(f - 1u) % kSlots];
            if (slot.pendingComputeVal > 0) {
                take(cmpTf->wait(slot.pendingComputeVal, std::chrono::seconds(30)), "ov wait slot", totals);
                take(slot.copyPool->reset(), "ov reset copyPool", totals);
                take(slot.cmpPool->reset(), "ov reset cmpPool", totals);
            }
            take(slot.copyCmd->begin(), "ov copy begin", totals);
            take(slot.copyCmd->transition(slot.devBuf, gfx::ResourceState::CopyDestination), "ov -> CopyDst", totals);
            take(slot.copyCmd->copyBuffer(slot.upload, slot.devBuf, kBytes), "ov copy", totals);
            take(slot.copyCmd->transition(slot.devBuf, gfx::ResourceState::Common), "ov -> Common", totals);
            take(slot.copyCmd->end(), "ov copy end", totals);
            {
                gfx::CommandList* lists[]{slot.copyCmd.get()};
                gfx::TimelineSignal sigs[]{{upTf, f}};
                take(cpyQ->submit({lists, {}, sigs}), "ov submit Copy", totals);
            }

            take(slot.cmpCmd->begin(), "ov cmp begin", totals);
            take(slot.cmpCmd->transition(cbBuf, gfx::ResourceState::ConstantBuffer), "ov cb", totals);
            take(slot.cmpCmd->transition(slot.devBuf, gfx::ResourceState::UnorderedAccess), "ov -> UAV", totals);
            take(slot.cmpCmd->setComputePipeline(computePipe), "ov setPipe", totals);
            gfx::ResourceBindings b{};
            b.storageBuffers = {{0, 0, slot.devBuf, 0, kBytes}};
            b.constants = {{1, 0, cbBuf, 0, 256}};
            take(slot.cmpCmd->bindComputeResources(b), "ov bind", totals);
            for (std::uint32_t p = 0; p < kComputePasses; ++p) {
                if (p > 0) take(slot.cmpCmd->uavBarrier(slot.devBuf), "ov uavBarrier", totals);
                take(slot.cmpCmd->dispatch(kElements / 64u, 1, 1), "ov dispatch", totals);
            }
            take(slot.cmpCmd->transition(slot.devBuf, gfx::ResourceState::Common), "ov -> Common after CS", totals);
            take(slot.cmpCmd->end(), "ov cmp end", totals);
            {
                gfx::CommandList* lists[]{slot.cmpCmd.get()};
                gfx::TimelineWait waits[]{{upTf, f}};
                gfx::TimelineSignal sigs[]{{cmpTf, f}};
                take(cmpQ->submit({lists, waits, sigs}), "ov submit Compute", totals);
            }
            slot.pendingComputeVal = f;
        }
        auto& lastSlot = slots[(kFrames - 1u) % kSlots];
        take(copyDownCmd->begin(), "ov copyDown begin", totals);
        take(copyDownCmd->transition(lastSlot.devBuf, gfx::ResourceState::CopySource), "ov -> CopySrc", totals);
        take(copyDownCmd->copyBuffer(lastSlot.devBuf, readback, kBytes), "ov readback copy", totals);
        take(copyDownCmd->end(), "ov copyDown end", totals);
        {
            gfx::CommandList* lists[]{copyDownCmd.get()};
            gfx::TimelineWait waits[]{{cmpTf, kFrames}};
            gfx::TimelineSignal sigs[]{{downTf, 1}};
            take(cpyQ->submit({lists, waits, sigs}), "ov submit CopyDown", totals);
        }
        take(downTf->wait(1, std::chrono::seconds(30)), "ov wait final", totals);
        const auto t1 = std::chrono::steady_clock::now();
        const auto sample = take(readback->read(0, 16), "ov verify sample", totals);
        std::uint32_t v0{};
        std::memcpy(&v0, sample.data(), 4);
        check(v0 == kComputePasses, "overlapped benchmark output value matches", totals);
        return std::chrono::duration<double, std::milli>(t1 - t0).count();
    };

    for (std::uint32_t w = 0; w < kWarmupRuns; ++w) {
        (void)runSequential();
        (void)runOverlapped();
    }
    std::vector<double> seqMs, ovMs;
    seqMs.reserve(kMeasuredRuns);
    ovMs.reserve(kMeasuredRuns);
    for (std::uint32_t r = 0; r < kMeasuredRuns; ++r) {
        seqMs.push_back(runSequential());
        ovMs.push_back(runOverlapped());
    }
    std::sort(seqMs.begin(), seqMs.end());
    std::sort(ovMs.begin(), ovMs.end());
    const double seqMed = seqMs[seqMs.size() / 2];
    const double ovMed = ovMs[ovMs.size() / 2];
    const double speedup = seqMed / ovMed;
    std::cout << std::fixed << std::setprecision(3)
              << "BENCHMARK multi_queue frames=" << kFrames
              << " payload_mib_per_frame=" << (kBytes / (1024.0 * 1024.0))
              << " compute_passes=" << kComputePasses
              << " sequential_median_ms=" << seqMed
              << " overlapped_median_ms=" << ovMed
              << " speedup=" << speedup << "x\n";
}

void testWarpDeviceLossAndRecovery(Totals& totals) {
    gfx::DeviceDesc desc{};
    desc.backend = gfx::Backend::D3D12;
    desc.softwareAdapter = true;
    desc.enableDeviceLossTesting = true;
    auto device = take(gfx::createDevice(desc), "create WARP device for loss test", totals);
    auto tf = take(device->createTimelineFence(1), "WARP timeline before loss", totals);
    auto* copyQ = take(device->queue(gfx::QueueType::Copy), "WARP copyQ before loss", totals);

    take(device->removeForTesting(), "WARP removeForTesting", totals);
    reject(tf->completedValue(), gfx::ErrorCode::DeviceLost, "TimelineFence::completedValue after loss", totals);
    reject(tf->signal(2), gfx::ErrorCode::DeviceLost, "TimelineFence::signal after loss", totals);
    reject(tf->wait(1, std::chrono::milliseconds(10)), gfx::ErrorCode::DeviceLost, "TimelineFence::wait after loss", totals);
    reject(device->queue(gfx::QueueType::Compute), gfx::ErrorCode::DeviceLost, "Device::queue(Compute) after loss", totals);
    reject(copyQ->waitIdle(), gfx::ErrorCode::DeviceLost, "CopyQueue::waitIdle after loss", totals);

    auto recovered = take(device->recover(), "WARP recover", totals);
    testCopyComputeCopyPipeline(*recovered, gfx::Backend::D3D12, totals);
    testFourStageCopyComputeGraphicsCopyPipeline(*recovered, gfx::Backend::D3D12, totals);
}

void verifyEmptyStatistics(gfx::Device& device, Totals& totals) {
    take(take(device.queue(gfx::QueueType::Graphics), "drain gfx", totals)->waitIdle(), "gfx idle", totals);
    take(take(device.queue(gfx::QueueType::Compute), "drain cmp", totals)->waitIdle(), "cmp idle", totals);
    take(take(device.queue(gfx::QueueType::Copy), "drain cpy", totals)->waitIdle(), "cpy idle", totals);
    const auto stats = take(device.resourceStatistics(), "resourceStatistics after cleanup", totals);
    check(stats.trackedObjects == 0, "trackedObjects must return to 0", totals);
    check(stats.uniqueResources == 0, "uniqueResources must return to 0", totals);
    check(stats.pendingSubmissions == 0, "pendingSubmissions must return to 0", totals);
    check(stats.pendingObjectReferences == 0, "pendingObjectReferences must return to 0", totals);
    check(stats.resourceAllocationBytes == 0, "resourceAllocationBytes must return to 0", totals);
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
        else if (arg == "--warp") warp = true;
        else if (arg == "--benchmark") benchmark = true;
        else {
            std::cerr << "Unknown argument: " << arg << "\n";
            return 2;
        }
    }

    Totals totals{};
    try {
        gfx::DeviceDesc desc{};
        desc.backend = backend;
        desc.validation = validation;
        desc.softwareAdapter = warp;
        auto created = gfx::createDevice(desc);
        if (!created) {
            if (created.error().code == gfx::ErrorCode::Unsupported) {
                std::cout << "SKIP multi_queue: " << created.error().message << "\n";
                return 77;
            }
            throw std::runtime_error("createDevice failed: " + created.error().message);
        }
        auto device = std::move(*created);
        const auto& caps = device->caps();
        check(caps.timelineFences, "adapter must expose caps.timelineFences", totals);

        std::cout << "MULTI_QUEUE adapter=\"" << caps.adapterName
                  << "\" backend=" << (backend == gfx::Backend::D3D12 ? "D3D12" : "Vulkan")
                  << " dedicatedCompute=" << (caps.dedicatedComputeQueue ? 1 : 0)
                  << " dedicatedCopy=" << (caps.dedicatedCopyQueue ? 1 : 0)
                  << " timelineFences=" << (caps.timelineFences ? 1 : 0)
                  << " validation=" << (validation ? 1 : 0)
                  << " warp=" << (warp ? 1 : 0) << "\n";

        testTimelineCpuContract(*device, totals);
        testCopyComputeCopyPipeline(*device, backend, totals);
        testFourStageCopyComputeGraphicsCopyPipeline(*device, backend, totals);
        testOverlappedFramesInFlight(*device, backend, totals);
        testRejections(*device, backend, warp, totals);

        if (benchmark) {
            runBenchmark(*device, backend, totals);
        }

        verifyEmptyStatistics(*device, totals);
        if (validation) {
            const auto errs = take(device->validationErrorCount(), "validationErrorCount", totals);
            if (errs != 0) {
                throw std::runtime_error("Validation errors observed: " + std::to_string(errs));
            }
        }
        device.reset();

        if (backend == gfx::Backend::D3D12 && warp && !validation) {
            testWarpDeviceLossAndRecovery(totals);
        }

        std::cout << "PASS multi_queue assertions=" << totals.assertions
                  << " pipelines=" << totals.pipelines
                  << " submits=" << totals.submits
                  << " rejections=" << totals.rejections << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL multi_queue: " << e.what() << "\n";
        return 1;
    }
}
