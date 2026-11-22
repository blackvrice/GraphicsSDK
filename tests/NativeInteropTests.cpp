#ifndef _WIN32
#include <iostream>
int main() {
    std::cout << "SKIP: gfx_native_interop_tests currently requires Win32 NT handles\n";
    return 77;
}
#else
#include <graphics/Graphics.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

int gAssertions = 0;
int gRejections = 0;

bool failMsg(const std::string& msg) {
    std::cerr << "FAIL: " << msg << "\n";
    return false;
}

bool require(bool cond, const std::string& msg) {
    ++gAssertions;
    if (!cond) return failMsg(msg);
    return true;
}

template<class T>
bool requireError(const gfx::Result<T>& r, gfx::ErrorCode expected, const std::string& msg) {
    ++gAssertions;
    if (r) return failMsg(msg + " (expected error, got success)");
    if (r.error().code != expected) {
        return failMsg(msg + " (unexpected error code: " + std::to_string(static_cast<int>(r.error().code)) +
                       ", msg=" + r.error().message + ")");
    }
    ++gRejections;
    return true;
}

std::vector<std::byte> makePattern(std::size_t size, std::uint8_t seed) {
    std::vector<std::byte> out(size);
    for (std::size_t i = 0; i < size; ++i) {
        out[i] = static_cast<std::byte>((i * 31u + seed * 17u + 13u) & 0xFFu);
    }
    return out;
}

bool uploadBuffer(gfx::Device& dev, const gfx::BufferPtr& dst, const std::vector<std::byte>& data,
                  const gfx::TimelineFencePtr& signalFence = nullptr, std::uint64_t signalValue = 0) {
    auto up = dev.createBuffer({data.size(), gfx::Memory::Upload});
    if (!require(up.has_value(), "create upload")) return false;
    if (!require((*up)->write(0, data).has_value(), "write upload")) return false;
    auto cmd = dev.createCommandList();
    if (!require(cmd.has_value(), "create cmd")) return false;
    if (!require((*cmd)->begin().has_value(), "begin")) return false;
    if (!require((*cmd)->transition(dst, gfx::ResourceState::CopyDestination).has_value(), "tr CopyDst")) return false;
    if (!require((*cmd)->copyBuffer(*up, dst, data.size()).has_value(), "copyBuffer")) return false;
    if (!require((*cmd)->transition(dst, gfx::ResourceState::Common).has_value(), "tr Common")) return false;
    if (!require((*cmd)->end().has_value(), "end")) return false;

    gfx::CommandList* list = cmd->get();
    gfx::SubmitDesc sd{};
    sd.commandLists = {&list, 1};
    gfx::TimelineSignal sig{signalFence, signalValue};
    if (signalFence) {
        sd.signals = {&sig, 1};
    }
    auto f = dev.queue().submit(sd);
    if (!require(f.has_value(), "submit upload")) return false;
    if (!require(dev.queue().wait(*f).has_value(), "wait upload")) return false;
    return true;
}

bool readbackAndVerifyBuffer(gfx::Device& dev, const gfx::BufferPtr& src, const std::vector<std::byte>& expected,
                             const gfx::TimelineFencePtr& waitFence = nullptr, std::uint64_t waitValue = 0) {
    if (waitFence) {
        if (!require(waitFence->wait(waitValue).has_value(), "wait shared timeline fence")) return false;
    }
    auto rb = dev.createBuffer({expected.size(), gfx::Memory::Readback});
    if (!require(rb.has_value(), "create readback")) return false;
    auto cmd = dev.createCommandList();
    if (!require(cmd.has_value(), "create rb cmd")) return false;
    if (!require((*cmd)->begin().has_value(), "rb begin")) return false;
    if (!require((*cmd)->transition(src, gfx::ResourceState::CopySource).has_value(), "rb tr CopySrc")) return false;
    if (!require((*cmd)->copyBuffer(src, *rb, expected.size()).has_value(), "rb copyBuffer")) return false;
    if (!require((*cmd)->transition(src, gfx::ResourceState::Common).has_value(), "rb tr Common")) return false;
    if (!require((*cmd)->end().has_value(), "rb end")) return false;
    auto f = dev.queue().submit(**cmd);
    if (!require(f.has_value() && dev.queue().wait(*f).has_value(), "rb submit/wait")) return false;
    auto bytes = (*rb)->read(0, expected.size());
    if (!require(bytes.has_value() && bytes->size() == expected.size(), "rb read")) return false;
    return require(*bytes == expected, "Shared buffer 64KiB byte-for-byte mismatch");
}

