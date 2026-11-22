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
#include <string_view>
#include <thread>
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

template <class T>
void expectError(const gfx::Result<T>& result, gfx::ErrorCode expected, const std::string& what, Totals& totals) {
    if (result) throw std::runtime_error(what + ": expected error but succeeded");
    if (result.error().code != expected) {
        throw std::runtime_error(what + ": expected error code " +
                                 std::to_string(static_cast<int>(expected)) + " got " +
                                 std::to_string(static_cast<int>(result.error().code)) +
                                 " (" + result.error().message + ")");
    }
    ++totals.rejections;
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

void verifyPostRecoveryWorkload(gfx::Device& device, const std::string& tag, Totals& totals) {
    // 64x64 Graphics Draw
    gfx::GraphicsPipelineDesc pDesc{};
    pDesc.vertex = spirvBinary(gfx::ShaderStage::Vertex, fixtures::fullscreen);
    pDesc.fragment = spirvBinary(gfx::ShaderStage::Fragment, fixtures::red);
    pDesc.colorFormat = gfx::Format::RGBA8UNorm;
    auto pipe = take(device.createGraphicsPipeline(pDesc), "post-recovery create graphics pipeline", totals);

    auto rt = take(device.createTexture({64, 64, gfx::Format::RGBA8UNorm, true}), "post-recovery create RT", totals);
    const auto fp = rt->footprint();
    auto rb = take(device.createBuffer({fp.size, gfx::Memory::Readback}), "post-recovery create RB", totals);

    auto cmd = take(device.createCommandList(), "post-recovery create cmd", totals);
    take(cmd->begin(), "post-recovery begin", totals);
    take(cmd->transition(rt, gfx::ResourceState::RenderTarget), "post-recovery RT", totals);
    gfx::RenderPassDesc rp{};
    rp.colors.push_back({rt, gfx::LoadOp::Clear, {0, 0, 0, 1}});
    take(cmd->beginRenderPass(rp), "post-recovery beginRP", totals);
    take(cmd->setPipeline(pipe), "post-recovery setPipeline", totals);
    take(cmd->draw(3), "post-recovery draw", totals);
    take(cmd->endRenderPass(), "post-recovery endRP", totals);
    take(cmd->transition(rt, gfx::ResourceState::CopySource), "post-recovery CopySrc", totals);
    take(cmd->copyTextureToBuffer(rt, rb), "post-recovery copyTexToBuf", totals);
    take(cmd->end(), "post-recovery end", totals);

    auto f = take(device.queue().submit(*cmd), "post-recovery submit", totals);
    take(device.queue().wait(f, std::chrono::milliseconds(5000)), "post-recovery wait", totals);
    const auto bytes = take(rb->read(0, fp.size), "post-recovery readback", totals);
    for (std::uint32_t y = 0; y < 64; ++y) {
        for (std::uint32_t x = 0; x < 64; ++x) {
            const std::size_t off = static_cast<std::size_t>(y) * fp.rowPitch + x * 4u;
            if (std::to_integer<std::uint8_t>(bytes[off + 0]) != 255 ||
                std::to_integer<std::uint8_t>(bytes[off + 1]) != 0 ||
                std::to_integer<std::uint8_t>(bytes[off + 2]) != 0 ||
                std::to_integer<std::uint8_t>(bytes[off + 3]) != 255) {
                throw std::runtime_error(tag + " post-recovery pixel mismatch");
            }
        }
    }
    ++totals.assertions;
}

} // namespace

