#include <graphics/Graphics.hpp>
#include "fixtures/ComputeFixtures.hpp"
#include "fixtures/VertexFixtures.hpp"

#include <algorithm>
#include <array>
#include <chrono>
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
    gfx::ShaderBinary bin{stage, std::vector<std::byte>(words.size() * 4u), gfx::ShaderFormat::SPIRV};
    std::memcpy(bin.bytes.data(), words.data(), bin.bytes.size());
    return bin;
}

gfx::PipelinePtr makeScaleAddComputePipeline(gfx::Device& device, gfx::Backend backend, Totals& totals) {
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
        cs = take(gfx::compileHlsl(hlsl, "CS", gfx::ShaderStage::Compute), "compile HLSL scale_add CS", totals);
    } else {
        cs = spirvBinary(gfx::ShaderStage::Compute, computeFixtures::buffer_scale_add);
    }
    gfx::ComputePipelineDesc desc{};
    desc.compute = std::move(cs);
    desc.threadgroupSizeX = 64u;
    desc.bindingLayout.entries = {
        {gfx::BindingType::StorageBuffer, 0u, 1u, 0u, gfx::ShaderVisibility::Compute},
        {gfx::BindingType::ConstantBuffer, 0u, 1u, 0u, gfx::ShaderVisibility::Compute}
    };
    return take(device.createComputePipeline(desc), "create scale_add compute pipeline", totals);
}

struct QuadVertex {
    float x{}, y{};
    float r{}, g{}, b{}, a{};
};
static_assert(sizeof(QuadVertex) == 24);

gfx::PipelinePtr makeColoredQuadPipeline(gfx::Device& device, gfx::Backend backend, Totals& totals) {
    gfx::ShaderBinary vs{}, ps{};
    if (backend == gfx::Backend::D3D12) {
        constexpr std::string_view hlsl = R"(
struct VSIn { float2 pos : POSITION0; float4 col : COLOR0; };
struct VSOut { float4 pos : SV_Position; float4 col : COLOR0; };
VSOut VS(VSIn i) {
    VSOut o;
    o.pos = float4(i.pos, 0.5, 1.0);
    o.col = i.col;
    return o;
}
float4 PS(VSOut i) : SV_Target0 { return i.col; }
)";
        vs = take(gfx::compileHlsl(hlsl, "VS", gfx::ShaderStage::Vertex), "compile HLSL quad VS", totals);
        ps = take(gfx::compileHlsl(hlsl, "PS", gfx::ShaderStage::Fragment), "compile HLSL quad PS", totals);
    } else {
        vs = spirvBinary(gfx::ShaderStage::Vertex, vertexFixtures::position2);
        ps = spirvBinary(gfx::ShaderStage::Fragment, vertexFixtures::color);
    }
    gfx::GraphicsPipelineDesc desc{vs, ps};
    desc.colorFormat = gfx::Format::RGBA8UNorm;
    desc.vertexLayout.buffers = {{0u, sizeof(QuadVertex), gfx::VertexInputRate::Vertex}};
    desc.vertexLayout.attributes = {
        {"POSITION", 0u, gfx::VertexFormat::Float2, 0u, 0u},
        {"COLOR", 0u, gfx::VertexFormat::Float4, 0u, 8u}
    };
    return take(device.createGraphicsPipeline(desc), "create colored quad graphics pipeline", totals);
}

void appendQuad(std::vector<QuadVertex>& verts, std::vector<std::uint16_t>& indices,
                float x0, float y0, float x1, float y1,
                float r, float g, float b) {
    const auto base = static_cast<std::uint16_t>(verts.size());
    verts.push_back({x0, y0, r, g, b, 1.f});
    verts.push_back({x1, y0, r, g, b, 1.f});
    verts.push_back({x1, y1, r, g, b, 1.f});
    verts.push_back({x0, y1, r, g, b, 1.f});
    indices.push_back(base + 0u);
    indices.push_back(base + 1u);
    indices.push_back(base + 2u);
    indices.push_back(base + 0u);
    indices.push_back(base + 2u);
    indices.push_back(base + 3u);
}

void writeConstants(const gfx::BufferPtr& cb, float x, float y, Totals& totals) {
    std::array<float, 64> data{};
    data[0] = x;
    data[1] = y;
    take(cb->write(0u, std::as_bytes(std::span{data})), "write compute constants", totals);
}

