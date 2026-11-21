#ifndef _WIN32
#include <iostream>
int main() {
    std::cout << "SKIP: gfx_hdr_colorspace_tests requires Win32\n";
    return 77;
}
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <graphics/Graphics.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
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

const char* formatName(gfx::Format f) {
    switch (f) {
    case gfx::Format::RGBA8UNorm: return "RGBA8UNorm";
    case gfx::Format::BGRA8UNorm: return "BGRA8UNorm";
    case gfx::Format::RGBA8SRGB: return "RGBA8SRGB";
    case gfx::Format::BGRA8SRGB: return "BGRA8SRGB";
    case gfx::Format::RGB10A2UNorm: return "RGB10A2UNorm";
    case gfx::Format::RGBA16Float: return "RGBA16Float";
    default: return "Other";
    }
}

const char* colorSpaceName(gfx::ColorSpace cs) {
    switch (cs) {
    case gfx::ColorSpace::SrgbNonlinear: return "SrgbNonlinear";
    case gfx::ColorSpace::LinearSrgb: return "LinearSrgb";
    case gfx::ColorSpace::DisplayP3Nonlinear: return "DisplayP3Nonlinear";
    case gfx::ColorSpace::Rec2020Linear: return "Rec2020Linear";
    case gfx::ColorSpace::Hdr10St2084: return "Hdr10St2084";
    case gfx::ColorSpace::ExtendedLinearScRgb: return "ExtendedLinearScRgb";
    default: return "Unknown";
    }
}

