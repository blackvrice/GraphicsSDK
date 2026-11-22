#ifndef _WIN32
#include <iostream>
int main() {
    std::cout << "SKIP: gfx_graphics_ui_tests requires Win32\n";
    return 77;
}
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <graphics/Graphics.hpp>
#include "../examples/graphics_ui_foundation/UiBatchRenderer.hpp"
#include "fixtures/BindingFixtures.hpp"
#include "fixtures/VertexFixtures.hpp"

#include <algorithm>
#include <array>
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

bool failMsg(const std::string& msg) {
    std::cerr << "FAIL: " << msg << "\n";
    return false;
}

bool require(bool cond, const std::string& msg) {
    ++gAssertions;
    if (!cond) return failMsg(msg);
    return true;
}

template<std::size_t N>
gfx::ShaderBinary spirvBinary(gfx::ShaderStage stage, const std::array<std::uint32_t, N>& words) {
    const auto raw = std::as_bytes(std::span{words});
    return {stage, std::vector<std::byte>(raw.begin(), raw.end()), gfx::ShaderFormat::SPIRV};
}

gfx::Result<gfx_ui_example::UiShaderSet> makeUiShaders(gfx::Backend backend) {
    gfx_ui_example::UiShaderSet out{};
    if (backend == gfx::Backend::D3D12) {
        constexpr std::string_view quadHlsl = R"(
struct VSIn {
    float2 pos : POSITION0;
    float4 col : COLOR0;
};
struct VSOut {
    float4 pos : SV_Position;
    float4 col : COLOR0;
};
VSOut VS(VSIn v) {
    VSOut o;
    o.pos = float4(v.pos, 0.5, 1.0);
    o.col = v.col;
    return o;
}
float4 PS(VSOut i) : SV_Target0 { return i.col; }
)";
        constexpr std::string_view atlasHlsl = R"(
Texture2D tex0 : register(t0);
SamplerState samp0 : register(s0);
struct VSOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};
VSOut VS(uint id : SV_VertexID) {
    float2 p[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
    float2 u[3] = { float2(0.0, 0.0), float2(2.0, 0.0), float2(0.0, 2.0) };
    VSOut o;
    o.pos = float4(p[id % 3], 0.5, 1.0);
    o.uv = u[id % 3];
    return o;
}
float4 PS(VSOut i) : SV_Target0 { return tex0.Sample(samp0, i.uv); }
)";
        auto qvs = gfx::compileHlsl(quadHlsl, "VS", gfx::ShaderStage::Vertex);
        if (!qvs) return std::unexpected(qvs.error());
        auto qps = gfx::compileHlsl(quadHlsl, "PS", gfx::ShaderStage::Fragment);
        if (!qps) return std::unexpected(qps.error());
        auto avs = gfx::compileHlsl(atlasHlsl, "VS", gfx::ShaderStage::Vertex);
        if (!avs) return std::unexpected(avs.error());
        auto aps = gfx::compileHlsl(atlasHlsl, "PS", gfx::ShaderStage::Fragment);
        if (!aps) return std::unexpected(aps.error());
        out.quadVs = std::move(*qvs);
        out.quadPs = std::move(*qps);
        out.atlasVs = std::move(*avs);
        out.atlasPs = std::move(*aps);
    } else {
        out.quadVs = spirvBinary(gfx::ShaderStage::Vertex, vertexFixtures::position2);
        out.quadPs = spirvBinary(gfx::ShaderStage::Fragment, vertexFixtures::color);
        out.atlasVs = spirvBinary(gfx::ShaderStage::Vertex, bindingFixtures::uv);
        out.atlasPs = spirvBinary(gfx::ShaderStage::Fragment, bindingFixtures::sample);
    }
    return out;
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
        hwnd = CreateWindowExW(0, className.c_str(), L"GFX UI Window", WS_POPUP,
                               0, 0, rc.right - rc.left, rc.bottom - rc.top,
                               nullptr, nullptr, wc.hInstance, nullptr);
        return hwnd != nullptr;
    }

    gfx::Surface surface() const {
        return gfx::Surface{gfx::SurfaceKind::Win32, static_cast<void*>(hwnd), nullptr, 0};
    }

    ~HiddenWindow() {
        if (hwnd) DestroyWindow(hwnd);
        if (cls) UnregisterClassW(className.c_str(), GetModuleHandleW(nullptr));
    }
};