void runAdapterSuite(gfx::Backend backend, const gfx::AdapterInfo& adapter, bool validation, Totals& totals) {
    gfx::DeviceDesc desc{};
    desc.backend = backend;
    desc.adapter = adapter.id;
    desc.softwareAdapter = adapter.softwareAdapter;
    desc.validation = validation;

    auto device = take(gfx::createDevice(desc), "createDevice", totals);
    const auto& caps = device->caps();

    // 1. Capability contracts
    check(caps.indirectDraw, "caps.indirectDraw must be true", totals);
    check(caps.maxDrawIndirectCount >= 1u, "caps.maxDrawIndirectCount >= 1", totals);
    if (caps.multiDrawIndirect) {
        check(caps.maxDrawIndirectCount >= 4u, "MDI adapter must support >= 4 indirect draws", totals);
    } else {
        check(caps.maxDrawIndirectCount == 1u, "non-MDI adapter maxDrawIndirectCount == 1", totals);
    }
    if (caps.descriptorIndexing) {
        check(caps.maxBindlessDescriptors > 0u, "descriptorIndexing implies maxBindlessDescriptors > 0", totals);
    } else {
        check(caps.maxBindlessDescriptors == 0u, "no descriptorIndexing implies maxBindlessDescriptors == 0", totals);
    }

    take(device->setProfilingEnabled(true), "enable profiling", totals);

    auto computePipe = makeScaleAddComputePipeline(*device, backend, totals);
    auto graphicsPipe = makeColoredQuadPipeline(*device, backend, totals);

    // 2. GPU-Generated DispatchIndirect on Compute Queue (192 elements = 3 threadgroups of 64)
    {
        auto dispatchArgsUp = take(device->createBuffer({256u, gfx::Memory::Upload}), "dispatchArgs upload", totals);
        auto dispatchArgsDev = take(device->createBuffer({256u, gfx::Memory::Device, true}), "dispatchArgs device storage", totals);
        std::array<std::uint32_t, 64> initArgs{};
        initArgs[0] = 1u; // After scale=2, add=1 => groupCountX = 3
        initArgs[1] = 0u; // After scale=2, add=1 => groupCountY = 1
        initArgs[2] = 0u; // After scale=2, add=1 => groupCountZ = 1
        take(dispatchArgsUp->write(0u, std::as_bytes(std::span{initArgs})), "write init dispatch args", totals);

        constexpr std::uint32_t kCount = 192u; // 3 * 64 threads
        constexpr std::uint64_t kDataBytes = kCount * sizeof(std::uint32_t);
        auto dataUp = take(device->createBuffer({kDataBytes, gfx::Memory::Upload}), "data upload", totals);
        auto dataDev = take(device->createBuffer({kDataBytes, gfx::Memory::Device, true}), "data device storage", totals);
        auto dataRb = take(device->createBuffer({kDataBytes, gfx::Memory::Readback}), "data readback", totals);
        std::vector<std::uint32_t> initData(kCount);
        for (std::uint32_t i = 0; i < kCount; ++i) initData[i] = i + 1u;
        take(dataUp->write(0u, std::as_bytes(std::span{initData})), "write init data", totals);

        auto cbGenArgs = take(device->createBuffer({256u, gfx::Memory::Upload}), "cbGenArgs", totals);
        auto cbWork = take(device->createBuffer({256u, gfx::Memory::Upload}), "cbWork", totals);
        writeConstants(cbGenArgs, 2.f, 1.f, totals);
        writeConstants(cbWork, 3.f, 7.f, totals);

        auto* computeQ = take(device->queue(gfx::QueueType::Compute), "get Compute queue", totals);
        auto cmd = take(device->createCommandList(gfx::QueueType::Compute), "create Compute cmd", totals);
        take(cmd->begin(), "compute cmd begin", totals);
        take(cmd->transition(dispatchArgsDev, gfx::ResourceState::CopyDestination), "dispatchArgs -> CopyDst", totals);
        take(cmd->copyBuffer(dispatchArgsUp, dispatchArgsDev, 256u), "copy initArgs -> dispatchArgsDev", totals);
        take(cmd->transition(dataDev, gfx::ResourceState::CopyDestination), "dataDev -> CopyDst", totals);
        take(cmd->copyBuffer(dataUp, dataDev, kDataBytes), "copy initData -> dataDev", totals);

        // Pass 1: Compute generates DispatchIndirectCommand {3, 1, 1} on GPU
        take(cmd->transition(dispatchArgsDev, gfx::ResourceState::UnorderedAccess), "dispatchArgs -> UAV", totals);
        take(cmd->transition(cbGenArgs, gfx::ResourceState::ConstantBuffer), "cbGenArgs -> CBV", totals);
        take(cmd->setComputePipeline(computePipe), "set computePipe 1", totals);
        gfx::ResourceBindings b1{};
        b1.storageBuffers = {{0u, 0u, dispatchArgsDev, 0u, 256u}};
        b1.constants = {{1u, 0u, cbGenArgs, 0u, 256u}};
        take(cmd->bindComputeResources(b1), "bind pass 1", totals);
        take(cmd->dispatch(1u, 1u, 1u), "dispatch pass 1 (generate indirect dispatch args)", totals);

        // Pass 2: Execute GPU-generated DispatchIndirect (3x1x1 groups = 192 threads)
        take(cmd->transition(dispatchArgsDev, gfx::ResourceState::IndirectArgument), "dispatchArgs -> IndirectArgument", totals);
        take(cmd->transition(dataDev, gfx::ResourceState::UnorderedAccess), "dataDev -> UAV", totals);
        take(cmd->transition(cbWork, gfx::ResourceState::ConstantBuffer), "cbWork -> CBV", totals);
        gfx::ResourceBindings b2{};
        b2.storageBuffers = {{0u, 0u, dataDev, 0u, kDataBytes}};
        b2.constants = {{1u, 0u, cbWork, 0u, 256u}};
        take(cmd->bindComputeResources(b2), "bind pass 2", totals);
        take(cmd->dispatchIndirect(dispatchArgsDev, 0u), "dispatchIndirect (3 groups)", totals);

        take(cmd->transition(dataDev, gfx::ResourceState::CopySource), "dataDev -> CopySrc", totals);
        take(cmd->copyBuffer(dataDev, dataRb, kDataBytes), "copy dataDev -> dataRb", totals);
        take(cmd->end(), "compute cmd end", totals);

        const auto fence = take(computeQ->submit(*cmd), "submit dispatchIndirect", totals);
        take(computeQ->wait(fence, std::chrono::seconds(10)), "wait dispatchIndirect", totals);

        const auto raw = take(dataRb->read(0u, kDataBytes), "readback dispatchIndirect data", totals);
        for (std::uint32_t i = 0; i < kCount; ++i) {
            std::uint32_t got{};
            std::memcpy(&got, raw.data() + static_cast<std::size_t>(i) * 4u, 4u);
            const std::uint32_t expected = (i + 1u) * 3u + 7u;
            if (got != expected) {
                throw std::runtime_error("dispatchIndirect mismatch at index " + std::to_string(i) +
                    ": got=" + std::to_string(got) + ", expected=" + std::to_string(expected));
            }
        }
        ++totals.assertions;
    }

    // 3. Async Compute GPU Culling -> GPU-to-GPU TimelineFence -> Graphics Multi-Draw Indexed Indirect
    // 4 quadrant quads: Q0(TL red, kept), Q1(TR green, GPU-culled), Q2(BL blue, kept), Q3(BR yellow, GPU-culled)
    {
        std::vector<QuadVertex> verts;
        std::vector<std::uint16_t> indices;
        // Top-left (NDC x in [-1, 0], y in [0, 1]) -> Red
        appendQuad(verts, indices, -1.f, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f);
        // Top-right (NDC x in [0, 1], y in [0, 1]) -> Green (will be culled by Async Compute)
        appendQuad(verts, indices, 0.f, 0.f, 1.f, 1.f, 0.f, 1.f, 0.f);
        // Bottom-left (NDC x in [-1, 0], y in [-1, 0]) -> Blue
        appendQuad(verts, indices, -1.f, -1.f, 0.f, 0.f, 0.f, 0.f, 1.f);
        // Bottom-right (NDC x in [0, 1], y in [-1, 0]) -> Yellow (will be culled by Async Compute)
        appendQuad(verts, indices, 0.f, -1.f, 1.f, 0.f, 1.f, 1.f, 0.f);

        const std::uint64_t vbBytes = verts.size() * sizeof(QuadVertex);
        const std::uint64_t ibBytes = indices.size() * sizeof(std::uint16_t);
        auto vbUp = take(device->createBuffer({vbBytes, gfx::Memory::Upload}), "vb upload", totals);
        auto ibUp = take(device->createBuffer({ibBytes, gfx::Memory::Upload}), "ib upload", totals);
        take(vbUp->write(0u, std::as_bytes(std::span{verts})), "write vb", totals);
        take(ibUp->write(0u, std::as_bytes(std::span{indices})), "write ib", totals);

        constexpr std::uint32_t kSlotStride = 256u; // 256B aligned for StorageBuffer sub-bindings & MDI stride
        constexpr std::uint64_t kMdiBytes = kSlotStride * 4u;
        std::vector<std::byte> mdiInit(kMdiBytes, std::byte{0});
        for (std::uint32_t q = 0; q < 4u; ++q) {
            const gfx::DrawIndexedIndirectCommand cmdArg{6u, 1u, q * 6u, 0, 0u};
            std::memcpy(mdiInit.data() + static_cast<std::size_t>(q) * kSlotStride, &cmdArg, sizeof(cmdArg));
        }
        auto mdiUp = take(device->createBuffer({kMdiBytes, gfx::Memory::Upload}), "mdi upload", totals);
        auto mdiDev = take(device->createBuffer({kMdiBytes, gfx::Memory::Device, true}), "mdi device storage", totals);
        take(mdiUp->write(0u, mdiInit), "write mdi init", totals);

        auto cbCullZero = take(device->createBuffer({256u, gfx::Memory::Upload}), "cbCullZero", totals);
        writeConstants(cbCullZero, 0.f, 0.f, totals);

        auto rt = take(device->createTexture({64u, 64u, gfx::Format::RGBA8UNorm, true}), "mdi rt", totals);
        const auto fp = rt->footprint();
        auto rb = take(device->createBuffer({fp.size, gfx::Memory::Readback}), "mdi readback", totals);

        // Step A: Async Compute Queue uploads MDI args and GPU-culls slots 1 (TR) & 3 (BR) to zero
        auto* computeQ = take(device->queue(gfx::QueueType::Compute), "Async Compute queue", totals);
        auto cullCmd = take(device->createCommandList(gfx::QueueType::Compute), "cull cmd", totals);
        take(cullCmd->begin(), "cull begin", totals);
        take(cullCmd->transition(mdiDev, gfx::ResourceState::CopyDestination), "mdiDev -> CopyDst", totals);
        take(cullCmd->copyBuffer(mdiUp, mdiDev, kMdiBytes), "copy mdiUp -> mdiDev", totals);
        take(cullCmd->transition(mdiDev, gfx::ResourceState::UnorderedAccess), "mdiDev -> UAV", totals);
        take(cullCmd->transition(cbCullZero, gfx::ResourceState::ConstantBuffer), "cbCullZero -> CBV", totals);
        take(cullCmd->setComputePipeline(computePipe), "set cull computePipe", totals);

        // Cull slot 1 (offset 256)
        gfx::ResourceBindings cullSlot1{};
        cullSlot1.storageBuffers = {{0u, 0u, mdiDev, kSlotStride * 1u, kSlotStride}};
        cullSlot1.constants = {{1u, 0u, cbCullZero, 0u, 256u}};
        take(cullCmd->bindComputeResources(cullSlot1), "bind cull slot 1", totals);
        take(cullCmd->dispatch(1u, 1u, 1u), "dispatch cull slot 1", totals);
        take(cullCmd->uavBarrier(mdiDev), "uavBarrier after cull slot 1", totals);

        // Cull slot 3 (offset 768)
        gfx::ResourceBindings cullSlot3{};
        cullSlot3.storageBuffers = {{0u, 0u, mdiDev, kSlotStride * 3u, kSlotStride}};
        cullSlot3.constants = {{1u, 0u, cbCullZero, 0u, 256u}};
        take(cullCmd->bindComputeResources(cullSlot3), "bind cull slot 3", totals);
        take(cullCmd->dispatch(1u, 1u, 1u), "dispatch cull slot 3", totals);
        take(cullCmd->transition(mdiDev, gfx::ResourceState::Common), "mdiDev -> Common for queue handoff", totals);
        take(cullCmd->end(), "cull end", totals);

        // Submit async compute culling first (without waiting on CPU when timeline fences are available)
        // so graphics recording observes the updated resource version and chains via GPU TimelineFence.
        gfx::TimelineFencePtr timeline;
        if (caps.timelineFences) {
            timeline = take(device->createTimelineFence(0u), "create GPU handoff timeline", totals);
            gfx::CommandList* cullList = cullCmd.get();
            gfx::SubmitDesc cullSub{};
            cullSub.commandLists = {&cullList, 1};
            gfx::TimelineSignal sigOp{timeline, 1u};
            cullSub.signals = {&sigOp, 1};
            (void)take(computeQ->submit(cullSub), "submit async compute culling", totals);
        } else {
            const auto f1 = take(computeQ->submit(*cullCmd), "submit compute cull", totals);
            take(computeQ->wait(f1, std::chrono::seconds(10)), "wait compute cull", totals);
        }

        // Step B: Graphics Queue records after producer submit and waits on GPU TimelineFence to execute MDI
        auto gfxCmd = take(device->createCommandList(gfx::QueueType::Graphics), "gfx MDI cmd", totals);
        take(gfxCmd->begin(), "gfx MDI begin", totals);
        take(gfxCmd->transition(vbUp, gfx::ResourceState::VertexBuffer), "vb -> VertexBuffer", totals);
        take(gfxCmd->transition(ibUp, gfx::ResourceState::IndexBuffer), "ib -> IndexBuffer", totals);
        take(gfxCmd->transition(mdiDev, gfx::ResourceState::IndirectArgument), "mdiDev -> IndirectArgument", totals);
        take(gfxCmd->transition(rt, gfx::ResourceState::RenderTarget), "rt -> RT", totals);
        take(gfxCmd->beginRenderPass(rt, gfx::Color{16.f / 255.f, 16.f / 255.f, 16.f / 255.f, 1.f}), "begin MDI pass", totals);
        take(gfxCmd->setPipeline(graphicsPipe), "set graphicsPipe", totals);
        take(gfxCmd->bindVertexBuffer(0u, vbUp, sizeof(QuadVertex)), "bind vb", totals);
        take(gfxCmd->bindIndexBuffer(ibUp, gfx::IndexFormat::UInt16), "bind ib", totals);
        if (caps.multiDrawIndirect) {
            take(gfxCmd->drawIndexedIndirect(mdiDev, 0u, 4u, kSlotStride), "drawIndexedIndirect MDI (4 draws)", totals);
        } else {
            reject(gfxCmd->drawIndexedIndirect(mdiDev, 0u, 4u, kSlotStride), gfx::ErrorCode::Unsupported, "MDI rejected when unsupported", totals);
            for (std::uint32_t q = 0; q < 4u; ++q) {
                take(gfxCmd->drawIndexedIndirect(mdiDev, static_cast<std::uint64_t>(q) * kSlotStride, 1u, kSlotStride), "single indirect fallback", totals);
            }
        }
        take(gfxCmd->endRenderPass(), "end MDI pass", totals);
        take(gfxCmd->transition(rt, gfx::ResourceState::CopySource), "rt -> CopySrc", totals);
        take(gfxCmd->copyTextureToBuffer(rt, rb), "copy rt -> rb", totals);
        take(gfxCmd->end(), "gfx MDI end", totals);

        if (timeline) {
            gfx::CommandList* gfxList = gfxCmd.get();
            gfx::SubmitDesc gfxSub{};
            gfxSub.commandLists = {&gfxList, 1};
            gfx::TimelineWait waitOp{timeline, 1u};
            gfx::TimelineSignal doneOp{timeline, 2u};
            gfxSub.waits = {&waitOp, 1};
            gfxSub.signals = {&doneOp, 1};
            (void)take(device->queue().submit(gfxSub), "submit graphics MDI waiting on async compute", totals);
            take(timeline->wait(2u, std::chrono::seconds(10)), "wait timeline 2", totals);
        } else {
            const auto f2 = take(device->queue().submit(*gfxCmd), "submit gfx MDI", totals);
            take(device->queue().wait(f2, std::chrono::seconds(10)), "wait gfx MDI", totals);
        }

        const auto pixels = take(rb->read(0u, fp.size), "readback MDI image", totals);
        auto expectPixel = [&](std::uint32_t x, std::uint32_t y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
            const std::size_t off = static_cast<std::size_t>(y) * fp.rowPitch + static_cast<std::size_t>(x) * 4u;
            const auto pr = static_cast<std::uint8_t>(pixels[off + 0]);
            const auto pg = static_cast<std::uint8_t>(pixels[off + 1]);
            const auto pb = static_cast<std::uint8_t>(pixels[off + 2]);
            if (std::abs(int(pr) - int(r)) > 1 || std::abs(int(pg) - int(g)) > 1 || std::abs(int(pb) - int(b)) > 1) {
                throw std::runtime_error("MDI pixel mismatch at (" + std::to_string(x) + "," + std::to_string(y) +
                    "): got (" + std::to_string(pr) + "," + std::to_string(pg) + "," + std::to_string(pb) + ")");
            }
        };
        // Check interior of all 4 quadrants across the entire 64x64 image
        for (std::uint32_t y = 2u; y < 30u; ++y) {
            for (std::uint32_t x = 2u; x < 30u; ++x) expectPixel(x, y, 255u, 0u, 0u);     // TL: kept (red)
            for (std::uint32_t x = 34u; x < 62u; ++x) expectPixel(x, y, 16u, 16u, 16u);  // TR: GPU-culled (clear bg)
        }
        for (std::uint32_t y = 34u; y < 62u; ++y) {
            for (std::uint32_t x = 2u; x < 30u; ++x) expectPixel(x, y, 0u, 0u, 255u);    // BL: kept (blue)
            for (std::uint32_t x = 34u; x < 62u; ++x) expectPixel(x, y, 16u, 16u, 16u);  // BR: GPU-culled (clear bg)
        }
        ++totals.assertions;
    }

    // 4. Non-indexed drawIndirect + MDI directly from Upload buffer in IndirectArgument state
    {
        std::vector<QuadVertex> verts{
            // Left half: green (6 non-indexed vertices)
            {-1.f, -1.f, 0.f, 1.f, 0.f, 1.f}, {0.f, -1.f, 0.f, 1.f, 0.f, 1.f}, {0.f, 1.f, 0.f, 1.f, 0.f, 1.f},
            {-1.f, -1.f, 0.f, 1.f, 0.f, 1.f}, {0.f,  1.f, 0.f, 1.f, 0.f, 1.f}, {-1.f, 1.f, 0.f, 1.f, 0.f, 1.f},
            // Right half: magenta (6 non-indexed vertices)
            {0.f, -1.f, 1.f, 0.f, 1.f, 1.f}, {1.f, -1.f, 1.f, 0.f, 1.f, 1.f}, {1.f, 1.f, 1.f, 0.f, 1.f, 1.f},
            {0.f, -1.f, 1.f, 0.f, 1.f, 1.f}, {1.f,  1.f, 1.f, 0.f, 1.f, 1.f}, {0.f, 1.f, 1.f, 0.f, 1.f, 1.f}
        };
        auto vb = take(device->createBuffer({verts.size() * sizeof(QuadVertex), gfx::Memory::Upload}), "non-indexed vb", totals);
        take(vb->write(0u, std::as_bytes(std::span{verts})), "write non-indexed vb", totals);

        const std::array<gfx::DrawIndirectCommand, 2> drawArgs{{
            {6u, 1u, 0u, 0u},
            {6u, 1u, 6u, 0u}
        }};
        auto argsUp = take(device->createBuffer({sizeof(drawArgs), gfx::Memory::Upload}), "non-indexed argsUp", totals);
        take(argsUp->write(0u, std::as_bytes(std::span{drawArgs})), "write drawArgs", totals);

        auto rt = take(device->createTexture({32u, 32u, gfx::Format::RGBA8UNorm, true}), "non-indexed rt", totals);
        const auto fp = rt->footprint();
        auto rb = take(device->createBuffer({fp.size, gfx::Memory::Readback}), "non-indexed rb", totals);

        auto cmd = take(device->createCommandList(), "non-indexed indirect cmd", totals);
        take(cmd->begin(), "begin non-indexed", totals);
        take(cmd->transition(vb, gfx::ResourceState::VertexBuffer), "vb -> VB", totals);
        take(cmd->transition(argsUp, gfx::ResourceState::IndirectArgument), "argsUp -> IndirectArgument", totals);
        take(cmd->transition(rt, gfx::ResourceState::RenderTarget), "rt -> RT", totals);
        take(cmd->beginRenderPass(rt, gfx::Color{0.f, 0.f, 0.f, 1.f}), "begin non-indexed pass", totals);
        take(cmd->setPipeline(graphicsPipe), "set pipe", totals);
        take(cmd->bindVertexBuffer(0u, vb, sizeof(QuadVertex)), "bind vb", totals);
        if (caps.multiDrawIndirect) {
            take(cmd->drawIndirect(argsUp, 0u, 2u, sizeof(gfx::DrawIndirectCommand)), "drawIndirect MDI (2 draws)", totals);
        } else {
            take(cmd->drawIndirect(argsUp, 0u, 1u, sizeof(gfx::DrawIndirectCommand)), "drawIndirect 0", totals);
            take(cmd->drawIndirect(argsUp, sizeof(gfx::DrawIndirectCommand), 1u, sizeof(gfx::DrawIndirectCommand)), "drawIndirect 1", totals);
        }
        take(cmd->endRenderPass(), "end non-indexed pass", totals);
        take(cmd->transition(rt, gfx::ResourceState::CopySource), "rt -> CopySrc", totals);
        take(cmd->copyTextureToBuffer(rt, rb), "copy rt -> rb", totals);
        take(cmd->end(), "end non-indexed", totals);

        const auto fence = take(device->queue().submit(*cmd), "submit non-indexed indirect", totals);
        take(device->queue().wait(fence, std::chrono::seconds(10)), "wait non-indexed indirect", totals);

        const auto pixels = take(rb->read(0u, fp.size), "read non-indexed pixels", totals);
        for (std::uint32_t y = 2u; y < 30u; ++y) {
            for (std::uint32_t x = 2u; x < 14u; ++x) {
                const std::size_t off = static_cast<std::size_t>(y) * fp.rowPitch + static_cast<std::size_t>(x) * 4u;
                check(static_cast<std::uint8_t>(pixels[off + 0]) == 0u &&
                      static_cast<std::uint8_t>(pixels[off + 1]) == 255u &&
                      static_cast<std::uint8_t>(pixels[off + 2]) == 0u, "left half green", totals);
            }
            for (std::uint32_t x = 18u; x < 30u; ++x) {
                const std::size_t off = static_cast<std::size_t>(y) * fp.rowPitch + static_cast<std::size_t>(x) * 4u;
                check(static_cast<std::uint8_t>(pixels[off + 0]) == 255u &&
                      static_cast<std::uint8_t>(pixels[off + 1]) == 0u &&
                      static_cast<std::uint8_t>(pixels[off + 2]) == 255u, "right half magenta", totals);
            }
        }
    }

    // 5. Profiling counters recorded indirect draw & dispatch calls
    {
        const auto snap = take(device->profilingSnapshot(), "profilingSnapshot", totals);
        check(snap.counters.drawCalls >= 2u, "profiling tracked indirect draw calls", totals);
        check(snap.counters.indexedDrawCalls >= 1u, "profiling tracked indexed indirect draw calls", totals);
        check(snap.counters.dispatchCalls >= 4u, "profiling tracked direct + indirect dispatches", totals);
    }

    // 6. Mesh/Task Shader & Rejection Contracts
    {
        reject(gfx::compileHlsl("void main(){}", "main", gfx::ShaderStage::Mesh), gfx::ErrorCode::Unsupported, "SM5.1 Mesh shader unsupported", totals);
        reject(gfx::compileHlsl("void main(){}", "main", gfx::ShaderStage::Task), gfx::ErrorCode::Unsupported, "SM5.1 Task shader unsupported", totals);

        gfx::MeshPipelineDesc badMesh{};
        reject(device->createMeshPipeline(badMesh), gfx::ErrorCode::InvalidArgument, "empty MeshPipelineDesc rejected", totals);

        gfx::MeshPipelineDesc validMesh{};
        validMesh.mesh = {gfx::ShaderStage::Mesh, {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}}, gfx::ShaderFormat::DXIL};
        validMesh.task = gfx::ShaderBinary{gfx::ShaderStage::Task, {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}}, gfx::ShaderFormat::DXIL};
        validMesh.fragment = {gfx::ShaderStage::Fragment, {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}}, gfx::ShaderFormat::DXIL};
        reject(device->createMeshPipeline(validMesh), gfx::ErrorCode::Unsupported, "MeshPipelineDesc explicit Unsupported", totals);

        auto devBuf = take(device->createBuffer({64u, gfx::Memory::Device}), "reject devBuf", totals);
        auto rbBuf = take(device->createBuffer({64u, gfx::Memory::Readback}), "reject rbBuf", totals);
        auto rt = take(device->createTexture({16u, 16u, gfx::Format::RGBA8UNorm, true}), "reject rt", totals);

        auto copyCmd = take(device->createCommandList(gfx::QueueType::Copy), "reject copyCmd", totals);
        take(copyCmd->begin(), "copyCmd begin", totals);
        reject(copyCmd->transition(devBuf, gfx::ResourceState::IndirectArgument), gfx::ErrorCode::InvalidArgument, "Copy queue rejects IndirectArgument transition", totals);
        take(copyCmd->end(), "copyCmd end", totals);

        auto cmd = take(device->createCommandList(), "reject gfx cmd", totals);
        take(cmd->begin(), "reject cmd begin", totals);
        reject(cmd->transition(rbBuf, gfx::ResourceState::IndirectArgument), gfx::ErrorCode::InvalidArgument, "Readback buffer rejects IndirectArgument", totals);
        reject(cmd->transition(rt, gfx::ResourceState::IndirectArgument), gfx::ErrorCode::InvalidArgument, "Texture rejects IndirectArgument", totals);

        // dispatchIndirect rejections
        take(cmd->setComputePipeline(computePipe), "set computePipe for reject", totals);
        auto cb = take(device->createBuffer({256u, gfx::Memory::Upload}), "reject cb", totals);
        auto st = take(device->createBuffer({256u, gfx::Memory::Device, true}), "reject st", totals);
        take(cmd->transition(st, gfx::ResourceState::UnorderedAccess), "st -> UAV", totals);
        take(cmd->transition(cb, gfx::ResourceState::ConstantBuffer), "cb -> CBV", totals);
        gfx::ResourceBindings cbBind{};
        cbBind.storageBuffers = {{0u, 0u, st, 0u, 256u}};
        cbBind.constants = {{1u, 0u, cb, 0u, 256u}};
        take(cmd->bindComputeResources(cbBind), "bind compute for reject", totals);
        reject(cmd->dispatchIndirect(devBuf, 0u), gfx::ErrorCode::InvalidState, "dispatchIndirect wrong state (Common)", totals);
        take(cmd->transition(devBuf, gfx::ResourceState::IndirectArgument), "devBuf -> IndirectArgument", totals);
        reject(cmd->dispatchIndirect(devBuf, 2u), gfx::ErrorCode::InvalidArgument, "dispatchIndirect unaligned offset", totals);
        reject(cmd->dispatchIndirect(devBuf, 60u), gfx::ErrorCode::InvalidArgument, "dispatchIndirect out of bounds", totals);
        reject(cmd->dispatchIndirect(rbBuf, 0u), gfx::ErrorCode::InvalidArgument, "dispatchIndirect Readback buffer", totals);
        reject(cmd->drawIndirect(devBuf, 0u, 1u, 16u), gfx::ErrorCode::InvalidState, "drawIndirect outside render pass", totals);

        // drawIndirect / drawIndexedIndirect rejections inside pass
        take(cmd->transition(rt, gfx::ResourceState::RenderTarget), "rt -> RT", totals);
        take(cmd->beginRenderPass(rt, gfx::Color{0, 0, 0, 1}), "begin pass for reject", totals);
        take(cmd->setPipeline(graphicsPipe), "set gfx pipe for reject", totals);
        reject(cmd->dispatchIndirect(devBuf, 0u), gfx::ErrorCode::InvalidState, "dispatchIndirect inside render pass", totals);
        reject(cmd->drawIndirect(devBuf, 0u, 0u, 16u), gfx::ErrorCode::InvalidArgument, "drawIndirect count=0", totals);
        reject(cmd->drawIndirect(devBuf, 0u, caps.maxDrawIndirectCount + 1u, 16u), gfx::ErrorCode::InvalidArgument, "drawIndirect count > max", totals);
        reject(cmd->drawIndirect(devBuf, 0u, 1u, 12u), gfx::ErrorCode::InvalidArgument, "drawIndirect stride < 16", totals);
        reject(cmd->drawIndirect(devBuf, 0u, 1u, 18u), gfx::ErrorCode::InvalidArgument, "drawIndirect unaligned stride", totals);
        reject(cmd->drawIndirect(devBuf, 2u, 1u, 16u), gfx::ErrorCode::InvalidArgument, "drawIndirect unaligned offset", totals);
        reject(cmd->drawIndirect(devBuf, 0u, 5u, 16u), gfx::ErrorCode::InvalidArgument, "drawIndirect out of bounds", totals);
        reject(cmd->drawIndexedIndirect(devBuf, 0u, 1u, 16u), gfx::ErrorCode::InvalidArgument, "drawIndexedIndirect stride < 20", totals);
        reject(cmd->drawIndexedIndirect(devBuf, 0u, 1u, 20u), gfx::ErrorCode::InvalidState, "drawIndexedIndirect without index buffer", totals);
        take(cmd->endRenderPass(), "end reject pass", totals);
        take(cmd->end(), "end reject cmd", totals);
    }

    computePipe.reset();
    graphicsPipe.reset();
    const auto finalStats = take(device->resourceStatistics(), "final resourceStatistics", totals);
    check(finalStats.uniqueResources == 0u && finalStats.resourceAllocationBytes == 0u, "zero leaked resources", totals);

    std::uint64_t valErrors = 0;
    if (validation) {
        valErrors = take(device->validationErrorCount(), "validationErrorCount", totals);
        check(valErrors == 0u, "validation errors must be 0", totals);
    }

    std::cout << "PASS adapter=\"" << caps.adapterName << "\""
              << " backend=" << (backend == gfx::Backend::D3D12 ? "D3D12" : "Vulkan")
              << " validation=" << (validation ? "on" : "off")
              << " val_errors=" << valErrors
              << " indirect=" << (caps.indirectDraw ? "yes" : "no")
              << " mdi=" << (caps.multiDrawIndirect ? "yes" : "no")
              << " max_mdi=" << caps.maxDrawIndirectCount
              << " bindless=" << (caps.descriptorIndexing ? "yes" : "no")
              << " max_bindless=" << caps.maxBindlessDescriptors
              << " mesh=" << (caps.meshShaders ? "yes" : "no")
              << "\n";
}

