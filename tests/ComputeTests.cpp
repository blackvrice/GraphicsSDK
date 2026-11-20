#include <graphics/Graphics.hpp>
#include "fixtures/BindingFixtures.hpp"
#include "fixtures/ComputeFixtures.hpp"

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
#include <vector>

namespace {
struct Totals {
    std::uint64_t assertions{};
    std::uint64_t dispatches{};
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

void verifyGradientImage(
    const gfx::BufferPtr& readback,
    const gfx::TextureFootprint& fp,
    float cr,
    float cg,
    float cb,
    bool flipY,
    Totals& totals) {
    const auto data = take(readback->read(0, fp.size), "readback image", totals);
    for (std::uint32_t y = 0; y < fp.height; ++y) {
        const std::uint32_t sampleY = flipY ? (fp.height - 1u - y) : y;
        for (std::uint32_t x = 0; x < fp.rowBytes / 4u; ++x) {
            const std::size_t off = static_cast<std::size_t>(y) * fp.rowPitch + static_cast<std::size_t>(x) * 4u;
            const int er = static_cast<int>(std::lround(cr * (static_cast<float>(x) / 15.0f) * 255.0f));
            const int eg = static_cast<int>(std::lround(cg * (static_cast<float>(sampleY) / 15.0f) * 255.0f));
            const int eb = static_cast<int>(std::lround(cb * 255.0f));
            const int ar = static_cast<int>(std::to_integer<std::uint8_t>(data[off + 0]));
            const int ag = static_cast<int>(std::to_integer<std::uint8_t>(data[off + 1]));
            const int ab = static_cast<int>(std::to_integer<std::uint8_t>(data[off + 2]));
            const int aa = static_cast<int>(std::to_integer<std::uint8_t>(data[off + 3]));
            if (std::abs(ar - er) > 2 || std::abs(ag - eg) > 2 || std::abs(ab - eb) > 2 || std::abs(aa - 255) > 1) {
                throw std::runtime_error("Compute/Graphics gradient pixel mismatch at (" +
                    std::to_string(x) + "," + std::to_string(y) + ")");
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

void runComputeSuite(gfx::Device& device, gfx::Backend backend, bool validation, Totals& totals) {
    // 1. Capabilities and format support.
    check(device.caps().compute, "DeviceCaps::compute must report true", totals);
    const auto rgba8Support = take(device.queryFormatSupport(gfx::Format::RGBA8UNorm), "queryFormatSupport RGBA8", totals);
    const auto d32Support = take(device.queryFormatSupport(gfx::Format::D32Float), "queryFormatSupport D32", totals);
    check(rgba8Support.storage, "RGBA8UNorm must support storage UAV", totals);
    check(!d32Support.storage, "D32Float must not report storage UAV support", totals);

    {
        auto bufShader = makeBufferComputeShader(backend, totals);
        auto texShader = makeTextureComputeShader(backend, totals);

        // 2. Reflection checks for Compute shaders.
        const auto bufReflected = take(gfx::reflectShader(bufShader), "reflect buffer compute shader", totals);
        bool hasStorageBuf = false, hasCbuf = false;
        for (const auto& b : bufReflected) {
            if (b.type == gfx::ShaderResourceType::StorageBuffer) hasStorageBuf = true;
            if (b.type == gfx::ShaderResourceType::ConstantBuffer) hasCbuf = true;
        }
        check(hasStorageBuf && hasCbuf, "Buffer compute reflection must expose StorageBuffer and ConstantBuffer", totals);

        const auto texReflected = take(gfx::reflectShader(texShader), "reflect texture compute shader", totals);
        bool hasStorageTex = false, hasTexCbuf = false;
        for (const auto& b : texReflected) {
            if (b.type == gfx::ShaderResourceType::StorageTexture2D) hasStorageTex = true;
            if (b.type == gfx::ShaderResourceType::ConstantBuffer) hasTexCbuf = true;
        }
        check(hasStorageTex && hasTexCbuf, "Texture compute reflection must expose StorageTexture2D and ConstantBuffer", totals);

        gfx::ComputePipelineDesc bufPipeDesc{};
        bufPipeDesc.compute = bufShader;
        bufPipeDesc.threadgroupSizeX = 64;
        bufPipeDesc.bindingLayout.entries = {
            {gfx::BindingType::StorageBuffer, 0, 1, 0, gfx::ShaderVisibility::Compute},
            {gfx::BindingType::ConstantBuffer, 0, 1, 0, gfx::ShaderVisibility::Compute},
        };
        auto bufPipeline = take(device.createComputePipeline(bufPipeDesc), "create buffer compute pipeline", totals);

        gfx::ComputePipelineDesc texPipeDesc{};
        texPipeDesc.compute = texShader;
        texPipeDesc.threadgroupSizeX = 8;
        texPipeDesc.threadgroupSizeY = 8;
        texPipeDesc.bindingLayout.entries = {
            {gfx::BindingType::StorageTexture2D, 0, 1, 0, gfx::ShaderVisibility::Compute},
            {gfx::BindingType::ConstantBuffer, 0, 1, 0, gfx::ShaderVisibility::Compute},
        };
        auto texPipeline = take(device.createComputePipeline(texPipeDesc), "create texture compute pipeline", totals);

        auto graphicsPipeline = makeSampledGraphicsPipeline(device, backend, totals);

        // Pipeline creation rejection contracts.
        {
            auto bad = bufPipeDesc;
            bad.threadgroupSizeX = 0;
            reject(device.createComputePipeline(bad), gfx::ErrorCode::InvalidArgument, "Zero threadgroupSizeX rejected", totals);

            bad = bufPipeDesc;
            bad.bindingLayout.entries[0].visibility = gfx::ShaderVisibility::Fragment;
            reject(device.createComputePipeline(bad), gfx::ErrorCode::InvalidArgument, "Fragment visibility on compute layout rejected", totals);

            bad = bufPipeDesc;
            bad.compute.stage = gfx::ShaderStage::Vertex;
            reject(device.createComputePipeline(bad), gfx::ErrorCode::InvalidArgument, "Vertex stage in createComputePipeline rejected", totals);

            gfx::GraphicsPipelineDesc badGfx{};
            badGfx.vertex = bufShader;
            badGfx.vertex.stage = gfx::ShaderStage::Vertex;
            badGfx.bindingLayout.entries = {{gfx::BindingType::StorageBuffer, 0, 1, 0, gfx::ShaderVisibility::All}};
            reject(device.createGraphicsPipeline(badGfx), gfx::ErrorCode::InvalidArgument, "StorageBuffer in graphics pipeline rejected", totals);
        }

        // Resource creation rejection contracts.
        reject(device.createBuffer({256, gfx::Memory::Upload, true}), gfx::ErrorCode::InvalidArgument, "Upload storage buffer rejected", totals);
        reject(device.createBuffer({256, gfx::Memory::Readback, true}), gfx::ErrorCode::InvalidArgument, "Readback storage buffer rejected", totals);
        reject(device.createTexture({16, 16, gfx::Format::D32Float, false, true, true}), gfx::ErrorCode::InvalidArgument, "Depth storage texture rejected", totals);

        // 3. Storage Buffer Compute + uavBarrier chaining + non-zero 256-byte offset.
        constexpr std::uint32_t kTotalUints = 512;
        constexpr std::uint64_t kBufBytes = kTotalUints * sizeof(std::uint32_t); // 2048 bytes (two 1024-byte slices)
        auto uploadBuf = take(device.createBuffer({kBufBytes, gfx::Memory::Upload}), "uploadBuf", totals);
        auto storageBuf = take(device.createBuffer({kBufBytes, gfx::Memory::Device, true}), "storageBuf", totals);
        auto nonStorageBuf = take(device.createBuffer({256, gfx::Memory::Device, false}), "nonStorageBuf", totals);
        auto readbackBuf = take(device.createBuffer({kBufBytes, gfx::Memory::Readback}), "readbackBuf", totals);
        auto cbBuf = take(device.createBuffer({1024, gfx::Memory::Upload}), "cbBuf", totals);

        std::vector<std::uint32_t> initial(kTotalUints);
        for (std::uint32_t i = 0; i < kTotalUints; ++i) initial[i] = i * 7u + 3u;
        take(uploadBuf->write(0, std::as_bytes(std::span{initial})), "write initial storage input", totals);

        // Write 3 constant buffer slots at offsets 0, 256, 512, 768.
        auto writeCb = [&](std::uint64_t offset, float x, float y, float z, float w) {
            const std::array<float, 4> v{x, y, z, w};
            take(cbBuf->write(offset, std::as_bytes(std::span{v})), "write CB slot", totals);
        };
        writeCb(0, 3.0f, 7.0f, 0.0f, 0.0f);     // Pass 1 on slice 0: x*3 + 7
        writeCb(256, 2.0f, 5.0f, 0.0f, 0.0f);   // Pass 2 on slice 0: (x*3 + 7)*2 + 5
        writeCb(512, 5.0f, 11.0f, 0.0f, 0.0f);  // Pass on slice 1 (offset=1024): x*5 + 11
        writeCb(768, 0.9f, 0.6f, 0.25f, 1.0f);  // Texture generation final color

        auto cbTexPass1 = take(device.createBuffer({256, gfx::Memory::Upload}), "cbTexPass1", totals);
        {
            const std::array<float, 4> c1{1.0f, 0.2f, 0.8f, 1.0f};
            take(cbTexPass1->write(0, std::as_bytes(std::span{c1})), "write cbTexPass1", totals);
        }

        // 4. Storage Texture2D + TextureViewUsage::Storage + Graphics-Compute sync.
        constexpr std::uint32_t kTexSize = 16;
        auto storageTex = take(device.createTexture({kTexSize, kTexSize, gfx::Format::RGBA8UNorm, false, false, true}), "storageTex", totals);
        auto nonStorageTex = take(device.createTexture({kTexSize, kTexSize, gfx::Format::RGBA8UNorm, true, false, false}), "nonStorageTex", totals);
        auto renderTarget = take(device.createTexture({kTexSize, kTexSize, gfx::Format::RGBA8UNorm, true, false, false}), "renderTarget", totals);
        auto storageView = take(device.createTextureView(storageTex, {std::nullopt, gfx::TextureViewUsage::Storage}), "create storageView", totals);
        auto srvView = take(device.createTextureView(storageTex, {std::nullopt, gfx::TextureViewUsage::ShaderRead}), "create srvView", totals);

        const auto texFp = storageTex->footprint();
        auto texDirectRb = take(device.createBuffer({texFp.size, gfx::Memory::Readback}), "texDirectRb", totals);
        auto gfxResultRb = take(device.createBuffer({texFp.size, gfx::Memory::Readback}), "gfxResultRb", totals);

        auto pool = take(device.createCommandPool(), "create compute CommandPool", totals);
        auto cmd = take(pool->createCommandList(), "create pooled compute CommandList", totals);

        take(cmd->begin(), "begin compute command list", totals);
        const bool hasTimestamps = device.caps().timestampQueries;
        if (hasTimestamps) take(cmd->writeTimestamp(0), "writeTimestamp 0", totals);

        // Recording-time rejection checks before compute setup.
        reject(cmd->transition(nonStorageBuf, gfx::ResourceState::UnorderedAccess), gfx::ErrorCode::InvalidArgument, "non-storage buffer UAV state rejected", totals);
        reject(cmd->transition(nonStorageTex, gfx::ResourceState::UnorderedAccess), gfx::ErrorCode::InvalidArgument, "non-storage texture UAV state rejected", totals);
        reject(cmd->uavBarrier(nonStorageBuf), gfx::ErrorCode::InvalidArgument, "uavBarrier on non-storage buffer rejected", totals);
        reject(cmd->uavBarrier(nonStorageTex), gfx::ErrorCode::InvalidArgument, "uavBarrier on non-storage texture rejected", totals);
        reject(cmd->uavBarrier(storageBuf), gfx::ErrorCode::InvalidState, "uavBarrier outside UnorderedAccess rejected", totals);
        reject(cmd->setComputePipeline(graphicsPipeline), gfx::ErrorCode::InvalidArgument, "graphics pipeline in setComputePipeline rejected", totals);
        reject(cmd->dispatch(1, 1, 1), gfx::ErrorCode::InvalidState, "dispatch without compute pipeline rejected", totals);

        // Upload initial data into storageBuf.
        take(cmd->transition(storageBuf, gfx::ResourceState::CopyDestination), "storageBuf -> CopyDst", totals);
        take(cmd->copyBuffer(uploadBuf, storageBuf, kBufBytes), "upload -> storageBuf", totals);
        take(cmd->transition(storageBuf, gfx::ResourceState::UnorderedAccess), "storageBuf -> UAV", totals);
        take(cmd->transition(cbBuf, gfx::ResourceState::ConstantBuffer), "cbBuf -> CB", totals);
        take(cmd->transition(cbTexPass1, gfx::ResourceState::ConstantBuffer), "cbTexPass1 -> CB", totals);

        take(cmd->setComputePipeline(bufPipeline), "setComputePipeline bufPipeline", totals);
        reject(cmd->dispatch(1, 1, 1), gfx::ErrorCode::InvalidState, "dispatch without bindComputeResources rejected", totals);

        // Bad offset / size / state binding rejections.
        gfx::ResourceBindings badAlign;
        badAlign.storageBuffers = {{0, 0, storageBuf, 16, 256}};
        badAlign.constants = {{1, 0, cbBuf, 0, 256}};
        reject(cmd->bindComputeResources(badAlign), gfx::ErrorCode::InvalidArgument, "unaligned StorageBuffer offset rejected", totals);

        gfx::ResourceBindings badSize;
        badSize.storageBuffers = {{0, 0, storageBuf, 0, 10}};
        badSize.constants = {{1, 0, cbBuf, 0, 256}};
        reject(cmd->bindComputeResources(badSize), gfx::ErrorCode::InvalidArgument, "non-4-byte-multiple StorageBuffer size rejected", totals);

        // Dispatch 1: slice 0 (0..1024), scale=3, add=7.
        gfx::ResourceBindings bufPass1;
        bufPass1.storageBuffers = {{0, 0, storageBuf, 0, 1024}};
        bufPass1.constants = {{1, 0, cbBuf, 0, 256}};
        take(cmd->bindComputeResources(bufPass1), "bindComputeResources bufPass1", totals);
        reject(cmd->dispatch(0, 1, 1), gfx::ErrorCode::InvalidArgument, "zero groupCountX rejected", totals);
        take(cmd->dispatch(4, 1, 1), "dispatch bufPass1", totals);
        ++totals.dispatches;

        // Explicit UAV barrier between dependent compute writes on same storage buffer!
        take(cmd->uavBarrier(storageBuf), "uavBarrier storageBuf", totals);

        // Dispatch 2: slice 0 (0..1024), scale=2, add=5.
        gfx::ResourceBindings bufPass2;
        bufPass2.storageBuffers = {{0, 0, storageBuf, 0, 1024}};
        bufPass2.constants = {{1, 0, cbBuf, 256, 256}};
        take(cmd->bindComputeResources(bufPass2), "bindComputeResources bufPass2", totals);
        take(cmd->dispatch(4, 1, 1), "dispatch bufPass2", totals);
        ++totals.dispatches;

        // Dispatch 3: slice 1 (offset=1024, size=1024), scale=5, add=11.
        gfx::ResourceBindings bufPass3;
        bufPass3.storageBuffers = {{0, 0, storageBuf, 1024, 1024}};
        bufPass3.constants = {{1, 0, cbBuf, 512, 256}};
        take(cmd->bindComputeResources(bufPass3), "bindComputeResources bufPass3", totals);
        take(cmd->dispatch(4, 1, 1), "dispatch bufPass3", totals);
        ++totals.dispatches;

        take(cmd->transition(storageBuf, gfx::ResourceState::CopySource), "storageBuf -> CopySrc", totals);
        take(cmd->copyBuffer(storageBuf, readbackBuf, kBufBytes), "storageBuf -> readbackBuf", totals);

        // Storage Texture2D compute pass 1 (bound via TextureViewUsage::Storage).
        take(cmd->transition(storageTex, gfx::ResourceState::UnorderedAccess), "storageTex -> UAV", totals);
        take(cmd->setComputePipeline(texPipeline), "setComputePipeline texPipeline", totals);

        gfx::ResourceBindings badViewBind;
        badViewBind.storageTextures = {{0, 0, nullptr, srvView}};
        badViewBind.constants = {{1, 0, cbBuf, 768, 256}};
        reject(cmd->bindComputeResources(badViewBind), gfx::ErrorCode::InvalidArgument, "ShaderRead view in StorageTextureBinding rejected", totals);

        gfx::ResourceBindings texPass1;
        texPass1.storageTextures = {{0, 0, nullptr, storageView}};
        texPass1.constants = {{1, 0, cbTexPass1, 0, 256}};
        take(cmd->bindComputeResources(texPass1), "bindComputeResources texPass1 with Storage view", totals);
        take(cmd->dispatch(2, 2, 1), "dispatch texPass1", totals);
        ++totals.dispatches;

        // UAV barrier on storage texture, then overwrite with final color via raw TexturePtr binding.
        take(cmd->uavBarrier(storageTex), "uavBarrier storageTex", totals);
        gfx::ResourceBindings texPass2;
        texPass2.storageTextures = {{0, 0, storageTex, nullptr}};
        texPass2.constants = {{1, 0, cbBuf, 768, 256}};
        take(cmd->bindComputeResources(texPass2), "bindComputeResources texPass2 with raw TexturePtr", totals);
        take(cmd->dispatch(2, 2, 1), "dispatch texPass2", totals);
        ++totals.dispatches;

        // Graphics-Compute synchronization: UnorderedAccess -> ShaderRead, sample in Graphics RenderPass!
        take(cmd->transition(storageTex, gfx::ResourceState::ShaderRead), "storageTex UAV -> ShaderRead", totals);
        take(cmd->transition(renderTarget, gfx::ResourceState::RenderTarget), "renderTarget -> RT", totals);
        take(cmd->beginRenderPass(renderTarget, {0.0f, 0.0f, 0.0f, 1.0f}), "beginRenderPass after compute", totals);

        // Compute commands inside an active render pass must fail cleanly.
        reject(cmd->setPipeline(bufPipeline), gfx::ErrorCode::InvalidArgument, "compute pipeline in graphics setPipeline rejected", totals);
        reject(cmd->setComputePipeline(bufPipeline), gfx::ErrorCode::InvalidState, "setComputePipeline inside pass rejected", totals);
        reject(cmd->dispatch(1, 1, 1), gfx::ErrorCode::InvalidState, "dispatch inside pass rejected", totals);
        reject(cmd->uavBarrier(storageBuf), gfx::ErrorCode::InvalidState, "uavBarrier inside pass rejected", totals);

        take(cmd->setPipeline(graphicsPipeline), "setPipeline sampled graphics", totals);
        take(cmd->bindTextureView(srvView), "bindTextureView compute output", totals);
        take(cmd->draw(3), "draw sampling compute output", totals);
        take(cmd->endRenderPass(), "endRenderPass", totals);

        take(cmd->transition(storageTex, gfx::ResourceState::CopySource), "storageTex -> CopySrc", totals);
        take(cmd->copyTextureToBuffer(storageTex, texDirectRb), "copy storageTex direct", totals);
        take(cmd->transition(renderTarget, gfx::ResourceState::CopySource), "renderTarget -> CopySrc", totals);
        take(cmd->copyTextureToBuffer(renderTarget, gfxResultRb), "copy renderTarget", totals);

        if (hasTimestamps) {
            take(cmd->writeTimestamp(1), "writeTimestamp 1", totals);
            take(cmd->resolveTimestamps(), "resolveTimestamps", totals);
        }
        take(cmd->end(), "end compute command list", totals);

        const auto fence = take(device.queue().submit(*cmd), "submit compute+graphics batch", totals);
        take(device.queue().wait(fence), "wait compute+graphics batch", totals);

        if (hasTimestamps) {
            const double ms = take(cmd->elapsedGpuMilliseconds(), "compute GPU elapsed ms", totals);
            check(std::isfinite(ms) && ms >= 0.0, "Compute GPU timestamp must be finite and non-negative", totals);
        }
        take(pool->reset(), "reset compute CommandPool", totals);

        // Verify all 512 uint32_t values from storage buffer.
        const auto rawOut = take(readbackBuf->read(0, kBufBytes), "readback storageBuf", totals);
        std::vector<std::uint32_t> actual(kTotalUints);
        std::memcpy(actual.data(), rawOut.data(), kBufBytes);
        for (std::uint32_t i = 0; i < 256; ++i) {
            const std::uint32_t expected0 = (initial[i] * 3u + 7u) * 2u + 5u;
            if (actual[i] != expected0) {
                throw std::runtime_error("StorageBuffer slice 0 mismatch at index " + std::to_string(i));
            }
            const std::uint32_t expected1 = initial[256u + i] * 5u + 11u;
            if (actual[256u + i] != expected1) {
                throw std::runtime_error("StorageBuffer slice 1 mismatch at index " + std::to_string(i));
            }
        }
        ++totals.assertions;

        // Verify direct storage texture AND graphics-sampled render target.
        verifyGradientImage(texDirectRb, texFp, 0.9f, 0.6f, 0.25f, false, totals);
        verifyGradientImage(gfxResultRb, texFp, 0.9f, 0.6f, 0.25f, true, totals);
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
    check(desc.adapter.has_value(), "WARP adapter required for compute loss recovery", totals);

    auto device = take(gfx::createDevice(desc), "create WARP loss device", totals);
    gfx::ComputePipelineDesc pipeDesc{};
    pipeDesc.compute = makeBufferComputeShader(gfx::Backend::D3D12, totals);
    pipeDesc.threadgroupSizeX = 64;
    pipeDesc.bindingLayout.entries = {
        {gfx::BindingType::StorageBuffer, 0, 1, 0, gfx::ShaderVisibility::Compute},
        {gfx::BindingType::ConstantBuffer, 0, 1, 0, gfx::ShaderVisibility::Compute},
    };
    auto oldPipeline = take(device->createComputePipeline(pipeDesc), "create old compute pipeline", totals);

    take(device->removeForTesting(), "removeForTesting", totals);
    reject(device->createComputePipeline(pipeDesc), gfx::ErrorCode::DeviceLost, "createComputePipeline after loss", totals);

    auto recovered = take(device->recover(), "recover WARP device", totals);
    {
        auto cmd = take(recovered->createCommandList(), "recovered cmd", totals);
        take(cmd->begin(), "recovered begin", totals);
        reject(cmd->setComputePipeline(oldPipeline), gfx::ErrorCode::WrongDevice, "old compute pipeline rejected on recovered device", totals);

        auto freshPipe = take(recovered->createComputePipeline(pipeDesc), "fresh compute pipeline", totals);
        auto up = take(recovered->createBuffer({256, gfx::Memory::Upload}), "fresh up", totals);
        auto st = take(recovered->createBuffer({256, gfx::Memory::Device, true}), "fresh st", totals);
        auto rb = take(recovered->createBuffer({256, gfx::Memory::Readback}), "fresh rb", totals);
        auto cb = take(recovered->createBuffer({256, gfx::Memory::Upload}), "fresh cb", totals);
        std::array<std::uint32_t, 64> init{};
        for (std::uint32_t i = 0; i < 64; ++i) init[i] = i + 1u;
        take(up->write(0, std::as_bytes(std::span{init})), "write fresh up", totals);
        const std::array<float, 4> params{4.0f, 1.0f, 0.0f, 0.0f};
        take(cb->write(0, std::as_bytes(std::span{params})), "write fresh cb", totals);

        take(cmd->transition(st, gfx::ResourceState::CopyDestination), "st -> CopyDst", totals);
        take(cmd->copyBuffer(up, st, 256), "up -> st", totals);
        take(cmd->transition(st, gfx::ResourceState::UnorderedAccess), "st -> UAV", totals);
        take(cmd->transition(cb, gfx::ResourceState::ConstantBuffer), "cb -> CB", totals);
        take(cmd->setComputePipeline(freshPipe), "setComputePipeline fresh", totals);
        gfx::ResourceBindings bindings;
        bindings.storageBuffers = {{0, 0, st, 0, 256}};
        bindings.constants = {{1, 0, cb, 0, 256}};
        take(cmd->bindComputeResources(bindings), "bindComputeResources fresh", totals);
        take(cmd->dispatch(1, 1, 1), "dispatch fresh", totals);
        ++totals.dispatches;
        take(cmd->transition(st, gfx::ResourceState::CopySource), "st -> CopySrc", totals);
        take(cmd->copyBuffer(st, rb, 256), "st -> rb", totals);
        take(cmd->end(), "recovered end", totals);

        const auto f = take(recovered->queue().submit(*cmd), "recovered submit", totals);
        take(recovered->queue().wait(f), "recovered wait", totals);
        const auto raw = take(rb->read(0, 256), "recovered read", totals);
        std::array<std::uint32_t, 64> out{};
        std::memcpy(out.data(), raw.data(), 256);
        for (std::uint32_t i = 0; i < 64; ++i) {
            if (out[i] != (i + 1u) * 4u + 1u) throw std::runtime_error("Recovered compute output mismatch");
        }
        ++totals.assertions;
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
            runComputeSuite(*device, backend, validation, totals);
        }

        if (backend == gfx::Backend::D3D12 && warp && !validation) {
            runWarpLossRecoveryCheck(totals);
        }

        std::cout << "PASS compute adapters=" << selected.size()
                  << " assertions=" << totals.assertions
                  << " dispatches=" << totals.dispatches
                  << " images=" << totals.images
                  << " rejections=" << totals.rejections
                  << " errors=0\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL compute: " << error.what() << '\n';
        return 1;
    }
}
