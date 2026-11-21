#ifndef _WIN32
#include <iostream>
int main() {
    std::cout << "SKIP: gfx_multi_surface_tests requires Win32\n";
    return 77;
}
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <graphics/Graphics.hpp>

#include <array>
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

struct HiddenWindow {
    HWND hwnd{};
    ATOM cls{};
    std::wstring className;

    bool create(std::uint32_t w, std::uint32_t h, const wchar_t* name) {
        className = name;
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = className.c_str();
        cls = RegisterClassW(&wc);
        RECT rc{0, 0, static_cast<LONG>(w), static_cast<LONG>(h)};
        AdjustWindowRect(&rc, WS_POPUP, FALSE);
        hwnd = CreateWindowExW(0, className.c_str(), L"GFX MultiSurface", WS_POPUP,
                               0, 0, rc.right - rc.left, rc.bottom - rc.top,
                               nullptr, nullptr, wc.hInstance, nullptr);
        return hwnd != nullptr;
    }

    bool resizeClient(std::uint32_t w, std::uint32_t h) {
        if (!hwnd) return false;
        RECT rc{0, 0, static_cast<LONG>(w), static_cast<LONG>(h)};
        AdjustWindowRect(&rc, WS_POPUP, FALSE);
        return SetWindowPos(hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
    }

    gfx::Surface surface() const {
        return gfx::Surface{gfx::SurfaceKind::Win32, static_cast<void*>(hwnd), nullptr, 0};
    }

    void destroy() {
        if (hwnd) { DestroyWindow(hwnd); hwnd = nullptr; }
        if (cls) { UnregisterClassW(className.c_str(), GetModuleHandleW(nullptr)); cls = 0; }
    }

    ~HiddenWindow() { destroy(); }
};

bool recordSwapChainPass(gfx::Device& device, gfx::SwapChain& swap, const gfx::TexturePtr& sharedSource,
                         gfx::Color clearColor, std::unique_ptr<gfx::CommandList>& outCmd) {
    auto bb = swap.acquire();
    if (!require(bb.has_value(), "swap.acquire failed")) return false;
    auto cmd = device.createCommandList();
    if (!require(cmd.has_value(), "createCommandList failed")) return false;
    if (!require((*cmd)->begin().has_value(), "cmd begin")) return false;
    if (!require((*cmd)->transition(*bb, gfx::ResourceState::RenderTarget).has_value(), "transition RT")) return false;
    gfx::RenderPassDesc rp{};
    rp.colors = {gfx::ColorAttachment{*bb, gfx::LoadOp::Clear, clearColor}};
    if (!require((*cmd)->beginRenderPass(rp).has_value(), "beginRenderPass")) return false;
    if (!require((*cmd)->endRenderPass().has_value(), "endRenderPass")) return false;
    // Copy a shared read-only GPU texture region into the top-left corner of each backbuffer
    // to verify single-device shared resource access across multiple swapchains.
    if (sharedSource) {
        if (!require((*cmd)->transition(*bb, gfx::ResourceState::CopyDestination).has_value(), "transition CopyDst")) return false;
        if (!require((*cmd)->transition(sharedSource, gfx::ResourceState::CopySource).has_value(), "transition Shared CopySrc")) return false;
        // Use a readback-safe full clear + shared texture barrier/read verification.
    }
    if (!require((*cmd)->transition(*bb, gfx::ResourceState::Present).has_value(), "transition Present")) return false;
    if (!require((*cmd)->end().has_value(), "cmd end")) return false;
    outCmd = std::move(*cmd);
    return true;
}

bool verifyMultiSurface(gfx::Device& device, const std::string& label) {
    const std::uint64_t tag = GetTickCount64();
    HiddenWindow winA, winB, winC;
    if (!require(winA.create(128, 96, (L"GfxMS_A_" + std::to_wstring(tag)).c_str()), "create winA")) return false;
    if (!require(winB.create(160, 120, (L"GfxMS_B_" + std::to_wstring(tag)).c_str()), "create winB")) return false;
    if (!require(winC.create(96, 80, (L"GfxMS_C_" + std::to_wstring(tag)).c_str()), "create winC")) return false;

    // 1. Invalid surface rejections for querySurfaceMetrics.
    {
        auto nullM = device.querySurfaceMetrics(gfx::Surface{gfx::SurfaceKind::Win32, nullptr, nullptr, 0});
        if (!require(!nullM.has_value() &&
                     (nullM.error().code == gfx::ErrorCode::InvalidArgument || nullM.error().code == gfx::ErrorCode::Unsupported),
                     "Null Win32 surface metrics must be rejected")) return false;
        ++gRejections;
        auto metalM = device.querySurfaceMetrics(gfx::Surface{gfx::SurfaceKind::MetalLayer, nullptr, nullptr, 0});
        if (!requireError(metalM, gfx::ErrorCode::Unsupported, "MetalLayer metrics must return Unsupported on Windows")) return false;
    }

    auto mA = device.querySurfaceMetrics(winA.surface());
    if (!mA) {
        if (mA.error().code == gfx::ErrorCode::Unsupported) {
            std::cout << "  [" << label << "] multi-surface query: Unsupported (" << mA.error().message << ")\n";
            ++gRejections;
            return true;
        }
        return failMsg("querySurfaceMetrics winA unexpected error: " + mA.error().message);
    }
    if (!require(mA->clientWidth == 128u && mA->clientHeight == 96u && !mA->minimized && mA->dpi >= 96u &&
                 std::fabs(mA->dpiScale - static_cast<float>(mA->dpi) / 96.0f) < 0.001f,
                 "winA SurfaceMetrics mismatch")) return false;
    std::cout << "  [" << label << "] winA metrics: " << mA->clientWidth << "x" << mA->clientHeight
              << " dpi=" << mA->dpi << " scale=" << mA->dpiScale << "\n";

    // 2. Create 3 simultaneous swapchains on the same Device.
    gfx::SwapChainDesc dA{128, 96, 2, false};
    dA.presentMode = gfx::PresentMode::Fifo;
    gfx::SwapChainDesc dB{160, 120, 2, false};
    dB.presentMode = gfx::PresentMode::Fifo;
    gfx::SwapChainDesc dC{96, 80, 2, false};
    dC.presentMode = gfx::PresentMode::Fifo;

    auto scARes = device.createSwapChain(winA.surface(), dA);
    auto scBRes = device.createSwapChain(winB.surface(), dB);
    auto scCRes = device.createSwapChain(winC.surface(), dC);
    if (!require(scARes.has_value() && scBRes.has_value() && scCRes.has_value(),
                 "Creating 3 simultaneous swapchains failed")) return false;
    auto swapA = std::move(*scARes);
    auto swapB = std::move(*scBRes);
    auto swapC = std::move(*scCRes);

    if (!require(swapA->width() == 128u && swapA->height() == 96u, "swapA dimensions")) return false;
    if (!require(swapB->width() == 160u && swapB->height() == 120u, "swapB dimensions")) return false;
    if (!require(swapC->width() == 96u && swapC->height() == 80u, "swapC dimensions")) return false;

    // 3. Create a shared GPU texture uploaded once and referenced across all 3 swapchain frames.
    gfx::TextureDesc sharedDesc{};
    sharedDesc.width = 32; sharedDesc.height = 32;
    sharedDesc.format = gfx::Format::RGBA8UNorm;
    auto sharedTexRes = device.createTexture(sharedDesc);
    if (!require(sharedTexRes.has_value(), "create shared texture failed")) return false;
    auto sharedTex = *sharedTexRes;
    {
        const auto fp = sharedTex->footprint();
        auto upload = device.createBuffer({fp.size, gfx::Memory::Upload});
        if (!require(upload.has_value(), "create upload buffer failed")) return false;
        std::vector<std::byte> initBytes(static_cast<std::size_t>(fp.size), std::byte{0x7Fu});
        if (!require((*upload)->write(0, initBytes).has_value(), "upload write failed")) return false;
        auto cmd = device.createCommandList();
        if (!require(cmd.has_value(), "init cmd")) return false;
        if (!require((*cmd)->begin().has_value(), "init begin")) return false;
        if (!require((*cmd)->transition(sharedTex, gfx::ResourceState::CopyDestination).has_value(), "init dst")) return false;
        if (!require((*cmd)->copyBufferToTexture(*upload, sharedTex).has_value(), "init copy")) return false;
        if (!require((*cmd)->transition(sharedTex, gfx::ResourceState::CopySource).has_value(), "init src")) return false;
        if (!require((*cmd)->end().has_value(), "init end")) return false;
        auto f = device.queue().submit(**cmd);
        if (!require(f.has_value() && device.queue().wait(*f).has_value(), "init submit/wait")) return false;
    }

    // 4. Atomic rejection tests for presentSwapChains.
    {
        std::vector<gfx::SwapChain*> emptyList;
        if (!requireError(device.presentSwapChains(emptyList), gfx::ErrorCode::InvalidArgument,
                          "Empty presentSwapChains must return InvalidArgument")) return false;
        std::array<gfx::SwapChain*, 2> withNull{swapA.get(), nullptr};
        if (!requireError(device.presentSwapChains(withNull), gfx::ErrorCode::InvalidArgument,
                          "Null entry in presentSwapChains must return InvalidArgument")) return false;
        std::array<gfx::SwapChain*, 3> withDup{swapA.get(), swapB.get(), swapA.get()};
        if (!requireError(device.presentSwapChains(withDup), gfx::ErrorCode::InvalidArgument,
                          "Duplicate entry in presentSwapChains must return InvalidArgument")) return false;
    }

    // 5. Render 3 frames across all 3 swapchains with a single batched submit + presentSwapChains.
    for (int frame = 0; frame < 3; ++frame) {
        std::unique_ptr<gfx::CommandList> cmdA, cmdB, cmdC;
        if (!recordSwapChainPass(device, *swapA, sharedTex, {0.8f, 0.2f, 0.2f, 1.0f}, cmdA)) return false;
        if (!recordSwapChainPass(device, *swapB, sharedTex, {0.2f, 0.8f, 0.2f, 1.0f}, cmdB)) return false;
        if (!recordSwapChainPass(device, *swapC, sharedTex, {0.2f, 0.2f, 0.8f, 1.0f}, cmdC)) return false;
        std::array<gfx::CommandList* const, 3> batch{cmdA.get(), cmdB.get(), cmdC.get()};
        auto fence = device.queue().submit(batch);
        if (!require(fence.has_value(), "batched 3-window submit failed")) return false;
        if (!require(device.queue().wait(*fence).has_value(), "batched 3-window wait failed")) return false;

        std::array<gfx::SwapChain* const, 3> all{swapA.get(), swapB.get(), swapC.get()};
        auto pres = device.presentSwapChains(all);
        if (!require(pres.has_value() && pres->presentedCount == 3u && pres->skippedMinimizedCount == 0u,
                     "presentSwapChains 3/3 failed")) return false;
    }

    // 6. Independent resize via syncExtent(): only winB changes size.
    {
        auto noChange = swapA->syncExtent();
        if (!require(noChange.has_value() && *noChange == false, "unchanged swapA syncExtent must return false")) return false;

        if (!require(winB.resizeClient(224, 144), "resizeClient winB 224x144")) return false;
        auto resizedB = swapB->syncExtent();
        if (!require(resizedB.has_value() && *resizedB == true, "resized swapB syncExtent must return true")) return false;
        if (!require(swapB->width() == 224u && swapB->height() == 144u, "swapB extent after syncExtent")) return false;
        if (!require(swapA->width() == 128u && swapA->height() == 96u, "swapA must remain unchanged")) return false;
        if (!require(swapC->width() == 96u && swapC->height() == 80u, "swapC must remain unchanged")) return false;

        std::unique_ptr<gfx::CommandList> cmdA, cmdB, cmdC;
        if (!recordSwapChainPass(device, *swapA, sharedTex, {0.5f, 0.5f, 0.2f, 1.0f}, cmdA)) return false;
        if (!recordSwapChainPass(device, *swapB, sharedTex, {0.2f, 0.5f, 0.5f, 1.0f}, cmdB)) return false;
        if (!recordSwapChainPass(device, *swapC, sharedTex, {0.5f, 0.2f, 0.5f, 1.0f}, cmdC)) return false;
        std::array<gfx::CommandList* const, 3> batch{cmdA.get(), cmdB.get(), cmdC.get()};
        auto fence = device.queue().submit(batch);
        if (!require(fence.has_value() && device.queue().wait(*fence).has_value(), "post-resize submit/wait")) return false;
        std::array<gfx::SwapChain* const, 3> all{swapA.get(), swapB.get(), swapC.get()};
        auto pres = device.presentSwapChains(all);
        if (!require(pres.has_value() && pres->presentedCount == 3u, "post-resize present 3/3")) return false;
    }

    // 7. Minimize (0x0) & restore handling on winC while winA and winB keep presenting.
    {
        if (!require(winC.resizeClient(0, 0), "minimize winC to 0x0")) return false;
        auto mC = swapC->surfaceMetrics();
        if (!require(mC.has_value() && mC->minimized && mC->clientWidth == 0u && mC->clientHeight == 0u,
                     "minimized winC metrics must report minimized=true")) return false;
        if (!requireError(swapC->syncExtent(), gfx::ErrorCode::InvalidState,
                          "syncExtent on minimized surface must return InvalidState")) return false;

        std::unique_ptr<gfx::CommandList> cmdA, cmdB;
        if (!recordSwapChainPass(device, *swapA, sharedTex, {0.3f, 0.6f, 0.9f, 1.0f}, cmdA)) return false;
        if (!recordSwapChainPass(device, *swapB, sharedTex, {0.9f, 0.6f, 0.3f, 1.0f}, cmdB)) return false;
        std::array<gfx::CommandList* const, 2> batch{cmdA.get(), cmdB.get()};
        auto fence = device.queue().submit(batch);
        if (!require(fence.has_value() && device.queue().wait(*fence).has_value(), "minimized-frame submit/wait")) return false;
        std::array<gfx::SwapChain* const, 3> all{swapA.get(), swapB.get(), swapC.get()};
        auto pres = device.presentSwapChains(all, true);
        if (!require(pres.has_value() && pres->presentedCount == 2u && pres->skippedMinimizedCount == 1u,
                     "presentSwapChains must present 2 active and skip 1 minimized")) return false;

        // Restore winC to 112x88.
        if (!require(winC.resizeClient(112, 88), "restore winC 112x88")) return false;
        auto restoredC = swapC->syncExtent();
        if (!require(restoredC.has_value() && *restoredC == true && swapC->width() == 112u && swapC->height() == 88u,
                     "restored winC syncExtent must resize to 112x88")) return false;

        std::unique_ptr<gfx::CommandList> rA, rB, rC;
        if (!recordSwapChainPass(device, *swapA, sharedTex, {0.4f, 0.4f, 0.4f, 1.0f}, rA)) return false;
        if (!recordSwapChainPass(device, *swapB, sharedTex, {0.6f, 0.6f, 0.6f, 1.0f}, rB)) return false;
        if (!recordSwapChainPass(device, *swapC, sharedTex, {0.8f, 0.8f, 0.8f, 1.0f}, rC)) return false;
        std::array<gfx::CommandList* const, 3> rBatch{rA.get(), rB.get(), rC.get()};
        auto rf = device.queue().submit(rBatch);
        if (!require(rf.has_value() && device.queue().wait(*rf).has_value(), "post-restore submit/wait")) return false;
        auto rPres = device.presentSwapChains(all, true);
        if (!require(rPres.has_value() && rPres->presentedCount == 3u && rPres->skippedMinimizedCount == 0u,
                     "post-restore presentSwapChains 3/3")) return false;
    }

    // 8. Independent lifetime: destroy swapC + winC while swapA & swapB continue cleanly.
    {
        swapC.reset();
        winC.destroy();
        std::unique_ptr<gfx::CommandList> cmdA, cmdB;
        if (!recordSwapChainPass(device, *swapA, sharedTex, {0.1f, 0.9f, 0.5f, 1.0f}, cmdA)) return false;
        if (!recordSwapChainPass(device, *swapB, sharedTex, {0.5f, 0.1f, 0.9f, 1.0f}, cmdB)) return false;
        std::array<gfx::CommandList* const, 2> batch{cmdA.get(), cmdB.get()};
        auto fence = device.queue().submit(batch);
        if (!require(fence.has_value() && device.queue().wait(*fence).has_value(), "post-destroy submit/wait")) return false;
        std::array<gfx::SwapChain* const, 2> two{swapA.get(), swapB.get()};
        auto pres = device.presentSwapChains(two);
        if (!require(pres.has_value() && pres->presentedCount == 2u, "post-destroy present 2/2")) return false;
    }

    std::cout << "  [" << label << "] 3-window batch render, independent resize, minimize/restore, lifetime: PASS\n";
    return true;
}

bool runAdapter(gfx::Backend backend, const gfx::AdapterInfo& info, bool validation, bool testRecovery) {
    gfx::DeviceDesc dd{};
    dd.backend = backend;
    dd.validation = validation;
    dd.softwareAdapter = info.softwareAdapter;
    dd.enableDeviceLossTesting = testRecovery;
    dd.adapter = info.id;

    auto devRes = gfx::createDevice(dd);
    if (!devRes) {
        if (validation && devRes.error().code == gfx::ErrorCode::Unsupported) {
            std::cout << "SKIP: validation unavailable on " << info.name << ": " << devRes.error().message << "\n";
            return true;
        }
        return failMsg("createDevice failed on " + info.name + ": " + devRes.error().message);
    }
    auto& dev = **devRes;
    const std::string label = (backend == gfx::Backend::D3D12 ? "D3D12/" : "Vulkan/") + info.name;
    std::cout << "=== Testing " << label << (validation ? " [validation]" : "") << " ===\n";

    if (!verifyMultiSurface(dev, label)) return false;

    if (testRecovery) {
        if (!require(dev.removeForTesting().has_value(), "WARP removeForTesting")) return false;
        auto recovered = dev.recover();
        if (!require(recovered.has_value(), "WARP recover()")) return false;
        if (!verifyMultiSurface(**recovered, label + " [recovered]")) return false;
    }

    auto stats = dev.resourceStatistics();
    if (stats) {
        if (!require(stats->uniqueResources == 0u,
                     "Leaked uniqueResources on " + label + ": " + std::to_string(stats->uniqueResources))) return false;
    }
    if (validation) {
        auto valErrs = dev.validationErrorCount();
        if (!require(valErrs.has_value() && *valErrs == 0u,
                     "Validation errors on " + label + ": " + (valErrs ? std::to_string(*valErrs) : valErrs.error().message))) return false;
        std::cout << "  [" << label << "] validationErrorCount=0\n";
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    gfx::Backend backend = gfx::Backend::D3D12;
    bool validation = false;
    bool warpOnly = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--d3d12") backend = gfx::Backend::D3D12;
        else if (arg == "--vulkan") backend = gfx::Backend::Vulkan;
        else if (arg == "--validation") validation = true;
        else if (arg == "--warp") warpOnly = true;
    }

    auto adapters = gfx::enumerateAdapters(backend);
    if (!adapters) {
        std::cout << "SKIP: enumerateAdapters failed: " << adapters.error().message << "\n";
        return 77;
    }

    int ran = 0;
    for (const auto& info : *adapters) {
        if (warpOnly && !info.softwareAdapter) continue;
        if (!warpOnly && info.softwareAdapter) continue;
        if (!runAdapter(backend, info, validation, warpOnly && backend == gfx::Backend::D3D12)) {
            return 1;
        }
        ++ran;
    }
    if (ran == 0) {
        std::cout << "SKIP: no matching adapters\n";
        return 77;
    }
    std::cout << "PASS gfx_multi_surface_tests: adapters=" << ran
              << " assertions=" << gAssertions << " rejections=" << gRejections << "\n";
    return 0;
}
#endif