struct RgbaF {
    float r{}, g{}, b{}, a{};
};

RgbaF blendOver(RgbaF dst, RgbaF src) {
    const float inv = 1.0f - src.a;
    return {
        src.r * src.a + dst.r * inv,
        src.g * src.a + dst.g * inv,
        src.b * src.a + dst.b * inv,
        src.a + dst.a * inv
    };
}

// Verifies 2D alpha blending, scissor clipping, dynamic VB/IB UploadRing, and partial atlas update
// against a full-image (128x128 = 16,384 pixels) CPU software reference.
bool verifyOffscreenUiReference(gfx::Device& device, gfx_ui_example::UiBatchRenderer& ui, const std::string& label) {
    constexpr std::uint32_t W = 128, H = 128;

    // Partially populate all 64x64 atlas quadrants via copyBufferToTextureRegion (4 distinct 32x32 glyph/icon tiles).
    if (!require(ui.updateAtlasRegion(0, 0, 32, 32, 240, 200, 40, 128).has_value(), "atlas tile (0,0)")) return false;
    if (!require(ui.updateAtlasRegion(32, 0, 32, 32, 240, 200, 40, 128).has_value(), "atlas tile (32,0)")) return false;
    if (!require(ui.updateAtlasRegion(0, 32, 32, 32, 240, 200, 40, 128).has_value(), "atlas tile (0,32)")) return false;
    if (!require(ui.updateAtlasRegion(32, 32, 32, 32, 240, 200, 40, 128).has_value(), "atlas tile (32,32)")) return false;

    auto targetRes = device.createTexture({W, H, gfx::Format::RGBA8UNorm, true});
    if (!require(targetRes.has_value(), "create 128x128 offscreen UI target")) return false;
    auto target = *targetRes;
    const auto fp = target->footprint();
    auto readbackRes = device.createBuffer({fp.size, gfx::Memory::Readback});
    if (!require(readbackRes.has_value(), "create UI readback buffer")) return false;

    // Compose UI batch:
    //  Background: (20, 30, 50, 255)
    //  1) Opaque panel:   [8..72, 8..72],   color (40, 120, 220, 255), full scissor
    //  2) Alpha panel:    [40..104, 40..104], color (220, 60, 40, 128) (50% alpha), scissor [48..96, 48..96]
    //  3) Atlas glyph overlay: [80..112, 16..48] sampling 50% alpha gold (240, 200, 40, 128)
    ui.beginBatch(W, H);
    ui.addRect(8, 8, 64, 64, 40, 120, 220, 255, {0, 0, W, H});
    ui.addRect(40, 40, 64, 64, 220, 60, 40, 128, {48, 48, 48, 48});
    ui.addAtlasOverlay(80, 16, 32, 32);
    auto staged = ui.stageBatch();
    if (!require(staged.has_value(), "UI stageBatch")) return false;

    auto cmdRes = device.createCommandList();
    if (!require(cmdRes.has_value(), "UI cmd")) return false;
    auto& cmd = **cmdRes;
    if (!require(cmd.begin().has_value(), "UI begin")) return false;
    if (!require(ui.recordStagedPass(cmd, *staged, target, gfx::LoadOp::Clear,
                                     {20.0f / 255.0f, 30.0f / 255.0f, 50.0f / 255.0f, 1.0f},
                                     gfx::ResourceState::CopySource).has_value(), "UI recordStagedPass")) return false;
    if (!require(cmd.copyTextureToBuffer(target, *readbackRes).has_value(), "UI copyTextureToBuffer")) return false;
    if (!require(cmd.end().has_value(), "UI end")) return false;

    auto fence = device.queue().submit(cmd);
    if (!require(fence.has_value() && device.queue().wait(*fence).has_value(), "UI submit/wait")) return false;
    if (!require(ui.commitAndReclaim(*fence).has_value(), "UI commitAndReclaim")) return false;

    auto bytesRes = (*readbackRes)->read(0, fp.size);
    if (!require(bytesRes.has_value() && bytesRes->size() == fp.size, "UI readback read")) return false;

    // Independent CPU reference verification across all 16,384 pixels.
    for (std::uint32_t y = 0; y < H; ++y) {
        const auto* row = reinterpret_cast<const std::uint8_t*>(bytesRes->data() + static_cast<std::size_t>(y) * fp.rowPitch);
        for (std::uint32_t x = 0; x < W; ++x) {
            RgbaF ref{20.0f / 255.0f, 30.0f / 255.0f, 50.0f / 255.0f, 1.0f};
            if (x >= 8 && x < 72 && y >= 8 && y < 72) {
                ref = blendOver(ref, {40.0f / 255.0f, 120.0f / 255.0f, 220.0f / 255.0f, 1.0f});
            }
            // Scissored to [48..96, 48..96]:
            if (x >= 48 && x < 96 && y >= 48 && y < 96) {
                ref = blendOver(ref, {220.0f / 255.0f, 60.0f / 255.0f, 40.0f / 255.0f, 128.0f / 255.0f});
            }
            // Atlas overlay at [80..112, 16..48]:
            if (x >= 80 && x < 112 && y >= 16 && y < 48) {
                ref = blendOver(ref, {240.0f / 255.0f, 200.0f / 255.0f, 40.0f / 255.0f, 128.0f / 255.0f});
            }
            const int er = static_cast<int>(std::lround(ref.r * 255.0f));
            const int eg = static_cast<int>(std::lround(ref.g * 255.0f));
            const int eb = static_cast<int>(std::lround(ref.b * 255.0f));
            const int r = row[x * 4 + 0];
            const int g = row[x * 4 + 1];
            const int b = row[x * 4 + 2];
            if (!require(std::abs(r - er) <= 2 && std::abs(g - eg) <= 2 && std::abs(b - eb) <= 2,
                         "UI pixel mismatch at (" + std::to_string(x) + "," + std::to_string(y) +
                         "): got (" + std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) +
                         ") expected (" + std::to_string(er) + "," + std::to_string(eg) + "," + std::to_string(eb) + ")")) {
                return false;
            }
        }
    }
    std::cout << "  [" << label << "] offscreen 128x128 (16,384 px) alpha+scissor+VB/IB+atlas CPU reference: PASS\n";
    return true;
}

