#include <graphics/Graphics.hpp>
#include "fixtures/ComputeFixtures.hpp"
#include "fixtures/SpirvFixtures.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Totals {
    std::uint64_t adapters{};
    std::uint64_t assertions{};
    std::uint64_t rejections{};
};

void check(bool condition, const std::string& message, Totals& totals) {
    if (!condition) throw std::runtime_error(message);
    ++totals.assertions;
}

template <class T>
T take(gfx::Result<T> result, const std::string& what, Totals& totals) {
    if (!result) {
        throw std::runtime_error(what + ": " + result.error().message +
                                 " (code=" + std::to_string(static_cast<int>(result.error().code)) +
                                 ", native=" + std::to_string(result.error().nativeCode) + ")");
    }
    ++totals.assertions;
    return std::move(result.value());
}

void take(gfx::Result<void> result, const std::string& what, Totals& totals) {
    if (!result) {
        throw std::runtime_error(what + ": " + result.error().message +
                                 " (code=" + std::to_string(static_cast<int>(result.error().code)) +
                                 ", native=" + std::to_string(result.error().nativeCode) + ")");
    }
    ++totals.assertions;
}

template <std::size_t N>
gfx::ShaderBinary spirvBinary(gfx::ShaderStage stage, const std::array<std::uint32_t, N>& words) {
    gfx::ShaderBinary bin{};
    bin.format = gfx::ShaderFormat::SPIRV;
    bin.stage = stage;
    bin.bytes.resize(N * sizeof(std::uint32_t));
    std::memcpy(bin.bytes.data(), words.data(), bin.bytes.size());
    return bin;
}

gfx::PipelinePtr makeBufferComputePipeline(gfx::Device& device, Totals& totals) {
    gfx::ComputePipelineDesc desc{};
    desc.compute = spirvBinary(gfx::ShaderStage::Compute, computeFixtures::buffer_scale_add);
    desc.bindingLayout.entries = {
        {gfx::BindingType::StorageBuffer, 0, 1, 0, gfx::ShaderVisibility::Compute},
        {gfx::BindingType::ConstantBuffer, 0, 1, 0, gfx::ShaderVisibility::Compute},
    };
    return take(device.createComputePipeline(desc), "create compute pipeline", totals);
}