void runWarpLossRecovery(Totals& totals) {
    gfx::DeviceDesc desc{};
    desc.backend = gfx::Backend::D3D12;
    desc.softwareAdapter = true;
    desc.enableDeviceLossTesting = true;
    auto device = take(gfx::createDevice(desc), "create WARP loss device", totals);
    take(device->removeForTesting(), "WARP removeForTesting", totals);
    gfx::MeshPipelineDesc meshDesc{};
    meshDesc.mesh = {gfx::ShaderStage::Mesh, {std::byte{1}}, gfx::ShaderFormat::DXIL};
    reject(device->createMeshPipeline(meshDesc), gfx::ErrorCode::DeviceLost, "createMeshPipeline after loss", totals);
    auto recovered = take(device->recover(), "WARP recover()", totals);
    check(recovered->caps().indirectDraw && recovered->caps().multiDrawIndirect, "recovered WARP keeps indirect/MDI caps", totals);
}
} // namespace

int main(int argc, char** argv) {
    try {
        gfx::Backend backend = gfx::Backend::D3D12;
        bool validation = false;
        bool warp = false;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg == "--d3d12") backend = gfx::Backend::D3D12;
            else if (arg == "--vulkan") backend = gfx::Backend::Vulkan;
            else if (arg == "--validation") validation = true;
            else if (arg == "--warp") warp = true;
            else {
                std::cerr << "Unknown argument: " << arg << "\n";
                return 2;
            }
        }

        Totals totals{};
        auto adaptersResult = gfx::enumerateAdapters(backend);
        if (!adaptersResult) {
            if (adaptersResult.error().code == gfx::ErrorCode::Unsupported) {
                std::cout << "SKIP: backend unavailable: " << adaptersResult.error().message << "\n";
                return 77;
            }
            throw std::runtime_error(adaptersResult.error().message);
        }

        std::vector<gfx::AdapterInfo> selected;
        for (const auto& info : *adaptersResult) {
            if (info.softwareAdapter == warp) selected.push_back(info);
        }
        if (selected.empty()) {
            std::cout << "SKIP: no matching adapter found\n";
            return 77;
        }

        for (const auto& adapter : selected) {
            runAdapterSuite(backend, adapter, validation, totals);
        }
        if (backend == gfx::Backend::D3D12 && warp && !validation) {
            runWarpLossRecovery(totals);
        }

        std::cout << "SUMMARY assertions=" << totals.assertions
                  << " rejections=" << totals.rejections
                  << " adapters=" << selected.size() << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