// Verifies low-latency multi-surface UI rendering across 2 independent windows.
bool verifyMultiWindowLowLatencyUi(gfx::Device& device, gfx_ui_example::UiBatchRenderer& ui, const std::string& label) {
    const std::uint64_t tag = GetTickCount64();
    HiddenWindow winA, winB;
    if (!require(winA.create(160, 120, (L"GfxUI_A_" + std::to_wstring(tag)).c_str()), "create UI winA")) return false;
    if (!require(winB.create(192, 128, (L"GfxUI_B_" + std::to_wstring(tag)).c_str()), "create UI winB")) return false;

    auto capsRes = device.querySurfacePresentCapabilities(winA.surface());
    if (!capsRes) {
        if (capsRes.error().code == gfx::ErrorCode::Unsupported) {
            std::cout << "  [" << label << "] multi-surface UI output: Unsupported (" << capsRes.error().message << ")\n";
            return true;
        }
        return failMsg("querySurfacePresentCapabilities failed: " + capsRes.error().message);
    }
    const gfx::PresentMode lowLatMode = capsRes->immediate ? gfx::PresentMode::Immediate : gfx::PresentMode::Fifo;

    gfx::SwapChainDesc dA{160, 120, 2, false};
    dA.presentMode = lowLatMode;
    dA.pacingMode = gfx::FramePacingMode::LowLatency;
    dA.maxFrameLatency = 1u;

    gfx::SwapChainDesc dB{192, 128, 2, false};
    dB.presentMode = lowLatMode;
    dB.pacingMode = gfx::FramePacingMode::LowLatency;
    dB.maxFrameLatency = 1u;

    auto scARes = device.createSwapChain(winA.surface(), dA);
    auto scBRes = device.createSwapChain(winB.surface(), dB);
    if (!require(scARes.has_value() && scBRes.has_value(), "create 2 low-latency UI swapchains")) return false;
    auto& swapA = **scARes;
    auto& swapB = **scBRes;

    constexpr int kFrames = 8;
    for (int f = 0; f < kFrames; ++f) {
        if (!require(swapA.waitForNextFrame().has_value(), "swapA waitForNextFrame")) return false;
        if (!require(swapB.waitForNextFrame().has_value(), "swapB waitForNextFrame")) return false;
        auto bbA = swapA.acquire();
        auto bbB = swapB.acquire();
        if (!require(bbA.has_value() && bbB.has_value(), "acquire 2 UI backbuffers")) return false;

        // Stage both windows' dynamic VB/IB batches BEFORE command recording begins.
        ui.beginBatch(160, 120);
        ui.addRect(8 + f * 4, 8, 64, 48, 50, 160, 240, 255, {0, 0, 160, 120});
        ui.addRect(24 + f * 4, 24, 64, 48, 240, 90, 60, 160, {16, 16, 128, 88});
        ui.addAtlasOverlay(96, 16, 32, 32);
        auto stagedA = ui.stageBatch();
        if (!require(stagedA.has_value(), "stageBatch A")) return false;

        ui.beginBatch(192, 128);
        ui.addRect(12 + f * 4, 12, 80, 56, 80, 210, 120, 255, {0, 0, 192, 128});
        ui.addAtlasOverlay(120, 24, 48, 48);
        auto stagedB = ui.stageBatch();
        if (!require(stagedB.has_value(), "stageBatch B")) return false;

        auto cmd = device.createCommandList();
        if (!require(cmd.has_value(), "create multi-window UI cmd")) return false;
        if (!require((*cmd)->begin().has_value(), "cmd begin")) return false;
        if (!require(ui.recordStagedPass(**cmd, *stagedA, *bbA, gfx::LoadOp::Clear, {0.08f, 0.1f, 0.14f, 1.0f}, gfx::ResourceState::Present).has_value(), "record A")) return false;
        if (!require(ui.recordStagedPass(**cmd, *stagedB, *bbB, gfx::LoadOp::Clear, {0.1f, 0.08f, 0.14f, 1.0f}, gfx::ResourceState::Present).has_value(), "record B")) return false;
        if (!require((*cmd)->end().has_value(), "cmd end")) return false;

        auto fence = device.queue().submit(**cmd);
        if (!require(fence.has_value() && device.queue().wait(*fence).has_value(), "submit/wait 2 UI windows")) return false;
        if (!require(ui.commitAndReclaim(*fence).has_value(), "multi-window commitAndReclaim")) return false;

        std::array<gfx::SwapChain* const, 2> chains{&swapA, &swapB};
        auto pres = device.presentSwapChains(chains);
        if (!require(pres.has_value() && pres->presentedCount == 2u, "presentSwapChains 2/2")) return false;
    }

    auto statsA = swapA.framePacingStats();
    auto statsB = swapB.framePacingStats();
    if (!require(statsA.has_value() && statsA->presentedFrames == kFrames && statsA->maxFrameLatency == 1u, "statsA")) return false;
    if (!require(statsB.has_value() && statsB->presentedFrames == kFrames && statsB->maxFrameLatency == 1u, "statsB")) return false;

    std::cout << std::fixed << std::setprecision(3)
              << "  [" << label << "] 2-window low-latency UI (" << kFrames << " frames, maxLat=1): "
              << "winA avg=" << statsA->averageFrameMilliseconds << "ms qLat=" << statsA->lastQueueLatencyMilliseconds
              << "ms | winB avg=" << statsB->averageFrameMilliseconds << "ms qLat=" << statsB->lastQueueLatencyMilliseconds << "ms\n";
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

    auto shadersRes = makeUiShaders(backend);
    if (!require(shadersRes.has_value(), "makeUiShaders failed")) return false;
    {
        auto uiRes = gfx_ui_example::UiBatchRenderer::create(dev, *shadersRes);
        if (!require(uiRes.has_value(), "UiBatchRenderer::create failed")) return false;
        if (!verifyOffscreenUiReference(dev, **uiRes, label)) return false;
        if (!verifyMultiWindowLowLatencyUi(dev, **uiRes, label)) return false;
    }

    if (testRecovery) {
        if (!require(dev.removeForTesting().has_value(), "WARP removeForTesting")) return false;
        auto recovered = dev.recover();
        if (!require(recovered.has_value(), "WARP recover()")) return false;
        auto uiRec = gfx_ui_example::UiBatchRenderer::create(**recovered, *shadersRes);
        if (!require(uiRec.has_value(), "UiBatchRenderer::create on recovered")) return false;
        if (!verifyOffscreenUiReference(**recovered, **uiRec, label + " [recovered]")) return false;
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
    std::cout << "PASS gfx_graphics_ui_tests: adapters=" << ran << " assertions=" << gAssertions << "\n";
    return 0;
}
#endif