bool verifySameApiInterop(gfx::Backend backend, const gfx::AdapterInfo& info, bool validation) {
    gfx::DeviceDesc dd{};
    dd.backend = backend;
    dd.validation = validation;
    dd.softwareAdapter = info.softwareAdapter;
    dd.adapter = info.id;

    auto devARes = gfx::createDevice(dd);
    auto devBRes = gfx::createDevice(dd);
    if (!devARes || !devBRes) {
        if (validation && ((!devARes && devARes.error().code == gfx::ErrorCode::Unsupported) ||
                           (!devBRes && devBRes.error().code == gfx::ErrorCode::Unsupported))) {
            std::cout << "SKIP: validation unavailable on " << info.name << "\n";
            return true;
        }
        return failMsg("createDevice pair failed on " + info.name);
    }
    auto& devA = **devARes;
    auto& devB = **devBRes;
    const std::string label = (backend == gfx::Backend::D3D12 ? "D3D12/" : "Vulkan/") + info.name;
    std::cout << "=== Same-API Interop " << label << (validation ? " [validation]" : "") << " ===\n";

    if (!require(devA.caps().externalMemoryInterop && devA.caps().externalFenceInterop,
                 "Expected externalMemoryInterop & externalFenceInterop on " + label)) return false;

    // 1. Non-owning NativeHandle queries & negative contracts.
    {
        auto hDev = devA.nativeDeviceHandle();
        if (!require(hDev.has_value() && static_cast<bool>(*hDev) && hDev->backend == backend, "nativeDeviceHandle")) return false;

        auto nonSharedBuf = devA.createBuffer({1024u, gfx::Memory::Device});
        auto nonSharedTex = devA.createTexture({32u, 32u, gfx::Format::RGBA8UNorm, true});
        auto nonSharedFence = devA.createTimelineFence(0u);
        if (!require(nonSharedBuf.has_value() && nonSharedTex.has_value() && nonSharedFence.has_value(), "create non-shared")) return false;

        auto hBuf = devA.nativeBufferHandle(*nonSharedBuf);
        auto hTex = devA.nativeTextureHandle(*nonSharedTex);
        auto hFen = devA.nativeTimelineFenceHandle(*nonSharedFence);
        if (!require(hBuf.has_value() && static_cast<bool>(*hBuf), "nativeBufferHandle")) return false;
        if (!require(hTex.has_value() && static_cast<bool>(*hTex), "nativeTextureHandle")) return false;
        if (!require(hFen.has_value() && static_cast<bool>(*hFen), "nativeTimelineFenceHandle")) return false;

        // WrongDevice & InvalidArgument & InvalidState & Unsupported checks.
        if (!requireError(devB.nativeBufferHandle(*nonSharedBuf), gfx::ErrorCode::WrongDevice, "cross-device nativeBufferHandle")) return false;
        if (!requireError(devA.nativeBufferHandle(nullptr), gfx::ErrorCode::InvalidArgument, "null nativeBufferHandle")) return false;
        if (!requireError(devA.exportBufferHandle(*nonSharedBuf), gfx::ErrorCode::InvalidState, "export non-shared buffer")) return false;
        if (!requireError(devA.exportTextureHandle(*nonSharedTex), gfx::ErrorCode::InvalidState, "export non-shared texture")) return false;
        if (!requireError(devA.exportTimelineFenceHandle(*nonSharedFence), gfx::ErrorCode::InvalidState, "export non-shared fence")) return false;

        gfx::BufferDesc badShared{1024u, gfx::Memory::Upload, false, true};
        if (!requireError(devA.createBuffer(badShared), gfx::ErrorCode::Unsupported, "shared Upload buffer")) return false;

        gfx::ExternalHandle emptyH{};
        if (!requireError(devB.importBufferHandle({1024u, gfx::Memory::Device}, emptyH), gfx::ErrorCode::InvalidArgument, "import empty handle")) return false;
        gfx::ExternalHandle fdH{gfx::ExternalHandleType::OpaqueFd, 1u};
        if (!requireError(devB.importBufferHandle({1024u, gfx::Memory::Device}, fdH), gfx::ErrorCode::Unsupported, "import OpaqueFd on Win32")) return false;
        (void)fdH.release(); // Synthetic sentinel value; do not close.
    }

    // 2. Shared 64 KiB Buffer + Shared Timeline Fence between Device A and Device B.
    {
        constexpr std::size_t kBytes = 65536u;
        gfx::BufferDesc sharedDesc{kBytes, gfx::Memory::Device, false, true};
        auto bufA = devA.createBuffer(sharedDesc);
        if (!require(bufA.has_value(), "create shared buffer A")) return false;
        auto bufHandle = devA.exportBufferHandle(*bufA);
        if (!require(bufHandle.has_value() && static_cast<bool>(*bufHandle), "export shared buffer A")) return false;
        auto bufB = devB.importBufferHandle(sharedDesc, *bufHandle);
        if (!require(bufB.has_value(), "import shared buffer B")) return false;

        auto fenceA = devA.createSharedTimelineFence(0u);
        if (!require(fenceA.has_value(), "create shared fence A")) return false;
        auto fenceHandle = devA.exportTimelineFenceHandle(*fenceA);
        if (!require(fenceHandle.has_value() && static_cast<bool>(*fenceHandle), "export shared fence A")) return false;
        auto fenceB = devB.importTimelineFenceHandle(*fenceHandle, 0u);
        if (!require(fenceB.has_value(), "import shared fence B")) return false;

        const auto pat1 = makePattern(kBytes, 0x3Au);
        if (!uploadBuffer(devA, *bufA, pat1, *fenceA, 1u)) return false;
        if (!readbackAndVerifyBuffer(devB, *bufB, pat1, *fenceB, 1u)) return false;

        // Reverse direction: Device B writes -> signals value 2 -> Device A waits and verifies.
        const auto pat2 = makePattern(kBytes, 0xA5u);
        if (!uploadBuffer(devB, *bufB, pat2, *fenceB, 2u)) return false;
        if (!readbackAndVerifyBuffer(devA, *bufA, pat2, *fenceA, 2u)) return false;
    }

    // 3. Shared 64x64 RGBA8UNorm Texture between Device A and Device B.
    {
        gfx::TextureDesc td{};
        td.width = 64; td.height = 64;
        td.format = gfx::Format::RGBA8UNorm;
        td.renderTarget = true;
        td.shared = true;
        auto texA = devA.createTexture(td);
        if (!require(texA.has_value(), "create shared texture A")) return false;
        auto texHandle = devA.exportTextureHandle(*texA);
        if (!require(texHandle.has_value() && static_cast<bool>(*texHandle), "export shared texture A")) return false;
        auto texB = devB.importTextureHandle(td, *texHandle);
        if (!require(texB.has_value(), "import shared texture B")) return false;

        // Device A clears shared texture to (64, 128, 192, 255).
        {
            auto cmd = devA.createCommandList();
            if (!require(cmd.has_value() && (*cmd)->begin().has_value(), "texA begin")) return false;
            if (!require((*cmd)->transition(*texA, gfx::ResourceState::RenderTarget).has_value(), "texA RT")) return false;
            gfx::RenderPassDesc rp{};
            rp.colors = {gfx::ColorAttachment{*texA, gfx::LoadOp::Clear, {64.0f / 255.0f, 128.0f / 255.0f, 192.0f / 255.0f, 1.0f}}};
            if (!require((*cmd)->beginRenderPass(rp).has_value() && (*cmd)->endRenderPass().has_value(), "texA RP")) return false;
            if (!require((*cmd)->transition(*texA, gfx::ResourceState::Common).has_value(), "texA Common")) return false;
            if (!require((*cmd)->end().has_value(), "texA end")) return false;
            auto f = devA.queue().submit(**cmd);
            if (!require(f.has_value() && devA.queue().wait(*f).has_value(), "texA wait")) return false;
        }
        // Device B reads back all 64x64 = 4,096 pixels from the imported texture.
        {
            const auto fp = (*texB)->footprint();
            auto rb = devB.createBuffer({fp.size, gfx::Memory::Readback});
            auto cmd = devB.createCommandList();
            if (!require(rb.has_value() && cmd.has_value() && (*cmd)->begin().has_value(), "texB begin")) return false;
            if (!require((*cmd)->transition(*texB, gfx::ResourceState::CopySource).has_value(), "texB CopySrc")) return false;
            if (!require((*cmd)->copyTextureToBuffer(*texB, *rb).has_value(), "texB copy")) return false;
            if (!require((*cmd)->end().has_value(), "texB end")) return false;
            auto f = devB.queue().submit(**cmd);
            if (!require(f.has_value() && devB.queue().wait(*f).has_value(), "texB wait")) return false;
            auto bytes = (*rb)->read(0, fp.size);
            if (!require(bytes.has_value() && bytes->size() == fp.size, "texB read")) return false;
            for (std::uint32_t y = 0; y < 64u; ++y) {
                const auto* row = reinterpret_cast<const std::uint8_t*>(bytes->data() + static_cast<std::size_t>(y) * fp.rowPitch);
                for (std::uint32_t x = 0; x < 64u; ++x) {
                    if (!require(std::abs(int(row[x * 4 + 0]) - 64) <= 1 &&
                                 std::abs(int(row[x * 4 + 1]) - 128) <= 1 &&
                                 std::abs(int(row[x * 4 + 2]) - 192) <= 1 &&
                                 row[x * 4 + 3] == 255, "Shared texture pixel mismatch")) return false;
                }
            }
        }
    }

    auto sA = devA.resourceStatistics();
    auto sB = devB.resourceStatistics();
    if (!require(sA.has_value() && sA->uniqueResources == 0u && sB.has_value() && sB->uniqueResources == 0u,
                 "Zero leaked resources on both devices")) return false;
    if (validation) {
        auto eA = devA.validationErrorCount();
        auto eB = devB.validationErrorCount();
        if (!require(eA.has_value() && *eA == 0u && eB.has_value() && *eB == 0u,
                     "Validation error count must be 0 on both devices")) return false;
        std::cout << "  [" << label << "] validationErrorCount=0 (devA & devB)\n";
    }
    std::cout << "  [" << label << "] 64KiB buffer + 64x64 texture + timeline fence same-API interop: PASS\n";
    return true;
}