void runCleanGraphicsAndCompute(gfx::Device& device, const std::string& tag, Totals& totals) {
    // 1. Clean 64x64 graphics render pass under GPU-AV.
    {
        gfx::GraphicsPipelineDesc pDesc{};
        pDesc.vertex = spirvBinary(gfx::ShaderStage::Vertex, fixtures::fullscreen);
        pDesc.fragment = spirvBinary(gfx::ShaderStage::Fragment, fixtures::green);
        pDesc.colorFormat = gfx::Format::RGBA8UNorm;
        auto pipe = take(device.createGraphicsPipeline(pDesc), "create graphics pipeline", totals);

        auto rt = take(device.createTexture({64, 64, gfx::Format::RGBA8UNorm, true}), "create RT", totals);
        const auto fp = rt->footprint();
        auto rb = take(device.createBuffer({fp.size, gfx::Memory::Readback}), "create RT readback", totals);

        auto cmd = take(device.createCommandList(), "create gfx cmd", totals);
        take(cmd->begin(), "begin gfx", totals);
        take(cmd->transition(rt, gfx::ResourceState::RenderTarget), "RT transition", totals);
        gfx::RenderPassDesc rp{};
        rp.colors.push_back({rt, gfx::LoadOp::Clear, {0, 0, 0, 1}});
        take(cmd->beginRenderPass(rp), "beginRP", totals);
        take(cmd->setPipeline(pipe), "setPipeline", totals);
        take(cmd->draw(3), "draw", totals);
        take(cmd->endRenderPass(), "endRP", totals);
        take(cmd->transition(rt, gfx::ResourceState::CopySource), "CopySrc", totals);
        take(cmd->copyTextureToBuffer(rt, rb), "copyTexToBuf", totals);
        take(cmd->end(), "end gfx", totals);

        auto f = take(device.queue().submit(*cmd), "submit gfx", totals);
        take(device.queue().wait(f, std::chrono::milliseconds(5000)), "wait gfx", totals);
        const auto bytes = take(rb->read(0, fp.size), "readback gfx", totals);
        for (std::uint32_t y = 0; y < 64; ++y) {
            for (std::uint32_t x = 0; x < 64; ++x) {
                const std::size_t off = static_cast<std::size_t>(y) * fp.rowPitch + x * 4u;
                if (std::to_integer<std::uint8_t>(bytes[off + 0]) != 0 ||
                    std::to_integer<std::uint8_t>(bytes[off + 1]) != 255 ||
                    std::to_integer<std::uint8_t>(bytes[off + 2]) != 0 ||
                    std::to_integer<std::uint8_t>(bytes[off + 3]) != 255) {
                    throw std::runtime_error(tag + " graphics pixel mismatch");
                }
            }
        }
        ++totals.assertions;
    }

    // 2. Clean 64-element (256B) compute dispatch under GPU-AV.
    {
        auto pipe = makeBufferComputePipeline(device, totals);
        auto up = take(device.createBuffer({256, gfx::Memory::Upload}), "create compute up", totals);
        auto storage = take(device.createBuffer({256, gfx::Memory::Device, /*storage=*/true}), "create storage", totals);
        auto rb = take(device.createBuffer({256, gfx::Memory::Readback}), "create compute rb", totals);
        auto cbUp = take(device.createBuffer({256, gfx::Memory::Upload}), "create cb up", totals);
        auto cbDev = take(device.createBuffer({256, gfx::Memory::Device}), "create cb dev", totals);

        std::array<std::uint32_t, 64> inVals{};
        for (std::uint32_t i = 0; i < 64; ++i) inVals[i] = i + 1u;
        take(up->write(0, std::as_bytes(std::span<const std::uint32_t>(inVals))), "write inVals", totals);
        std::array<std::byte, 256> cbData{};
        const std::array<float, 4> params{3.0f, 5.0f, 0.0f, 0.0f};
        std::memcpy(cbData.data(), params.data(), sizeof(params));
        take(cbUp->write(0, cbData), "write cb", totals);

        auto cmd = take(device.createCommandList(), "create cmp cmd", totals);
        take(cmd->begin(), "begin cmp", totals);
        take(cmd->transition(storage, gfx::ResourceState::CopyDestination), "storage -> CopyDst", totals);
        take(cmd->copyBuffer(up, storage, 256), "copy inVals", totals);
        take(cmd->transition(cbDev, gfx::ResourceState::CopyDestination), "cb -> CopyDst", totals);
        take(cmd->copyBuffer(cbUp, cbDev, 256), "copy cb", totals);
        take(cmd->transition(cbDev, gfx::ResourceState::ConstantBuffer), "cb -> CB", totals);
        take(cmd->transition(storage, gfx::ResourceState::UnorderedAccess), "storage -> UAV", totals);
        take(cmd->setComputePipeline(pipe), "setComputePipeline", totals);
        gfx::ResourceBindings bindings{};
        bindings.storageBuffers = {{0, 0, storage, 0, 256}};
        bindings.constants = {{1, 0, cbDev, 0, 256}};
        take(cmd->bindComputeResources(bindings), "bindComputeResources", totals);
        take(cmd->dispatch(1, 1, 1), "dispatch clean", totals);
        take(cmd->uavBarrier(storage), "uavBarrier", totals);
        take(cmd->transition(storage, gfx::ResourceState::CopySource), "storage -> CopySrc", totals);
        take(cmd->copyBuffer(storage, rb, 256), "copy outVals", totals);
        take(cmd->end(), "end cmp", totals);

        auto f = take(device.queue().submit(*cmd), "submit cmp", totals);
        take(device.queue().wait(f, std::chrono::milliseconds(5000)), "wait cmp", totals);
        const auto outBytes = take(rb->read(0, 256), "readback cmp", totals);
        for (std::uint32_t i = 0; i < 64; ++i) {
            std::uint32_t actual = 0;
            std::memcpy(&actual, outBytes.data() + i * 4u, 4u);
            const std::uint32_t expected = (i + 1u) * 3u + 5u;
            if (actual != expected) {
                throw std::runtime_error(tag + " compute mismatch at " + std::to_string(i));
            }
        }
        ++totals.assertions;
    }
}