// IEEE 754 binary16 to float decoder for verifying HDR (> 1.0) values.
float halfToFloat(std::uint16_t h) {
    const std::uint32_t sign = static_cast<std::uint32_t>(h & 0x8000u) << 16;
    const std::uint32_t exp = (h >> 10) & 0x1Fu;
    const std::uint32_t frac = h & 0x03FFu;
    std::uint32_t bits = 0;
    if (exp == 0) {
        if (frac == 0) {
            bits = sign;
        } else {
            int e = -14;
            std::uint32_t f = frac;
            while ((f & 0x0400u) == 0) { f <<= 1; --e; }
            f &= 0x03FFu;
            bits = sign | (static_cast<std::uint32_t>(e + 127) << 23) | (f << 13);
        }
    } else if (exp == 31) {
        bits = sign | 0x7F800000u | (frac << 13);
    } else {
        bits = sign | ((exp + 112u) << 23) | (frac << 13);
    }
    float out{};
    std::memcpy(&out, &bits, sizeof(out));
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
        // WS_POPUP avoids OS minimum tracking width clamping and preserves exact client extent.
        RECT rc{0, 0, static_cast<LONG>(w), static_cast<LONG>(h)};
        AdjustWindowRect(&rc, WS_POPUP, FALSE);
        hwnd = CreateWindowExW(0, className.c_str(), L"GFX HDR Test", WS_POPUP,
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

// Clears an offscreen render target and reads back all pixels into outBytes.
bool renderAndReadback(gfx::Device& device, gfx::Format fmt, gfx::Color clearColor,
                       std::uint32_t width, std::uint32_t height,
                       std::vector<std::uint8_t>& outBytes, gfx::TextureFootprint& outFootprint) {
    gfx::TextureDesc td{};
    td.width = width;
    td.height = height;
    td.format = fmt;
    td.renderTarget = true;
    auto tex = device.createTexture(td);
    if (!require(tex.has_value(), std::string("createTexture failed for ") + formatName(fmt))) return false;
    outFootprint = (*tex)->footprint();
    auto rb = device.createBuffer({outFootprint.size, gfx::Memory::Readback});
    if (!require(rb.has_value(), "create readback buffer failed")) return false;

    auto cmd = device.createCommandList();
    if (!require(cmd.has_value(), "createCommandList failed")) return false;
    if (!require((*cmd)->begin().has_value(), "cmd begin")) return false;
    if (!require((*cmd)->transition(*tex, gfx::ResourceState::RenderTarget).has_value(), "transition RT")) return false;
    gfx::RenderPassDesc rp{};
    rp.colors = {gfx::ColorAttachment{*tex, gfx::LoadOp::Clear, clearColor}};
    if (!require((*cmd)->beginRenderPass(rp).has_value(), "beginRenderPass")) return false;
    if (!require((*cmd)->endRenderPass().has_value(), "endRenderPass")) return false;
    if (!require((*cmd)->transition(*tex, gfx::ResourceState::CopySource).has_value(), "transition CopySource")) return false;
    if (!require((*cmd)->copyTextureToBuffer(*tex, *rb).has_value(), "copyTextureToBuffer")) return false;
    if (!require((*cmd)->end().has_value(), "cmd end")) return false;
    auto fence = device.queue().submit(**cmd);
    if (!require(fence.has_value(), "queue submit")) return false;
    if (!require(device.queue().wait(*fence).has_value(), "queue wait")) return false;

    auto readRes = (*rb)->read(0, outFootprint.size);
    if (!require(readRes.has_value() && readRes->size() == outFootprint.size, "readback buffer read failed")) return false;
    outBytes.resize(readRes->size());
    std::memcpy(outBytes.data(), readRes->data(), readRes->size());
    return true;
}

bool verifyOffscreenHdrAndSrgb(gfx::Device& device) {
    // 1. Format block info and capability queries.
    for (gfx::Format f : {gfx::Format::RGBA8SRGB, gfx::Format::BGRA8SRGB, gfx::Format::RGB10A2UNorm}) {
        auto bi = gfx::queryFormatBlockInfo(f);
        if (!require(bi.has_value() && bi->bytesPerBlock == 4u && !bi->compressed,
                     std::string("blockInfo 4B for ") + formatName(f))) return false;
        auto sup = device.queryFormatSupport(f);
        if (!require(sup.has_value(), std::string("queryFormatSupport ") + formatName(f))) return false;
        if (!require(sup->texture2D && sup->renderTarget && sup->sampled && sup->copySource && sup->copyDestination && sup->partialRegionCopy,
                     std::string("expected RT/sampled/copy support for ") + formatName(f))) return false;
        if (f == gfx::Format::RGBA8SRGB || f == gfx::Format::BGRA8SRGB) {
            if (!require(!sup->storage, std::string("sRGB format must not report storage support: ") + formatName(f))) return false;
        }
    }
    // Reject storage texture creation for sRGB format.
    {
        gfx::TextureDesc badStorage{};
        badStorage.width = 16; badStorage.height = 16;
        badStorage.format = gfx::Format::RGBA8SRGB;
        badStorage.storage = true;
        auto res = device.createTexture(badStorage);
        if (!requireError(res, gfx::ErrorCode::Unsupported, "RGBA8SRGB storage texture must be rejected")) return false;
    }

    constexpr std::uint32_t W = 32, H = 16;
    std::vector<std::uint8_t> bytes;
    gfx::TextureFootprint fp{};

    // 2. RGBA8SRGB hardware linear-to-sRGB gamma conversion full-pixel check.
    // linear (0.5, 0.25, 0.0, 1.0) -> sRGB ~(188, 137, 0, 255)
    if (!renderAndReadback(device, gfx::Format::RGBA8SRGB, {0.5f, 0.25f, 0.0f, 1.0f}, W, H, bytes, fp)) return false;
    for (std::uint32_t y = 0; y < H; ++y) {
        const auto* row = bytes.data() + static_cast<std::size_t>(y) * fp.rowPitch;
        for (std::uint32_t x = 0; x < W; ++x) {
            const int r = row[x * 4 + 0];
            const int g = row[x * 4 + 1];
            const int b = row[x * 4 + 2];
            const int a = row[x * 4 + 3];
            if (!require(std::abs(r - 188) <= 2 && std::abs(g - 137) <= 2 && b == 0 && a == 255,
                         "RGBA8SRGB pixel mismatch: got (" + std::to_string(r) + "," + std::to_string(g) + "," +
                         std::to_string(b) + "," + std::to_string(a) + ")")) return false;
        }
    }

    // 3. BGRA8SRGB channel order + sRGB gamma conversion full-pixel check.
    if (!renderAndReadback(device, gfx::Format::BGRA8SRGB, {0.5f, 0.25f, 0.0f, 1.0f}, W, H, bytes, fp)) return false;
    for (std::uint32_t y = 0; y < H; ++y) {
        const auto* row = bytes.data() + static_cast<std::size_t>(y) * fp.rowPitch;
        for (std::uint32_t x = 0; x < W; ++x) {
            const int b = row[x * 4 + 0];
            const int g = row[x * 4 + 1];
            const int r = row[x * 4 + 2];
            const int a = row[x * 4 + 3];
            if (!require(b == 0 && std::abs(g - 137) <= 2 && std::abs(r - 188) <= 2 && a == 255,
                         "BGRA8SRGB pixel mismatch")) return false;
        }
    }

    // 4. RGB10A2UNorm 10-bit packing full-pixel check: R=1023, G=511..512, B=255..256, A=3.
    if (!renderAndReadback(device, gfx::Format::RGB10A2UNorm, {1.0f, 0.5f, 0.25f, 1.0f}, W, H, bytes, fp)) return false;
    for (std::uint32_t y = 0; y < H; ++y) {
        const auto* row = bytes.data() + static_cast<std::size_t>(y) * fp.rowPitch;
        for (std::uint32_t x = 0; x < W; ++x) {
            std::uint32_t packed{};
            std::memcpy(&packed, row + x * 4, sizeof(packed));
            const int r10 = static_cast<int>(packed & 0x3FFu);
            const int g10 = static_cast<int>((packed >> 10) & 0x3FFu);
            const int b10 = static_cast<int>((packed >> 20) & 0x3FFu);
            const int a2 = static_cast<int>((packed >> 30) & 0x3u);
            if (!require(r10 == 1023 && std::abs(g10 - 512) <= 1 && std::abs(b10 - 256) <= 1 && a2 == 3,
                         "RGB10A2UNorm pixel mismatch: got (" + std::to_string(r10) + "," + std::to_string(g10) +
                         "," + std::to_string(b10) + "," + std::to_string(a2) + ")")) return false;
        }
    }

    // 5. RGBA16Float HDR (> 1.0) dynamic range full-pixel check.
    if (!renderAndReadback(device, gfx::Format::RGBA16Float, {2.5f, 1.5f, 0.5f, 1.0f}, W, H, bytes, fp)) return false;
    for (std::uint32_t y = 0; y < H; ++y) {
        const auto* row = bytes.data() + static_cast<std::size_t>(y) * fp.rowPitch;
        for (std::uint32_t x = 0; x < W; ++x) {
            std::uint16_t h[4]{};
            std::memcpy(h, row + x * 8, sizeof(h));
            const float rf = halfToFloat(h[0]);
            const float gf = halfToFloat(h[1]);
            const float bf = halfToFloat(h[2]);
            const float af = halfToFloat(h[3]);
            if (!require(std::fabs(rf - 2.5f) < 0.01f && std::fabs(gf - 1.5f) < 0.01f &&
                         std::fabs(bf - 0.5f) < 0.01f && std::fabs(af - 1.0f) < 0.01f,
                         "RGBA16Float HDR > 1.0 pixel mismatch")) return false;
        }
    }
    return true;
}

bool renderSwapChainFrame(gfx::Device& device, gfx::SwapChain& swap, gfx::Color clearColor) {
    auto bb = swap.acquire();
    if (!require(bb.has_value(), "swapchain acquire failed")) return false;
    if (!require((*bb)->description().format == swap.format(), "backbuffer format must match swap.format()")) return false;
    auto cmd = device.createCommandList();
    if (!require(cmd.has_value(), "createCommandList failed")) return false;
    if (!require((*cmd)->begin().has_value(), "cmd begin")) return false;
    if (!require((*cmd)->transition(*bb, gfx::ResourceState::RenderTarget).has_value(), "transition RT")) return false;
    gfx::RenderPassDesc rp{};
    rp.colors = {gfx::ColorAttachment{*bb, gfx::LoadOp::Clear, clearColor}};
    if (!require((*cmd)->beginRenderPass(rp).has_value(), "beginRenderPass")) return false;
    if (!require((*cmd)->endRenderPass().has_value(), "endRenderPass")) return false;
    if (!require((*cmd)->transition(*bb, gfx::ResourceState::Present).has_value(), "transition Present")) return false;
    if (!require((*cmd)->end().has_value(), "cmd end")) return false;
    auto fence = device.queue().submit(**cmd);
    if (!require(fence.has_value(), "queue submit")) return false;
    if (!require(device.queue().wait(*fence).has_value(), "queue wait")) return false;
    if (!require(swap.present().has_value(), "swap present")) return false;
    return true;
}

bool verifySwapChainColorSpaces(gfx::Device& device, const std::string& label, bool isD3D12) {
    HiddenWindow win;
    const std::wstring clsName = L"GfxHdrWin_" + std::to_wstring(GetTickCount64());
    if (!require(win.create(128, 96, clsName.c_str()), "Create hidden Win32 window failed")) return false;

    // Invalid surface rejections.
    {
        auto nullSurf = device.querySurfaceColorCapabilities(gfx::Surface{gfx::SurfaceKind::Win32, nullptr, nullptr, 0});
        if (!require(nullSurf.has_value() == false &&
                     (nullSurf.error().code == gfx::ErrorCode::InvalidArgument || nullSurf.error().code == gfx::ErrorCode::Unsupported),
                     "Null Win32 surface must be rejected")) return false;
        ++gRejections;
        auto metalSurf = device.querySurfaceColorCapabilities(gfx::Surface{gfx::SurfaceKind::MetalLayer, nullptr, nullptr, 0});
        if (!requireError(metalSurf, gfx::ErrorCode::Unsupported, "MetalLayer surface must return Unsupported on Windows")) return false;
    }

    auto capsRes = device.querySurfaceColorCapabilities(win.surface());
    if (!capsRes) {
        if (capsRes.error().code == gfx::ErrorCode::Unsupported) {
            std::cout << "  [" << label << "] surface color query: Unsupported (" << capsRes.error().message << ")\n";
            ++gRejections;
            return true;
        }
        return failMsg("querySurfaceColorCapabilities unexpected error: " + capsRes.error().message);
    }
    const auto& caps = *capsRes;
    std::cout << "  [" << label << "] color caps: pairs=" << caps.supportedFormats.size()
              << " srgb=" << (caps.srgbNonlinear ? "yes" : "no")
              << " linear=" << (caps.linearSrgb ? "yes" : "no")
              << " p3=" << (caps.displayP3 ? "yes" : "no")
              << " rec2020=" << (caps.rec2020Linear ? "yes" : "no")
              << " hdr10=" << (caps.hdr10St2084 ? "yes" : "no")
              << " scrgb=" << (caps.extendedLinearScRgb ? "yes" : "no")
              << " 10bit=" << (caps.tenBitOutput ? "yes" : "no")
              << " hdrMeta=" << (caps.hdrMetadata ? "yes" : "no")
              << " displayHdr=" << (caps.displayHdrEnabled ? "yes" : "no")
              << " displayMaxNits=" << caps.displayMaxLuminanceNits << "\n";

    if (!require(!caps.supportedFormats.empty(), "supportedFormats must not be empty")) return false;
    if (!require(caps.srgbNonlinear, "SrgbNonlinear must be supported")) return false;

    // Invalid ColorSpace enum, invalid HdrMetadata, and format/colorSpace mismatch rejections.
    {
        gfx::SwapChainDesc badCs{};
        badCs.width = 128; badCs.height = 96; badCs.buffers = 2;
        badCs.colorSpace = static_cast<gfx::ColorSpace>(99);
        if (!requireError(device.createSwapChain(win.surface(), badCs), gfx::ErrorCode::InvalidArgument,
                          "Unknown ColorSpace enum must return InvalidArgument")) return false;

        gfx::SwapChainDesc badMeta{};
        badMeta.width = 128; badMeta.height = 96; badMeta.buffers = 2;
        gfx::HdrMetadata invalidChrom{};
        invalidChrom.redPrimaryX = 1.5f;
        badMeta.hdrMetadata = invalidChrom;
        if (!requireError(device.createSwapChain(win.surface(), badMeta), gfx::ErrorCode::InvalidArgument,
                          "Out-of-range HDR chromaticity must return InvalidArgument")) return false;

        gfx::SwapChainDesc badLum{};
        badLum.width = 128; badLum.height = 96; badLum.buffers = 2;
        gfx::HdrMetadata invLum{};
        invLum.maxMasteringLuminance = 100.0f;
        invLum.minMasteringLuminance = 200.0f;
        badLum.hdrMetadata = invLum;
        if (!requireError(device.createSwapChain(win.surface(), badLum), gfx::ErrorCode::InvalidArgument,
                          "minMasteringLuminance > maxMasteringLuminance must return InvalidArgument")) return false;

        gfx::SwapChainDesc badCll{};
        badCll.width = 128; badCll.height = 96; badCll.buffers = 2;
        gfx::HdrMetadata invCll{};
        invCll.maxContentLightLevel = 400.0f;
        invCll.maxFrameAverageLightLevel = 800.0f;
        badCll.hdrMetadata = invCll;
        if (!requireError(device.createSwapChain(win.surface(), badCll), gfx::ErrorCode::InvalidArgument,
                          "maxFALL > maxCLL must return InvalidArgument")) return false;

        gfx::SwapChainDesc depthSwap{};
        depthSwap.width = 128; depthSwap.height = 96; depthSwap.buffers = 2;
        depthSwap.format = gfx::Format::D32Float;
        if (!requireError(device.createSwapChain(win.surface(), depthSwap), gfx::ErrorCode::Unsupported,
                          "D32Float swapchain format must return Unsupported")) return false;

        gfx::SwapChainDesc mismatch{};
        mismatch.width = 128; mismatch.height = 96; mismatch.buffers = 2;
        mismatch.format = gfx::Format::RGBA8UNorm;
        mismatch.colorSpace = gfx::ColorSpace::Hdr10St2084;
        if (!requireError(device.createSwapChain(win.surface(), mismatch), gfx::ErrorCode::Unsupported,
                          "RGBA8UNorm + Hdr10St2084 must return Unsupported")) return false;
    }

    // Platform-specific unsupported color space rejection checks.
    if (isD3D12) {
        for (gfx::ColorSpace unsup : {gfx::ColorSpace::DisplayP3Nonlinear, gfx::ColorSpace::Rec2020Linear}) {
            gfx::SwapChainDesc d{};
            d.width = 128; d.height = 96; d.buffers = 2;
            d.colorSpace = unsup;
            if (!requireError(device.createSwapChain(win.surface(), d), gfx::ErrorCode::Unsupported,
                              std::string("D3D12 must reject unsupported ") + colorSpaceName(unsup))) return false;
        }
    } else {
        auto testIfUnsupported = [&](bool supported, gfx::ColorSpace cs) -> bool {
            if (supported) return true;
            gfx::SwapChainDesc d{};
            d.width = 128; d.height = 96; d.buffers = 2;
            d.colorSpace = cs;
            return requireError(device.createSwapChain(win.surface(), d), gfx::ErrorCode::Unsupported,
                                std::string("Vulkan must reject unsupported ") + colorSpaceName(cs));
        };
        if (!testIfUnsupported(caps.displayP3, gfx::ColorSpace::DisplayP3Nonlinear)) return false;
        if (!testIfUnsupported(caps.rec2020Linear, gfx::ColorSpace::Rec2020Linear)) return false;
        if (!testIfUnsupported(caps.hdr10St2084, gfx::ColorSpace::Hdr10St2084)) return false;
        if (!testIfUnsupported(caps.extendedLinearScRgb, gfx::ColorSpace::ExtendedLinearScRgb)) return false;
    }

    // Verify every reported (Format, ColorSpace) pair: create, render, present, metadata, resize.
    int verifiedPairs = 0;
    for (const auto& pair : caps.supportedFormats) {
        if (!require(win.resizeClient(128, 96), "resizeClient 128x96")) return false;
        gfx::SwapChainDesc d{};
        d.width = 128; d.height = 96; d.buffers = 2;
        d.presentMode = gfx::PresentMode::Fifo;
        d.format = pair.format;
        d.colorSpace = pair.colorSpace;
        auto swapRes = device.createSwapChain(win.surface(), d);
        if (!swapRes) {
            if (swapRes.error().code == gfx::ErrorCode::Unsupported) {
                std::cout << "    pair (" << formatName(pair.format) << ", " << colorSpaceName(pair.colorSpace)
                          << "): runtime Unsupported (" << swapRes.error().message << ")\n";
                ++gRejections;
                continue;
            }
            return failMsg(std::string("createSwapChain failed for ") + formatName(pair.format) + " / " +
                           colorSpaceName(pair.colorSpace) + ": " + swapRes.error().message);
        }
        auto& swap = **swapRes;
        if (!require(swap.format() == pair.format, "swap.format() mismatch")) return false;
        if (!require(swap.colorSpace() == pair.colorSpace, "swap.colorSpace() mismatch")) return false;
        const gfx::Color clearColor = (pair.format == gfx::Format::RGBA16Float)
            ? gfx::Color{1.5f, 0.75f, 0.25f, 1.0f}
            : gfx::Color{0.5f, 0.25f, 0.125f, 1.0f};
        if (!renderSwapChainFrame(device, swap, clearColor)) return false;

        if (pair.colorSpace == gfx::ColorSpace::Hdr10St2084 || pair.colorSpace == gfx::ColorSpace::ExtendedLinearScRgb) {
            gfx::HdrMetadata meta{};
            meta.maxMasteringLuminance = 1000.0f;
            meta.minMasteringLuminance = 0.001f;
            meta.maxContentLightLevel = 1000.0f;
            meta.maxFrameAverageLightLevel = 400.0f;
            auto metaRes = swap.setHdrMetadata(meta);
            if (caps.hdrMetadata) {
                if (metaRes) {
                    if (!require(swap.hdrMetadata().has_value() && *swap.hdrMetadata() == meta,
                                 "hdrMetadata() must match setHdrMetadata()")) return false;
                } else if (metaRes.error().code == gfx::ErrorCode::Unsupported) {
                    ++gRejections;
                } else {
                    return failMsg("setHdrMetadata unexpected error: " + metaRes.error().message);
                }
            } else {
                if (!requireError(metaRes, gfx::ErrorCode::Unsupported,
                                  "setHdrMetadata must return Unsupported when caps.hdrMetadata=false")) return false;
            }
            gfx::HdrMetadata bad{};
            bad.maxMasteringLuminance = -10.0f;
            if (!requireError(swap.setHdrMetadata(bad), gfx::ErrorCode::InvalidArgument,
                              "Negative maxMasteringLuminance must return InvalidArgument")) return false;
        }

        const auto savedMeta = swap.hdrMetadata();
        if (!require(win.resizeClient(160, 120), "resizeClient 160x120")) return false;
        if (!require(swap.resize(160, 120).has_value(), "swap.resize(160,120) failed")) return false;
        if (!require(swap.format() == pair.format, "swap.format() must persist across resize")) return false;
        if (!require(swap.colorSpace() == pair.colorSpace, "swap.colorSpace() must persist across resize")) return false;
        if (!require(swap.hdrMetadata() == savedMeta, "swap.hdrMetadata() must persist across resize")) return false;
        if (!renderSwapChainFrame(device, swap, clearColor)) return false;

        ++verifiedPairs;
        std::cout << "    pair (" << formatName(pair.format) << ", " << colorSpaceName(pair.colorSpace)
                  << "): PASS (render+present+resize"
                  << (savedMeta ? "+hdrMeta" : "") << ")\n";
    }
    if (!require(verifiedPairs >= 1, "At least one swapchain color space pair must be verified")) return false;
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

    if (!verifyOffscreenHdrAndSrgb(dev)) return false;
    if (!verifySwapChainColorSpaces(dev, label, backend == gfx::Backend::D3D12)) return false;

    if (testRecovery) {
        if (!require(dev.removeForTesting().has_value(), "WARP removeForTesting")) return false;
        auto recovered = dev.recover();
        if (!require(recovered.has_value(), "WARP recover()")) return false;
        if (!verifyOffscreenHdrAndSrgb(**recovered)) return false;
        if (!verifySwapChainColorSpaces(**recovered, label + " [recovered]", true)) return false;
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
    std::cout << "PASS gfx_hdr_colorspace_tests: adapters=" << ran
              << " assertions=" << gAssertions << " rejections=" << gRejections << "\n";
    return 0;
}
#endif