bool verifyCrossApiD3D12VulkanInterop(bool validation) {
    auto dxAdapters = gfx::enumerateAdapters(gfx::Backend::D3D12);
    auto vkAdapters = gfx::enumerateAdapters(gfx::Backend::Vulkan);
    if (!dxAdapters || !vkAdapters) {
        std::cout << "SKIP: Cross-API interop requires both D3D12 and Vulkan adapters\n";
        return true;
    }

    int testedPairs = 0;
    for (const auto& dxInfo : *dxAdapters) {
        if (dxInfo.softwareAdapter) continue;
        const gfx::AdapterInfo* matchedVk = nullptr;
        for (const auto& vkInfo : *vkAdapters) {
            if (!vkInfo.softwareAdapter && vkInfo.vendorId == dxInfo.vendorId && vkInfo.deviceId == dxInfo.deviceId) {
                matchedVk = &vkInfo;
                break;
            }
        }
        if (!matchedVk) continue;

        gfx::DeviceDesc dxDesc{gfx::Backend::D3D12, validation, false, false, dxInfo.id};
        gfx::DeviceDesc vkDesc{gfx::Backend::Vulkan, validation, false, false, matchedVk->id};
        auto dxRes = gfx::createDevice(dxDesc);
        auto vkRes = gfx::createDevice(vkDesc);
        if (!dxRes || !vkRes) {
            if (validation) {
                std::cout << "SKIP: Cross-API validation unavailable on " << dxInfo.name << "\n";
                continue;
            }
            return failMsg("Cross-API createDevice failed on " + dxInfo.name);
        }
        auto& dx = **dxRes;
        auto& vk = **vkRes;
        std::cout << "=== Cross-API D3D12 <-> Vulkan Interop on " << dxInfo.name
                  << (validation ? " [validation]" : "") << " ===\n";

        if (!vk.caps().externalMemoryInterop || !vk.caps().externalFenceInterop) {
            std::cout << "  [" << dxInfo.name << "] Vulkan external interop unsupported; skipping\n";
            continue;
        }

        // D3D12 exports shared 64 KiB buffer + shared timeline fence; Vulkan imports both!
        {
            constexpr std::size_t kBytes = 65536u;
            gfx::BufferDesc sharedDesc{kBytes, gfx::Memory::Device, false, true};
            auto dxBuf = dx.createBuffer(sharedDesc);
            if (!require(dxBuf.has_value(), "D3D12 create shared buffer")) return false;
            auto bufH = dx.exportBufferHandle(*dxBuf);
            if (!require(bufH.has_value(), "D3D12 export buffer NT handle")) return false;
            auto vkBuf = vk.importBufferHandle(sharedDesc, *bufH);
            if (!require(vkBuf.has_value(), "Vulkan import D3D12 buffer NT handle")) return false;

            auto dxFence = dx.createSharedTimelineFence(0u);
            if (!require(dxFence.has_value(), "D3D12 create shared fence")) return false;
            auto fenceH = dx.exportTimelineFenceHandle(*dxFence);
            if (!require(fenceH.has_value(), "D3D12 export fence NT handle")) return false;
            auto vkFence = vk.importTimelineFenceHandle(*fenceH, 0u);
            if (!require(vkFence.has_value(), "Vulkan import D3D12 fence NT handle")) return false;

            // Direction 1: D3D12 writes 64 KiB -> signals fence=1 -> Vulkan waits and verifies all 65,536 bytes!
            const auto patD3D = makePattern(kBytes, 0x55u);
            if (!uploadBuffer(dx, *dxBuf, patD3D, *dxFence, 1u)) return false;
            if (!readbackAndVerifyBuffer(vk, *vkBuf, patD3D, *vkFence, 1u)) return false;

            // Direction 2: Vulkan writes 64 KiB -> signals fence=2 -> D3D12 waits and verifies all 65,536 bytes!
            const auto patVk = makePattern(kBytes, 0xCCu);
            if (!uploadBuffer(vk, *vkBuf, patVk, *vkFence, 2u)) return false;
            if (!readbackAndVerifyBuffer(dx, *dxBuf, patVk, *dxFence, 2u)) return false;
        }

        auto sDx = dx.resourceStatistics();
        auto sVk = vk.resourceStatistics();
        if (!require(sDx.has_value() && sDx->uniqueResources == 0u && sVk.has_value() && sVk->uniqueResources == 0u,
                     "Zero leaked cross-API resources")) return false;
        if (validation) {
            auto eDx = dx.validationErrorCount();
            auto eVk = vk.validationErrorCount();
            if (!require(eDx.has_value() && *eDx == 0u && eVk.has_value() && *eVk == 0u,
                         "Cross-API validationErrorCount must be 0")) return false;
            std::cout << "  [" << dxInfo.name << "] cross-API validationErrorCount=0 (D3D12 & Vulkan)\n";
        }
        std::cout << "  [" << dxInfo.name << "] D3D12 <-> Vulkan 64KiB shared buffer + timeline fence: PASS\n";
        ++testedPairs;
    }
    return require(testedPairs >= 1, "At least one physical GPU must pass Cross-API D3D12 <-> Vulkan interop");
}

} // namespace