std::uint64_t runIndirectDispatchGpuAvControl(gfx::Device& device, Totals& totals) {
    // Write an invalid DispatchIndirectCommand (groupCountX = 0x10000000 > maxComputeWorkGroupCount[0])
    // into a GPU indirect buffer and execute dispatchIndirect().
    // CPU-side core/sync validation cannot inspect GPU buffer contents (errors == 0);
    // Vulkan GPU-Assisted Validation (gpuav_indirect_dispatches_buffers) validates the
    // indirect buffer on the GPU and reports VUID-VkDispatchIndirectCommand-x-00417 (> 0).
    auto pipe = makeBufferComputePipeline(device, totals);
    auto storage = take(device.createBuffer({256, gfx::Memory::Device, /*storage=*/true}), "create storage", totals);
    auto cbUp = take(device.createBuffer({256, gfx::Memory::Upload}), "create cb up", totals);
    auto cbDev = take(device.createBuffer({256, gfx::Memory::Device}), "create cb dev", totals);
    std::array<std::byte, 256> cbData{};
    const std::array<float, 4> params{1.0f, 0.0f, 0.0f, 0.0f};
    std::memcpy(cbData.data(), params.data(), sizeof(params));
    take(cbUp->write(0, cbData), "write cb", totals);

    auto indUp = take(device.createBuffer({256, gfx::Memory::Upload}), "create indirect up", totals);
    auto indDev = take(device.createBuffer({256, gfx::Memory::Device}), "create indirect dev", totals);
    // Exceeds maxComputeWorkGroupCount[2] (65535 on both NVIDIA GTX 1660 SUPER and Intel UHD 770)
    // without launching billions of workgroups: triggers VUID-VkDispatchIndirectCommand-z-00419 under GPU-AV.
    gfx::DispatchIndirectCommand badCmd{1u, 1u, 65537u};
    take(indUp->write(0, std::as_bytes(std::span<const gfx::DispatchIndirectCommand>(&badCmd, 1))), "write bad indirect cmd", totals);

    auto cmd = take(device.createCommandList(), "create indirect control cmd", totals);
    take(cmd->begin(), "begin indirect control", totals);
    take(cmd->transition(cbDev, gfx::ResourceState::CopyDestination), "cb -> CopyDst", totals);
    take(cmd->copyBuffer(cbUp, cbDev, 256), "copy cb", totals);
    take(cmd->transition(cbDev, gfx::ResourceState::ConstantBuffer), "cb -> CB", totals);
    take(cmd->transition(indDev, gfx::ResourceState::CopyDestination), "ind -> CopyDst", totals);
    take(cmd->copyBuffer(indUp, indDev, 256), "copy bad indirect", totals);
    take(cmd->transition(indDev, gfx::ResourceState::IndirectArgument), "ind -> IndirectArgument", totals);
    take(cmd->transition(storage, gfx::ResourceState::UnorderedAccess), "storage -> UAV", totals);
    take(cmd->setComputePipeline(pipe), "setComputePipeline", totals);
    gfx::ResourceBindings bindings{};
    bindings.storageBuffers = {{0, 0, storage, 0, 256}};
    bindings.constants = {{1, 0, cbDev, 0, 256}};
    take(cmd->bindComputeResources(bindings), "bind indirect control resources", totals);
    take(cmd->dispatchIndirect(indDev, 0), "dispatchIndirect with invalid GPU buffer count", totals);
    take(cmd->end(), "end indirect control", totals);

    auto f = take(device.queue().submit(*cmd), "submit indirect control", totals);
    take(device.queue().wait(f, std::chrono::milliseconds(5000)), "wait indirect control", totals);
    // VVL GPU-AV flushes its GPU-side validation output buffer on vkQueueWaitIdle.
    take(device.queue().waitIdle(), "waitIdle to flush GPU-AV output buffer", totals);
    return take(device.validationErrorCount(), "query validationErrorCount after indirect control", totals);
}

} // namespace