int main(int argc, char** argv) {
    bool validation = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--validation") validation = true;
    }

    try {
        Totals totals{};
        auto adapters = take(gfx::enumerateAdapters(gfx::Backend::Vulkan), "enumerate Vulkan", totals);
        for (const auto& a : adapters) {
            if (a.softwareAdapter) continue;
            const std::string tag = "Vulkan/" + a.name + (validation ? " [validation]" : "");
            std::cout << "=== Vulkan Loss Injection & Recovery " << tag << " ===\n";

            // 1. Default (opt-out): deviceLossInjection=false, removeForTesting() -> Unsupported.
            {
                gfx::DeviceDesc d{};
                d.backend = gfx::Backend::Vulkan;
                d.adapter = a.id;
                d.validation = validation;
                d.enableDeviceLossTesting = false;
                auto dev = take(gfx::createDevice(d), "create opt-out device", totals);
                check(!dev->caps().deviceLossInjection, "opt-out deviceLossInjection == false", totals);
                expectError(dev->removeForTesting(), gfx::ErrorCode::Unsupported, "opt-out removeForTesting", totals);
                expectError(dev->recover(), gfx::ErrorCode::InvalidState, "healthy recover rejected", totals);
                if (validation) check(take(dev->validationErrorCount(), "opt-out val", totals) == 0, "opt-out val == 0", totals);
            }

            // 2. Opt-in synthetic loss injection + full error coverage + breadcrumb preservation + recovery.
            {
                gfx::DeviceDesc d{};
                d.backend = gfx::Backend::Vulkan;
                d.adapter = a.id;
                d.validation = validation;
                d.enableDeviceLossTesting = true;
                auto dev = take(gfx::createDevice(d), "create opt-in loss device", totals);
                check(dev->caps().deviceLossInjection, "opt-in deviceLossInjection == true", totals);

                // Thread affinity on removeForTesting().
                bool wrongThread = false;
                std::thread worker([&] {
                    auto r = dev->removeForTesting();
                    wrongThread = !r && r.error().code == gfx::ErrorCode::InvalidState;
                });
                worker.join();
                check(wrongThread, "removeForTesting enforces creating-thread affinity", totals);
                ++totals.rejections;

                auto oldBuf = take(dev->createBuffer({256, gfx::Memory::Device}), "create old buffer", totals);
                auto oldTex = take(dev->createTexture({32, 32, gfx::Format::RGBA8UNorm}), "create old texture", totals);
                const auto bcSeq = take(dev->recordBreadcrumb("vulkan_pre_loss_marker", gfx::QueueType::Graphics, 42), "recordBreadcrumb", totals);

                // Inject controlled synthetic device loss.
                take(dev->removeForTesting(), "inject controlled synthetic Vulkan loss", totals);

                // Full error coverage after loss.
                auto st = dev->checkStatus();
                expectError(st, gfx::ErrorCode::DeviceLost, "checkStatus after loss", totals);
                check(st.error().nativeCode != 0, "DeviceLost preserves native VK_ERROR_DEVICE_LOST code", totals);
                expectError(dev->createBuffer({256}), gfx::ErrorCode::DeviceLost, "createBuffer after loss", totals);
                expectError(dev->createTexture({16, 16, gfx::Format::RGBA8UNorm}), gfx::ErrorCode::DeviceLost, "createTexture after loss", totals);
                expectError(dev->createCommandList(), gfx::ErrorCode::DeviceLost, "createCommandList after loss", totals);
                expectError(dev->createTimelineFence(0), gfx::ErrorCode::DeviceLost, "createTimelineFence after loss", totals);
                expectError(dev->queryFormatSupport(gfx::Format::RGBA8UNorm), gfx::ErrorCode::DeviceLost, "queryFormatSupport after loss", totals);
                expectError(dev->queryMemoryBudget(gfx::MemorySegment::Local), gfx::ErrorCode::DeviceLost, "queryMemoryBudget after loss", totals);
                expectError(dev->resourceStatistics(), gfx::ErrorCode::DeviceLost, "resourceStatistics after loss", totals);
                expectError(dev->removeForTesting(), gfx::ErrorCode::DeviceLost, "second removeForTesting after loss", totals);

                // Crash breadcrumbs preserved after DeviceLost.
                const auto diag = dev->diagnosticReport();
                check(diag.deviceLost, "diagnosticReport reports deviceLost=true", totals);
                bool foundMarker = false;
                for (const auto& b : diag.breadcrumbs) {
                    if (b.label == "vulkan_pre_loss_marker" && b.sequence == bcSeq && !b.completed) foundMarker = true;
                }
                check(foundMarker, "incomplete pre-loss breadcrumb preserved after DeviceLost", totals);

                // Recover to a fresh healthy generation on exact same adapter.
                auto fresh = take(dev->recover(), "recover fresh Vulkan generation", totals);
                check(fresh->caps().adapterId == a.id, "recovered device matches exact adapterId", totals);
                check(fresh->caps().deviceLossInjection, "recovered device preserves opt-in flag", totals);
                take(fresh->checkStatus(), "fresh checkStatus healthy", totals);
                expectError(dev->recover(), gfx::ErrorCode::InvalidState, "second recover on retired generation", totals);

                // Cross-generation old resource must be rejected as WrongDevice on fresh generation.
                auto freshCmd = take(fresh->createCommandList(), "fresh cmd", totals);
                take(freshCmd->begin(), "fresh cmd begin", totals);
                expectError(freshCmd->transition(oldBuf, gfx::ResourceState::CopySource), gfx::ErrorCode::WrongDevice, "old buffer rejected on fresh generation", totals);
                expectError(freshCmd->transition(oldTex, gfx::ResourceState::CopySource), gfx::ErrorCode::WrongDevice, "old texture rejected on fresh generation", totals);
                take(freshCmd->end(), "fresh cmd end", totals);

                // Run real GPU workload on recovered generation.
                verifyPostRecoveryWorkload(*fresh, tag, totals);
                if (validation) {
                    const auto v = take(fresh->validationErrorCount(), "fresh validationErrorCount", totals);
                    check(v == 0, "fresh validationErrorCount == 0", totals);
                    std::cout << "  [" << tag << "] post-recovery validationErrorCount=0\n";
                }
                std::cout << "  [" << tag << "] opt-out + synthetic loss + error coverage + recovery 64x64: PASS\n";
            }
            ++totals.adapters;
        }
        check(totals.adapters > 0, "Expected at least one physical Vulkan adapter", totals);
        std::cout << "PASS gfx_vulkan_loss_injection_tests: adapters=" << totals.adapters
                  << " assertions=" << totals.assertions
                  << " rejections=" << totals.rejections << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