int main(int argc, char** argv) {
    bool runCrossApi = false;
    gfx::Backend backend = gfx::Backend::D3D12;
    bool validation = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--d3d12") backend = gfx::Backend::D3D12;
        else if (arg == "--vulkan") backend = gfx::Backend::Vulkan;
        else if (arg == "--cross-api") runCrossApi = true;
        else if (arg == "--validation") validation = true;
    }

    if (runCrossApi) {
        if (!verifyCrossApiD3D12VulkanInterop(validation)) return 1;
        std::cout << "PASS gfx_native_interop_tests (cross-api): assertions=" << gAssertions
                  << " rejections=" << gRejections << "\n";
        return 0;
    }

    auto adapters = gfx::enumerateAdapters(backend);
    if (!adapters) {
        std::cout << "SKIP: enumerateAdapters failed: " << adapters.error().message << "\n";
        return 77;
    }
    int ran = 0;
    for (const auto& info : *adapters) {
        if (info.softwareAdapter) continue;
        if (!verifySameApiInterop(backend, info, validation)) return 1;
        ++ran;
    }
    if (ran == 0) {
        std::cout << "SKIP: no matching hardware adapters\n";
        return 77;
    }
    std::cout << "PASS gfx_native_interop_tests: adapters=" << ran
              << " assertions=" << gAssertions << " rejections=" << gRejections << "\n";
    return 0;
}
#endif