int main() {
    try {
        Totals totals{};
        // 1. Contract rejection: gpuAssistedValidation=true with validation=false must fail InvalidArgument.
        {
            gfx::DeviceDesc bad{};
            bad.backend = gfx::Backend::Vulkan;
            bad.validation = false;
            bad.gpuAssistedValidation = true;
            auto res = gfx::createDevice(bad);
            check(!res && res.error().code == gfx::ErrorCode::InvalidArgument,
                  "gpuAssistedValidation without validation=true must return InvalidArgument", totals);
            ++totals.rejections;
        }

        auto adapters = take(gfx::enumerateAdapters(gfx::Backend::Vulkan), "enumerate Vulkan adapters", totals);
        for (const auto& a : adapters) {
            if (a.softwareAdapter) continue;
            const std::string tag = "Vulkan/" + a.name;
            std::cout << "=== GPU-Assisted Validation " << tag << " ===\n";

            // 2. Sync-only validation baseline: caps.gpuAssistedValidation == false, OOB shader access undetected (0).
            {
                gfx::DeviceDesc syncDesc{};
                syncDesc.backend = gfx::Backend::Vulkan;
                syncDesc.adapter = a.id;
                syncDesc.validation = true;
                syncDesc.gpuAssistedValidation = false;
                auto syncDev = take(gfx::createDevice(syncDesc), "create sync-only device", totals);
                check(!syncDev->caps().gpuAssistedValidation, "sync-only device must report gpuAssistedValidation=false", totals);
                const auto syncCtrlErrors = runIndirectDispatchGpuAvControl(*syncDev, totals);
                check(syncCtrlErrors == 0, "sync-only CPU validation must not inspect GPU indirect buffer (distinguished from GPU-AV)", totals);
                std::cout << "  [" << tag << "] Sync-only validation indirect-buffer control: errors=" << syncCtrlErrors << " (expected 0)\n";
            }

            // 3. GPU-Assisted Validation clean workload: caps.gpuAssistedValidation == true, clean draw+compute errors == 0.
            {
                gfx::DeviceDesc gpuAvDesc{};
                gpuAvDesc.backend = gfx::Backend::Vulkan;
                gpuAvDesc.adapter = a.id;
                gpuAvDesc.validation = true;
                gpuAvDesc.gpuAssistedValidation = true;
                auto gpuAvDev = take(gfx::createDevice(gpuAvDesc), "create GPU-AV device", totals);
                check(gpuAvDev->caps().gpuAssistedValidation, "GPU-AV device must report gpuAssistedValidation=true", totals);
                runCleanGraphicsAndCompute(*gpuAvDev, tag, totals);
                const auto cleanErrors = take(gpuAvDev->validationErrorCount(), "clean GPU-AV error count", totals);
                check(cleanErrors == 0, "clean graphics+compute workload under GPU-AV must have 0 errors", totals);
                std::cout << "  [" << tag << "] GPU-AV clean Graphics(64x64) + Compute(64x u32): errors=0 PASS\n";

                // 4. GPU-Assisted Validation positive control: invalid GPU dispatchIndirect buffer MUST be caught (> 0)!
                const auto gpuAvCtrlErrors = runIndirectDispatchGpuAvControl(*gpuAvDev, totals);
                check(gpuAvCtrlErrors > 0, "GPU-AV must detect invalid GPU indirect dispatch buffer (> 0)", totals);
                std::cout << "  [" << tag << "] GPU-AV indirect-buffer control detected: errors=" << gpuAvCtrlErrors << " (> 0) PASS\n";
            }
            ++totals.adapters;
        }
        check(totals.adapters > 0, "Expected at least one physical Vulkan adapter", totals);
        std::cout << "PASS gfx_vulkan_gpu_av_tests: adapters=" << totals.adapters
                  << " assertions=" << totals.assertions
                  << " rejections=" << totals.rejections << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
