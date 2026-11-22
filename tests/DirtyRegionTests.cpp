#ifndef _WIN32
#include <iostream>
int main() {
    std::cout << "SKIP: gfx_dirty_region_tests requires Win32\n";
    return 77;
}
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <graphics/Graphics.hpp>
#include "fixtures/SpirvFixtures.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <span>
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

template<std::size_t N>
gfx::ShaderBinary spirvBinary(gfx::ShaderStage stage, const std::array<std::uint32_t, N>& words) {
    const auto raw = std::as_bytes(std::span{words});
    return {stage, std::vector<std::byte>(raw.begin(), raw.end()), gfx::ShaderFormat::SPIRV};
}

gfx::Result<gfx::PipelinePtr> makeRedPipeline(gfx::Device& device, gfx::Backend backend) {
    gfx::ShaderBinary vs{}, ps{};
    if (backend == gfx::Backend::D3D12) {
        constexpr std::string_view hlsl = R"(
float4 VS(uint id : SV_VertexID) : SV_Position {
    float2 p[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
    return float4(p[id % 3], 0.5, 1.0);
}
float4 PS() : SV_Target0 { return float4(1.0, 0.0, 0.0, 1.0); }
)";
        auto vsRes = gfx::compileHlsl(hlsl, "VS", gfx::ShaderStage::Vertex);
        if (!vsRes) return std::unexpected(vsRes.error());
        auto psRes = gfx::compileHlsl(hlsl, "PS", gfx::ShaderStage::Fragment);
        if (!psRes) return std::unexpected(psRes.error());
        vs = std::move(*vsRes);
        ps = std::move(*psRes);
    } else {
        vs = spirvBinary(gfx::ShaderStage::Vertex, fixtures::fullscreen);
        ps = spirvBinary(gfx::ShaderStage::Fragment, fixtures::red);
    }
    gfx::GraphicsPipelineDesc desc{vs, ps};
    desc.renderTargets = gfx::RenderTargetLayout{{gfx::Format::RGBA8UNorm}, std::nullopt};
    return device.createGraphicsPipeline(desc);
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
        hwnd = CreateWindowExW(0, className.c_str(), L"GFX DirtyRegion", WS_POPUP,
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

    ~HiddenWindow() {
        if (hwnd) DestroyWindow(hwnd);
        if (cls) UnregisterClassW(className.c_str(), GetModuleHandleW(nullptr));
    }
};

// Verifies untouched pixel preservation across:
// 1) initial full Clear (blue),
// 2) partial texture region copy (green in [16..80, 24..72]),
// 3) LoadOp::Load preserve + setViewport + setScissor draw (red in [120..184, 100..164]).
bool verifyDirtyRegionPreservation(gfx::Device& device, gfx::Backend backend, const std::string& label) {
    constexpr std::uint32_t W = 256, H = 256;
    auto pipeRes = makeRedPipeline(device, backend);
    if (!require(pipeRes.has_value(), "create red pipeline failed")) return false;
    auto targetRes = device.createTexture({W, H, gfx::Format::RGBA8UNorm, true});
    if (!require(targetRes.has_value(), "create 256x256 RT failed")) return false;
    auto target = *targetRes;

    // Prepare 64x48 green patch upload buffer for partial copy.
    constexpr std::uint32_t pX = 16, pY = 24, pW = 64, pH = 48;
    auto patchFpRes = gfx::computeTextureRegionFootprint(gfx::Format::RGBA8UNorm, pW, pH);
    if (!require(patchFpRes.has_value(), "computeTextureRegionFootprint failed")) return false;
    auto uploadRes = device.createBuffer({patchFpRes->size, gfx::Memory::Upload});
    if (!require(uploadRes.has_value(), "create patch upload buffer failed")) return false;
    std::vector<std::byte> patchData(static_cast<std::size_t>(patchFpRes->size), std::byte{0});
    for (std::uint32_t y = 0; y < pH; ++y) {
        auto* row = reinterpret_cast<std::uint8_t*>(patchData.data() + static_cast<std::size_t>(y) * patchFpRes->rowPitch);
        for (std::uint32_t x = 0; x < pW; ++x) {
            row[x * 4 + 0] = 30;
            row[x * 4 + 1] = 210;
            row[x * 4 + 2] = 60;
            row[x * 4 + 3] = 255;
        }
    }
    if (!require((*uploadRes)->write(0, patchData).has_value(), "write patch upload buffer")) return false;

    const auto fullFp = target->footprint();
    auto readbackRes = device.createBuffer({fullFp.size, gfx::Memory::Readback});
    if (!require(readbackRes.has_value(), "create readback buffer")) return false;

    constexpr std::uint32_t dX = 120, dY = 100, dW = 64, dH = 64;
    auto cmdRes = device.createCommandList();
    if (!require(cmdRes.has_value(), "createCommandList")) return false;
    auto& cmd = **cmdRes;
    if (!require(cmd.begin().has_value(), "cmd.begin")) return false;

    // Pass 1: initial full clear to blue (20, 40, 200, 255).
    if (!require(cmd.transition(target, gfx::ResourceState::RenderTarget).has_value(), "tr RT 1")) return false;
    gfx::RenderPassDesc clearPass{};
    clearPass.colors = {gfx::ColorAttachment{target, gfx::LoadOp::Clear, {20.0f / 255.0f, 40.0f / 255.0f, 200.0f / 255.0f, 1.0f}}};
    if (!require(cmd.beginRenderPass(clearPass).has_value(), "begin clear pass")) return false;
    if (!require(cmd.endRenderPass().has_value(), "end clear pass")) return false;

    // Pass 2: partial texture region copy into [16..80, 24..72].
    if (!require(cmd.transition(target, gfx::ResourceState::CopyDestination).has_value(), "tr CopyDst")) return false;
    gfx::TextureCopyRegion region{};
    region.x = pX; region.y = pY; region.width = pW; region.height = pH;
    if (!require(cmd.copyBufferToTextureRegion(*uploadRes, target, region).has_value(), "copyBufferToTextureRegion")) return false;

    // Pass 3: LoadOp::Load preserve + setViewport + setScissor draw in [120..184, 100..164].
    if (!require(cmd.transition(target, gfx::ResourceState::RenderTarget).has_value(), "tr RT 2")) return false;
    gfx::RenderPassDesc preservePass{};
    preservePass.colors = {gfx::ColorAttachment{target, gfx::LoadOp::Load, {}}};
    if (!require(cmd.beginRenderPass(preservePass).has_value(), "begin preserve pass")) return false;
    if (!require(cmd.setPipeline(*pipeRes).has_value(), "setPipeline")) return false;
    if (!require(cmd.setViewport({static_cast<float>(dX), static_cast<float>(dY), static_cast<float>(dW), static_cast<float>(dH), 0.0f, 1.0f}).has_value(), "setViewport")) return false;
    if (!require(cmd.setScissor(dX, dY, dW, dH).has_value(), "setScissor")) return false;
    if (!require(cmd.draw(3, 1).has_value(), "draw dirty rect")) return false;
    if (!require(cmd.endRenderPass().has_value(), "end preserve pass")) return false;

    if (!require(cmd.transition(target, gfx::ResourceState::CopySource).has_value(), "tr CopySrc")) return false;
    if (!require(cmd.copyTextureToBuffer(target, *readbackRes).has_value(), "copyTextureToBuffer")) return false;
    if (!require(cmd.end().has_value(), "cmd.end")) return false;

    auto fence = device.queue().submit(cmd);
    if (!require(fence.has_value() && device.queue().wait(*fence).has_value(), "submit/wait preservation")) return false;

    auto readRes = (*readbackRes)->read(0, fullFp.size);
    if (!require(readRes.has_value() && readRes->size() == fullFp.size, "readback preservation")) return false;

    std::uint32_t greenCount = 0, redCount = 0, preservedBlueCount = 0;
    for (std::uint32_t y = 0; y < H; ++y) {
        const auto* row = reinterpret_cast<const std::uint8_t*>(readRes->data() + static_cast<std::size_t>(y) * fullFp.rowPitch);
        for (std::uint32_t x = 0; x < W; ++x) {
            const int r = row[x * 4 + 0];
            const int g = row[x * 4 + 1];
            const int b = row[x * 4 + 2];
            const int a = row[x * 4 + 3];
            const bool inPatch = (x >= pX && x < pX + pW && y >= pY && y < pY + pH);
            const bool inDraw = (x >= dX && x < dX + dW && y >= dY && y < dY + dH);
            if (inPatch) {
                if (!require(r == 30 && g == 210 && b == 60 && a == 255, "Patch pixel mismatch")) return false;
                ++greenCount;
            } else if (inDraw) {
                if (!require(r == 255 && g == 0 && b == 0 && a == 255, "Dirty draw pixel mismatch")) return false;
                ++redCount;
            } else {
                if (!require(std::abs(r - 20) <= 1 && std::abs(g - 40) <= 1 && std::abs(b - 200) <= 1 && a == 255,
                             "Untouched background pixel not preserved")) return false;
                ++preservedBlueCount;
            }
        }
    }
    if (!require(greenCount == pW * pH && redCount == dW * dH && preservedBlueCount == W * H - pW * pH - dW * dH,
                 "Pixel region counts mismatch")) return false;
    std::cout << "  [" << label << "] preservation verified: green=" << greenCount
              << " red=" << redCount << " untouched_blue=" << preservedBlueCount << "\n";
    return true;
}

// Measures identical 32-layer overdraw workload on 1024x1024 target:
// Full-frame (Clear + 1024x1024) vs Dirty-region (Load + 128x128 = 1.56% area).
bool benchmarkFullVsDirty(gfx::Device& device, gfx::Backend backend, const std::string& label, bool isValidation) {
    constexpr std::uint32_t W = 1024, H = 1024;
    constexpr std::uint32_t dirtySize = 128;
    const int layers = isValidation ? 8 : 32;
    const int iterations = isValidation ? 4 : 16;

    auto pipeRes = makeRedPipeline(device, backend);
    if (!require(pipeRes.has_value(), "bench pipeline")) return false;
    auto targetRes = device.createTexture({W, H, gfx::Format::RGBA8UNorm, true});
    if (!require(targetRes.has_value(), "bench 1024x1024 target")) return false;
    auto target = *targetRes;
    const bool hasTimestamps = device.caps().timestampQueries;

    // Initialize target so LoadOp::Load is valid.
    {
        auto initCmd = device.createCommandList();
        if (!require(initCmd.has_value(), "bench init cmd")) return false;
        if (!require((*initCmd)->begin().has_value(), "init begin")) return false;
        if (!require((*initCmd)->transition(target, gfx::ResourceState::RenderTarget).has_value(), "init tr")) return false;
        gfx::RenderPassDesc rp{};
        rp.colors = {gfx::ColorAttachment{target, gfx::LoadOp::Clear, {0.1f, 0.2f, 0.3f, 1.0f}}};
        if (!require((*initCmd)->beginRenderPass(rp).has_value(), "init rp")) return false;
        if (!require((*initCmd)->endRenderPass().has_value(), "init end rp")) return false;
        if (!require((*initCmd)->end().has_value(), "init end")) return false;
        auto f = device.queue().submit(**initCmd);
        if (!require(f.has_value() && device.queue().wait(*f).has_value(), "init wait")) return false;
    }

    auto measure = [&](bool dirtyOnly, double& outCpuMs, double& outGpuMs, bool& outHasGpu) -> bool {
        double totalCpuMs = 0.0;
        double totalGpuMs = 0.0;
        int gpuSamples = 0;
        auto poolRes = device.createCommandPool();
        if (!require(poolRes.has_value(), "bench pool")) return false;
        auto cmdRes = (*poolRes)->createCommandList();
        if (!require(cmdRes.has_value(), "bench cmd")) return false;
        auto& cmd = **cmdRes;

        for (int it = 0; it < iterations; ++it) {
            if (!require((*poolRes)->reset().has_value(), "pool reset")) return false;
            const auto t0 = std::chrono::steady_clock::now();
            if (!require(cmd.begin().has_value(), "bench begin")) return false;
            if (hasTimestamps) {
                if (!require(cmd.writeTimestamp(0).has_value(), "ts 0")) return false;
            }
            gfx::RenderPassDesc rp{};
            rp.colors = {gfx::ColorAttachment{target, dirtyOnly ? gfx::LoadOp::Load : gfx::LoadOp::Clear, {0.0f, 0.0f, 0.0f, 1.0f}}};
            if (!require(cmd.beginRenderPass(rp).has_value(), "bench rp")) return false;
            if (!require(cmd.setPipeline(*pipeRes).has_value(), "bench pipe")) return false;
            if (dirtyOnly) {
                if (!require(cmd.setViewport({64.0f, 64.0f, static_cast<float>(dirtySize), static_cast<float>(dirtySize), 0.0f, 1.0f}).has_value(), "dirty vp")) return false;
                if (!require(cmd.setScissor(64, 64, dirtySize, dirtySize).has_value(), "dirty scissor")) return false;
            } else {
                if (!require(cmd.setViewport({0.0f, 0.0f, static_cast<float>(W), static_cast<float>(H), 0.0f, 1.0f}).has_value(), "full vp")) return false;
                if (!require(cmd.setScissor(0, 0, W, H).has_value(), "full scissor")) return false;
            }
            for (int l = 0; l < layers; ++l) {
                if (!require(cmd.draw(3, 1).has_value(), "bench draw")) return false;
            }
            if (!require(cmd.endRenderPass().has_value(), "bench end rp")) return false;
            if (hasTimestamps) {
                if (!require(cmd.writeTimestamp(1).has_value(), "ts 1")) return false;
                if (!require(cmd.resolveTimestamps().has_value(), "resolve ts")) return false;
            }
            if (!require(cmd.end().has_value(), "bench end")) return false;
            auto f = device.queue().submit(cmd);
            if (!require(f.has_value() && device.queue().wait(*f).has_value(), "bench wait")) return false;
            const auto t1 = std::chrono::steady_clock::now();
            totalCpuMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
            if (hasTimestamps) {
                auto gpuMs = cmd.elapsedGpuMilliseconds();
                if (gpuMs && std::isfinite(*gpuMs) && *gpuMs >= 0.0) {
                    totalGpuMs += *gpuMs;
                    ++gpuSamples;
                }
            }
        }
        outCpuMs = totalCpuMs / static_cast<double>(iterations);
        outHasGpu = (gpuSamples > 0);
        outGpuMs = outHasGpu ? (totalGpuMs / static_cast<double>(gpuSamples)) : 0.0;
        return true;
    };

    double fullCpu = 0.0, fullGpu = 0.0, dirtyCpu = 0.0, dirtyGpu = 0.0;
    bool fullHasGpu = false, dirtyHasGpu = false;
    if (!measure(false, fullCpu, fullGpu, fullHasGpu)) return false;
    if (!measure(true, dirtyCpu, dirtyGpu, dirtyHasGpu)) return false;

    std::cout << std::fixed << std::setprecision(4)
              << "  [" << label << "] 1024x1024x" << layers << " layers (" << iterations << " iters): "
              << "full_cpu=" << fullCpu << "ms dirty_cpu=" << dirtyCpu << "ms";
    if (fullHasGpu && dirtyHasGpu) {
        std::cout << " full_gpu=" << fullGpu << "ms dirty_gpu=" << dirtyGpu << "ms";
        if (dirtyGpu > 0.0) {
            std::cout << " gpu_speedup=" << std::setprecision(2) << (fullGpu / dirtyGpu) << "x";
        }
    } else {
        std::cout << " gpu=null";
    }
    std::cout << "\n";
    return true;
}

bool renderSwapFrame(gfx::Device& device, gfx::SwapChain& swap, gfx::Color c) {
    auto bb = swap.acquire();
    if (!require(bb.has_value(), "swap.acquire")) return false;
    auto cmd = device.createCommandList();
    if (!require(cmd.has_value(), "swap cmd")) return false;
    if (!require((*cmd)->begin().has_value(), "swap begin")) return false;
    if (!require((*cmd)->transition(*bb, gfx::ResourceState::RenderTarget).has_value(), "swap tr RT")) return false;
    gfx::RenderPassDesc rp{};
    rp.colors = {gfx::ColorAttachment{*bb, gfx::LoadOp::Clear, c}};
    if (!require((*cmd)->beginRenderPass(rp).has_value(), "swap rp")) return false;
    if (!require((*cmd)->endRenderPass().has_value(), "swap end rp")) return false;
    if (!require((*cmd)->transition(*bb, gfx::ResourceState::Present).has_value(), "swap tr Present")) return false;
    if (!require((*cmd)->end().has_value(), "swap end")) return false;
    auto f = device.queue().submit(**cmd);
    return require(f.has_value() && device.queue().wait(*f).has_value(), "swap submit/wait");
}

bool verifyPartialPresent(gfx::Device& device, const std::string& label, bool isD3D12) {
    HiddenWindow win;
    const std::wstring cls = L"GfxDirtyWin_" + std::to_wstring(GetTickCount64());
    if (!require(win.create(128, 96, cls.c_str()), "create dirty win")) return false;

    auto capsRes = device.querySurfacePresentCapabilities(win.surface());
    if (!capsRes) {
        if (capsRes.error().code == gfx::ErrorCode::Unsupported) {
            std::cout << "  [" << label << "] partial present query: Unsupported (" << capsRes.error().message << ")\n";
            ++gRejections;
            return true;
        }
        return failMsg("querySurfacePresentCapabilities failed: " + capsRes.error().message);
    }
    const auto& caps = *capsRes;
    std::cout << "  [" << label << "] partialPresent=" << (caps.partialPresent ? "yes" : "no")
              << " maxDirtyRects=" << caps.maxDirtyRects << "\n";

    gfx::SwapChainDesc sd{128, 96, 2, false};
    sd.presentMode = gfx::PresentMode::Fifo;
    auto scRes = device.createSwapChain(win.surface(), sd);
    if (!require(scRes.has_value(), "create swapchain for partial present")) return false;
    auto& swap = **scRes;

    // Invalid dirty rect rejections (must fail before presenting).
    {
        std::vector<gfx::DirtyRect> emptyRects;
        if (!requireError(swap.presentDirtyRects(emptyRects), gfx::ErrorCode::InvalidArgument, "empty dirty rects")) return false;
        std::array<gfx::DirtyRect, 1> zeroRect{{{8, 8, 0, 16}}};
        if (!requireError(swap.presentDirtyRects(zeroRect), gfx::ErrorCode::InvalidArgument, "zero width dirty rect")) return false;
        std::array<gfx::DirtyRect, 1> oobRect{{{100, 80, 40, 20}}};
        if (!requireError(swap.presentDirtyRects(oobRect), gfx::ErrorCode::InvalidArgument, "out-of-bounds dirty rect")) return false;
        std::vector<gfx::DirtyRect> tooMany(65, gfx::DirtyRect{0, 0, 4, 4});
        if (!requireError(swap.presentDirtyRects(tooMany), gfx::ErrorCode::InvalidArgument, "65 dirty rects > 64")) return false;
    }

    std::array<gfx::DirtyRect, 2> validRects{{{8, 8, 48, 32}, {64, 48, 32, 32}}};
    if (caps.partialPresent) {
        // Frame 0 + Frames 1..3 with dirty rects (first frame safely handles DXGI full-present requirement).
        for (int f = 0; f < 4; ++f) {
            if (!renderSwapFrame(device, swap, {0.2f * f, 0.4f, 0.6f, 1.0f})) return false;
            if (!require(swap.presentDirtyRects(validRects).has_value(), "presentDirtyRects failed")) return false;
        }
        // Resize and verify presentDirtyRects continues cleanly after resize.
        if (!require(win.resizeClient(160, 120), "resizeClient 160x120")) return false;
        if (!require(swap.resize(160, 120).has_value(), "swap.resize 160x120")) return false;
        for (int f = 0; f < 2; ++f) {
            if (!renderSwapFrame(device, swap, {0.3f, 0.6f, 0.2f, 1.0f})) return false;
            if (!require(swap.presentDirtyRects(validRects).has_value(), "post-resize presentDirtyRects failed")) return false;
        }
    } else {
        if (!renderSwapFrame(device, swap, {0.2f, 0.4f, 0.6f, 1.0f})) return false;
        if (!requireError(swap.presentDirtyRects(validRects), gfx::ErrorCode::Unsupported,
                          "presentDirtyRects must return Unsupported when partialPresent=false")) return false;
        if (!require(swap.present().has_value(), "fallback full present after Unsupported")) return false;
    }

    // D3D12 Tearing + dirty rects must return Unsupported.
    if (isD3D12 && caps.tearing) {
        scRes->reset();
        gfx::SwapChainDesc td{160, 120, 2, false};
        td.presentMode = gfx::PresentMode::Tearing;
        auto tearRes = device.createSwapChain(win.surface(), td);
        if (tearRes) {
            if (!renderSwapFrame(device, **tearRes, {0.5f, 0.5f, 0.5f, 1.0f})) return false;
            if (!requireError((*tearRes)->presentDirtyRects(validRects), gfx::ErrorCode::Unsupported,
                              "D3D12 Tearing + presentDirtyRects must return Unsupported")) return false;
            if (!require((*tearRes)->present().has_value(), "Tearing full present after rejection")) return false;
        }
    }
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

    if (!verifyDirtyRegionPreservation(dev, backend, label)) return false;
    if (!verifyPartialPresent(dev, label, backend == gfx::Backend::D3D12)) return false;
    if (!benchmarkFullVsDirty(dev, backend, label, validation)) return false;

    if (testRecovery) {
        if (!require(dev.removeForTesting().has_value(), "WARP removeForTesting")) return false;
        auto recovered = dev.recover();
        if (!require(recovered.has_value(), "WARP recover()")) return false;
        if (!verifyDirtyRegionPreservation(**recovered, backend, label + " [recovered]")) return false;
        if (!verifyPartialPresent(**recovered, label + " [recovered]", true)) return false;
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
    std::cout << "PASS gfx_dirty_region_tests: adapters=" << ran
              << " assertions=" << gAssertions << " rejections=" << gRejections << "\n";
    return 0;
}
#endif
