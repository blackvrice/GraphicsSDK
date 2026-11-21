#include <graphics/Graphics.hpp>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
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

const char* presentModeName(gfx::PresentMode m) {
    switch (m) {
    case gfx::PresentMode::Fifo: return "Fifo";
    case gfx::PresentMode::Immediate: return "Immediate";
    case gfx::PresentMode::Mailbox: return "Mailbox";
    case gfx::PresentMode::Tearing: return "Tearing";
    }
    return "Unknown";
}

const char* pacingModeName(gfx::FramePacingMode m) {
    switch (m) {
    case gfx::FramePacingMode::Balanced: return "Balanced";
    case gfx::FramePacingMode::LowLatency: return "LowLatency";
    case gfx::FramePacingMode::Throughput: return "Throughput";
    }
    return "Unknown";
}

#ifdef _WIN32
struct HiddenWindow {
    HWND handle{};
    std::uint32_t width{};
    std::uint32_t height{};
    HiddenWindow(std::uint32_t w, std::uint32_t h) : width(w), height(h) {
        static const wchar_t* kClassName = L"GfxFramePacingTestWindow";
        static bool registered = false;
        if (!registered) {
            WNDCLASSW wc{};
            wc.lpfnWndProc = DefWindowProcW;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = kClassName;
            RegisterClassW(&wc);
            registered = true;
        }
        RECT rc{0, 0, static_cast<LONG>(w), static_cast<LONG>(h)};
        AdjustWindowRect(&rc, WS_POPUP, FALSE);
        handle = CreateWindowExW(
            0, kClassName, L"GfxFramePacingTest", WS_POPUP,
            CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!handle) throw std::runtime_error("CreateWindowExW failed");
    }
    ~HiddenWindow() {
        if (handle) DestroyWindow(handle);
    }
    gfx::Surface surface() const {
        return gfx::Surface{gfx::SurfaceKind::Win32, handle};
    }
    void resizeClient(std::uint32_t w, std::uint32_t h) {
        RECT rc{0, 0, static_cast<LONG>(w), static_cast<LONG>(h)};
        AdjustWindowRect(&rc, WS_POPUP, FALSE);
        SetWindowPos(handle, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        width = w;
        height = h;
    }
};

void renderFrames(gfx::Device& device, gfx::SwapChain& swap, int frames, Totals& totals) {
    auto cmd = take(device.createCommandList(), "create frame cmd", totals);
    for (int i = 0; i < frames; ++i) {
        take(swap.waitForNextFrame(std::chrono::seconds(5)), "waitForNextFrame", totals);
        auto back = take(swap.acquire(), "swap acquire", totals);
        take(cmd->begin(), "cmd begin", totals);
        take(cmd->transition(back, gfx::ResourceState::RenderTarget), "back -> RT", totals);
        const float t = static_cast<float>(i % 8) / 7.0f;
        take(cmd->beginRenderPass(back, gfx::Color{t, 0.25f, 1.0f - t, 1.0f}), "clear backbuffer", totals);
        take(cmd->endRenderPass(), "end pass", totals);
        take(cmd->transition(back, gfx::ResourceState::Present), "back -> Present", totals);
        take(cmd->end(), "cmd end", totals);
        const auto fence = take(device.queue().submit(*cmd), "submit frame", totals);
        back.reset();
        take(swap.present(), "swap present", totals);
        take(device.queue().wait(fence, std::chrono::seconds(10)), "wait frame fence", totals);
    }
}

void runAdapterSuite(gfx::Backend backend, const gfx::AdapterInfo& adapter, bool validation, Totals& totals) {
    gfx::DeviceDesc desc{};
    desc.backend = backend;
    desc.adapter = adapter.id;
    desc.softwareAdapter = adapter.softwareAdapter;
    desc.validation = validation;

    auto device = take(gfx::createDevice(desc), "createDevice", totals);
    const auto& caps = device->caps();

    HiddenWindow win(128u, 96u);
    const auto surface = win.surface();

    // Surface rejection checks
    reject(device->querySurfacePresentCapabilities(gfx::Surface{gfx::SurfaceKind::MetalLayer, win.handle}),
           gfx::ErrorCode::Unsupported, "MetalLayer rejected on Windows", totals);
    reject(device->querySurfacePresentCapabilities(gfx::Surface{gfx::SurfaceKind::Xcb, nullptr, nullptr, 1u}),
           gfx::ErrorCode::Unsupported, "Xcb rejected on Windows", totals);
    if (caps.swapChain) {
        reject(device->querySurfacePresentCapabilities(gfx::Surface{gfx::SurfaceKind::Win32, nullptr}),
               gfx::ErrorCode::InvalidArgument, "null Win32 HWND rejected", totals);
    } else {
        reject(device->querySurfacePresentCapabilities(surface),
               gfx::ErrorCode::Unsupported, "swapChain=false adapter rejects querySurfacePresentCapabilities", totals);
        std::uint64_t valErrors = 0;
        if (validation) {
            valErrors = take(device->validationErrorCount(), "validationErrorCount", totals);
            check(valErrors == 0u, "validation errors must be 0", totals);
        }
        std::cout << "PASS adapter=\"" << caps.adapterName << "\""
                  << " backend=" << (backend == gfx::Backend::D3D12 ? "D3D12" : "Vulkan")
                  << " validation=" << (validation ? "on" : "off")
                  << " val_errors=" << valErrors
                  << " swapchain=unsupported\n";
        return;
    }

    const auto presCaps = take(device->querySurfacePresentCapabilities(surface), "querySurfacePresentCapabilities", totals);
    check(presCaps.fifo, "FIFO present mode must be supported", totals);
    check(presCaps.frameLatencyWaitable, "frameLatencyWaitable must be true", totals);
    check(presCaps.minImageCount >= 2u && presCaps.maxImageCount >= presCaps.minImageCount, "valid image count bounds", totals);

    // 1. Test every PresentMode: supported -> run 6 frames & measure stats; unsupported -> reject Unsupported
    const std::array<std::pair<gfx::PresentMode, bool>, 4> modeTable{{
        {gfx::PresentMode::Fifo, presCaps.fifo},
        {gfx::PresentMode::Immediate, presCaps.immediate},
        {gfx::PresentMode::Mailbox, presCaps.mailbox},
        {gfx::PresentMode::Tearing, presCaps.tearing}
    }};

    for (const auto& [mode, supported] : modeTable) {
        gfx::SwapChainDesc sc{};
        sc.width = win.width;
        sc.height = win.height;
        sc.buffers = std::max<std::uint32_t>(2u, presCaps.minImageCount);
        sc.presentMode = mode;
        sc.pacingMode = gfx::FramePacingMode::LowLatency;

        if (!supported) {
            reject(device->createSwapChain(surface, sc), gfx::ErrorCode::Unsupported,
                   "unsupported present mode rejected", totals);
            continue;
        }
        auto swap = take(device->createSwapChain(surface, sc), "createSwapChain(mode)", totals);
        check(swap->presentMode() == mode, "active presentMode matches requested", totals);
        check(swap->pacingMode() == gfx::FramePacingMode::LowLatency, "pacingMode matches", totals);
        check(swap->maxFrameLatency() == 1u, "LowLatency default maxFrameLatency == 1", totals);

        const int frames = (mode == gfx::PresentMode::Fifo) ? 4 : 10;
        renderFrames(*device, *swap, frames, totals);
        const auto stats = take(swap->framePacingStats(), "framePacingStats", totals);
        check(stats.presentedFrames == static_cast<std::uint64_t>(frames), "presentedFrames count matches", totals);
        check(stats.activePresentMode == mode, "stats activePresentMode matches", totals);
        check(stats.averageFrameMilliseconds >= 0.0 && std::isfinite(stats.averageFrameMilliseconds), "avg frame ms finite", totals);
        check(stats.frameTimeStdDevMilliseconds >= 0.0 && std::isfinite(stats.frameTimeStdDevMilliseconds), "stddev ms finite", totals);
        check(stats.lastQueueLatencyMilliseconds >= 0.0 && std::isfinite(stats.lastQueueLatencyMilliseconds), "queue latency finite", totals);

        std::cout << "  MODE_METRIC adapter=\"" << caps.adapterName << "\""
                  << " backend=" << (backend == gfx::Backend::D3D12 ? "D3D12" : "Vulkan")
                  << " mode=" << presentModeName(mode)
                  << " frames=" << stats.presentedFrames
                  << " avg_ms=" << std::fixed << std::setprecision(3) << stats.averageFrameMilliseconds
                  << " stddev_ms=" << stats.frameTimeStdDevMilliseconds
                  << " queue_lat_ms=" << stats.lastQueueLatencyMilliseconds
                  << " stalls=" << stats.gpuStallCount << "\n";

        take(swap->resetFramePacingStats(), "resetFramePacingStats", totals);
        const auto cleared = take(swap->framePacingStats(), "framePacingStats after reset", totals);
        check(cleared.presentedFrames == 0u && cleared.gpuStallCount == 0u, "stats reset cleanly", totals);

        // Verify resize preserves swapchain flags (especially DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING + WAITABLE_OBJECT)
        if (mode == gfx::PresentMode::Tearing || mode == gfx::PresentMode::Immediate) {
            win.resizeClient(96u, 64u);
            take(swap->resize(96u, 64u), "swap resize preserves flags", totals);
            renderFrames(*device, *swap, 2, totals);
            win.resizeClient(128u, 96u);
            take(swap->resize(128u, 96u), "swap resize restore", totals);
        }
    }

    // 2. Test FramePacingMode presets: LowLatency (1), Balanced (2), Throughput (3)
    const gfx::PresentMode fastMode = presCaps.immediate ? gfx::PresentMode::Immediate : gfx::PresentMode::Fifo;
    const std::array<std::pair<gfx::FramePacingMode, std::uint32_t>, 3> pacingTable{{
        {gfx::FramePacingMode::LowLatency, 1u},
        {gfx::FramePacingMode::Balanced, 2u},
        {gfx::FramePacingMode::Throughput, 3u}
    }};
    for (const auto& [pMode, expectedLatency] : pacingTable) {
        gfx::SwapChainDesc sc{};
        sc.width = win.width;
        sc.height = win.height;
        sc.buffers = std::min<std::uint32_t>(3u, presCaps.maxImageCount);
        sc.presentMode = fastMode;
        sc.pacingMode = pMode;
        auto swap = take(device->createSwapChain(surface, sc), "createSwapChain(pacing)", totals);
        const std::uint32_t wantLat = std::min<std::uint32_t>(expectedLatency, sc.buffers);
        check(swap->maxFrameLatency() == wantLat, "pacing mode default maxFrameLatency matches", totals);
        renderFrames(*device, *swap, 8, totals);
        const auto stats = take(swap->framePacingStats(), "pacing stats", totals);
        check(stats.presentedFrames == 8u && stats.activePacingMode == pMode && stats.maxFrameLatency == wantLat, "pacing stats consistent", totals);
        std::cout << "  PACING_METRIC adapter=\"" << caps.adapterName << "\""
                  << " backend=" << (backend == gfx::Backend::D3D12 ? "D3D12" : "Vulkan")
                  << " pacing=" << pacingModeName(pMode)
                  << " max_lat=" << stats.maxFrameLatency
                  << " avg_ms=" << std::fixed << std::setprecision(3) << stats.averageFrameMilliseconds
                  << " stddev_ms=" << stats.frameTimeStdDevMilliseconds
                  << " queue_lat_ms=" << stats.lastQueueLatencyMilliseconds << "\n";
    }

    // 3. GPU stall detection threshold verification
    {
        gfx::SwapChainDesc sc{};
        sc.width = win.width;
        sc.height = win.height;
        sc.buffers = std::max<std::uint32_t>(2u, presCaps.minImageCount);
        sc.presentMode = fastMode;
        sc.stallThresholdMilliseconds = 0.0001; // 100 ns -> every real frame triggers stall detection
        auto swap = take(device->createSwapChain(surface, sc), "createSwapChain(stall detect)", totals);
        renderFrames(*device, *swap, 3, totals);
        const auto stats = take(swap->framePacingStats(), "stall stats", totals);
        check(stats.gpuStallCount >= 2u, "low stall threshold detects slow frames", totals);
    }

    // 4. Negative / Rejection contracts
    {
        gfx::SwapChainDesc badLat{win.width, win.height, 2u, false, fastMode, gfx::FramePacingMode::Balanced, 5u, 50.0};
        reject(device->createSwapChain(surface, badLat), gfx::ErrorCode::InvalidArgument, "maxFrameLatency > 4 rejected", totals);

        gfx::SwapChainDesc badStall{win.width, win.height, 2u, false, fastMode, gfx::FramePacingMode::Balanced, 1u, 0.0};
        reject(device->createSwapChain(surface, badStall), gfx::ErrorCode::InvalidArgument, "stallThreshold <= 0 rejected", totals);

        gfx::SwapChainDesc nanStall{win.width, win.height, 2u, false, fastMode, gfx::FramePacingMode::Balanced, 1u, std::numeric_limits<double>::quiet_NaN()};
        reject(device->createSwapChain(surface, nanStall), gfx::ErrorCode::InvalidArgument, "NaN stallThreshold rejected", totals);

        auto swap = take(device->createSwapChain(surface, {win.width, win.height, 2u, false, fastMode}), "swap for wait reject", totals);
        reject(swap->waitForNextFrame(std::chrono::milliseconds(-1)), gfx::ErrorCode::InvalidArgument, "negative wait timeout rejected", totals);
    }

    std::uint64_t valErrors = 0;
    if (validation) {
        valErrors = take(device->validationErrorCount(), "validationErrorCount", totals);
        check(valErrors == 0u, "validation errors must be 0", totals);
    }
    std::cout << "PASS adapter=\"" << caps.adapterName << "\""
              << " backend=" << (backend == gfx::Backend::D3D12 ? "D3D12" : "Vulkan")
              << " validation=" << (validation ? "on" : "off")
              << " val_errors=" << valErrors
              << " fifo=" << (presCaps.fifo ? "yes" : "no")
              << " immediate=" << (presCaps.immediate ? "yes" : "no")
              << " mailbox=" << (presCaps.mailbox ? "yes" : "no")
              << " tearing=" << (presCaps.tearing ? "yes" : "no")
              << " waitable=" << (presCaps.frameLatencyWaitable ? "yes" : "no")
              << "\n";
}

void runWarpLossRecovery(Totals& totals) {
    gfx::DeviceDesc desc{};
    desc.backend = gfx::Backend::D3D12;
    desc.softwareAdapter = true;
    desc.enableDeviceLossTesting = true;
    auto device = take(gfx::createDevice(desc), "create WARP loss device", totals);
    HiddenWindow win(64u, 64u);
    auto swap = take(device->createSwapChain(win.surface(), {64u, 64u, 2u, false, gfx::PresentMode::Immediate, gfx::FramePacingMode::LowLatency}), "WARP swap", totals);

    take(device->removeForTesting(), "WARP removeForTesting", totals);
    reject(device->querySurfacePresentCapabilities(win.surface()), gfx::ErrorCode::DeviceLost, "querySurfacePresentCapabilities after loss", totals);
    reject(swap->waitForNextFrame(std::chrono::milliseconds(100)), gfx::ErrorCode::DeviceLost, "waitForNextFrame after loss", totals);
    reject(swap->framePacingStats(), gfx::ErrorCode::DeviceLost, "framePacingStats after loss", totals);
    swap.reset();

    auto recovered = take(device->recover(), "WARP recover()", totals);
    const auto caps = take(recovered->querySurfacePresentCapabilities(win.surface()), "recovered querySurfacePresentCapabilities", totals);
    check(caps.fifo && caps.immediate && caps.frameLatencyWaitable, "recovered WARP surface caps valid", totals);
    auto freshSwap = take(recovered->createSwapChain(win.surface(), {64u, 64u, 2u, false, gfx::PresentMode::Immediate, gfx::FramePacingMode::LowLatency}), "recovered WARP swap", totals);
    renderFrames(*recovered, *freshSwap, 2, totals);
}
#endif
} // namespace

int main(int argc, char** argv) {
    try {
#ifndef _WIN32
        (void)argc; (void)argv;
        std::cout << "SKIP: Win32 surface required\n";
        return 77;
#else
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
#endif
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
