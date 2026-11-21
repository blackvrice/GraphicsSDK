#include <graphics/Graphics.hpp>
#include "../examples/sandbox_adapter/SandboxRhiAdapter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace gfx::sandbox_adapter;

struct Totals {
    std::uint64_t assertions{};
    std::uint64_t framesRendered{};
    std::uint64_t rejections{};
};

void check(bool cond, const char* msg, Totals& t) {
    if (!cond) throw std::runtime_error(msg);
    ++t.assertions;
}

template <class T>
T take(gfx::Result<T>&& r, const char* msg, Totals& t) {
    if (!r) {
        throw std::runtime_error(std::string(msg) + ": " + r.error().message +
            " (code=" + std::to_string(static_cast<int>(r.error().code)) + ")");
    }
    ++t.assertions;
    return std::move(*r);
}

void take(gfx::Result<void>&& r, const char* msg, Totals& t) {
    if (!r) {
        throw std::runtime_error(std::string(msg) + ": " + r.error().message +
            " (code=" + std::to_string(static_cast<int>(r.error().code)) + ")");
    }
    ++t.assertions;
}

template <class T>
void reject(const gfx::Result<T>& r, gfx::ErrorCode expected, const char* msg, Totals& t) {
    if (r) throw std::runtime_error(std::string("Unexpected success: ") + msg);
    if (r.error().code != expected) {
        throw std::runtime_error(std::string(msg) + " (expected=" +
            std::to_string(static_cast<int>(expected)) + ", got=" +
            std::to_string(static_cast<int>(r.error().code)) + ": " + r.error().message + ")");
    }
    ++t.assertions;
    ++t.rejections;
}

void testHandlePoolAndGapRejections(AdapterDevice& adapter, Totals& totals) {
    // 1. Generational HandlePool & stale handle rejection
    auto h1 = take(adapter.createBuffer({256, AdapterMemoryType::GpuOnly, false, "b1"}), "create b1", totals);
    check(h1.valid() && adapter.alive(h1), "h1 alive", totals);
    adapter.destroy(h1);
    check(!adapter.alive(h1), "h1 immediately dead after destroy", totals);
    reject(adapter.resolve(h1), gfx::ErrorCode::InvalidArgument, "stale h1 rejected", totals);

    // Reallocating slot bumps generation so old h1 stays invalid while new h2 is alive.
    auto h2 = take(adapter.createBuffer({256, AdapterMemoryType::GpuOnly, false, "b2"}), "create b2", totals);
    check(h2.valid() && adapter.alive(h2), "h2 alive", totals);
    check(h2.index == h1.index && h2.generation != h1.generation, "slot reused with bumped generation", totals);
    check(!adapter.alive(h1), "h1 still dead after slot reuse", totals);
    adapter.destroy(h2);

    // 2. Honest Unsupported rejections for documented Sandbox RHI gaps
    for (AdapterFormat unsupportedFmt : {
            AdapterFormat::R8Unorm, AdapterFormat::RG8Unorm,
            AdapterFormat::RGBA8Srgb, AdapterFormat::D24UnormS8Uint, AdapterFormat::R16Uint}) {
        AdapterTextureDesc td{};
        td.width = 16; td.height = 16; td.format = unsupportedFmt;
        reject(adapter.createTexture(td), gfx::ErrorCode::Unsupported, "unsupported format rejected", totals);
    }
    reject(adapter.createTexture({16, 16, AdapterFormat::RGBA8Unorm, 4, 1}),
           gfx::ErrorCode::Unsupported, "multi-mip texture rejected (P8-TEX-02)", totals);
    reject(adapter.createTexture({16, 16, AdapterFormat::RGBA8Unorm, 1, 2}),
           gfx::ErrorCode::Unsupported, "multi-layer texture rejected (P8-TEX-02)", totals);

    take(adapter.waitIdle(), "waitIdle after handle/gap test", totals);
    const auto st = take(adapter.stats(), "stats after handle/gap test", totals);
    check(st.liveBuffers == 0u && st.pendingDestructions == 0u && st.releasedObjects >= 2u,
          "deferred destruction drained on waitIdle", totals);
}

void testMultiFrameUploadRingPartialCopyViewportAndTimestamps(AdapterDevice& adapter, Totals& totals) {
    // Enable SDK profiling so adapter + SDK counters stay aligned.
    take(adapter.nativeDevice().setProfilingEnabled(true), "enable profiling", totals);
    take(adapter.nativeDevice().resetProfilingStatistics(), "reset profiling", totals);

    // Prepare target texture (32x32 RGBA8), readback buffer, and a device buffer for offset copy checks.
    auto texH = take(adapter.createTexture({32, 32, AdapterFormat::RGBA8Unorm, 1, 1, 1, true, false, false, "atlas"}),
                     "create atlas", totals);
    auto texPtr = take(adapter.resolve(texH), "resolve atlas", totals);
    const auto fp = texPtr->footprint();
    texPtr.reset();

    auto rbH = take(adapter.createBuffer({fp.size, AdapterMemoryType::Readback, false, "atlasRB"}), "create atlasRB", totals);
    auto devBufH = take(adapter.createBuffer({2048, AdapterMemoryType::GpuOnly, false, "devBuf"}), "create devBuf", totals);
    auto devRbH = take(adapter.createBuffer({2048, AdapterMemoryType::Readback, false, "devRB"}), "create devRB", totals);

    // 4 frames across framesInFlight=2 slots:
    // Each frame uploads a 8x8 sub-region via UploadRing (512-byte aligned) + 128B buffer slice,
    // renders a clear pass with dynamic viewport/scissor, records 2 GPU timestamp scopes,
    // and destroys an ephemeral buffer mid-flight to verify DeferredDestructionQueue!
    const bool hasTs = adapter.caps().timestampQueries;
    for (std::uint32_t frame = 0; frame < 4u; ++frame) {
        auto* cmd = take(adapter.beginFrame(), "beginFrame", totals);
        if (hasTs) take(cmd->beginTimestampScope("AdapterFrame"), "begin AdapterFrame", totals);

        // Verify previous slot's completedTimestamps() becomes populated once slot wraps (frame >= 2).
        if (hasTs && frame >= adapter.framesInFlight()) {
            const auto& ts = adapter.completedTimestamps();
            check(ts.valid(), "completedTimestamps valid after ring wrap", totals);
            check(ts.scopes.size() == 2u, "completedTimestamps has 2 scopes", totals);
            check(ts.scopes[0].name == "AdapterFrame" && ts.scopes[1].name == "UploadAndClear",
                  "completedTimestamps scope names match", totals);
            check(ts.scopes[0].gpuMilliseconds.has_value() && *ts.scopes[0].gpuMilliseconds >= 0.0,
                  "completedTimestamps GPU ms valid", totals);
        }

        // Ephemeral buffer destroyed while recorded in flight -> must stay alive until GPU finishes!
        auto tempH = take(adapter.createBuffer({512, AdapterMemoryType::GpuOnly, false, "ephemeral"}), "temp buf", totals);
        auto tempPtr = take(adapter.resolve(tempH), "resolve temp", totals);
        auto atlasPtr = take(adapter.resolve(texH), "resolve atlas", totals);
        auto devPtr = take(adapter.resolve(devBufH), "resolve dev", totals);
        auto devRbPtr = take(adapter.resolve(devRbH), "resolve devRB", totals);
        auto rbPtr = take(adapter.resolve(rbH), "resolve atlasRB", totals);

        if (hasTs) take(cmd->beginTimestampScope("UploadAndClear"), "begin UploadAndClear", totals);

        // 1. Stage all CPU uploads into the frame's UploadRing segment BEFORE recording copy commands
        //    (preserves GraphicsSDK's strict observedVersion snapshot contract on Upload buffers).
        std::array<std::uint8_t, 128> pattern{};
        for (std::size_t i = 0; i < pattern.size(); ++i) {
            pattern[i] = static_cast<std::uint8_t>((frame * 37u + i) & 0xFFu);
        }
        auto bufSlice = take(adapter.uploadData(std::as_bytes(std::span{pattern}), 256), "upload bufSlice", totals);

        const auto tileFp = take(gfx::computeTextureRegionFootprint(gfx::Format::RGBA8UNorm, 8, 8, 0), "tile footprint", totals);
        std::vector<std::byte> tileBytes(static_cast<std::size_t>(tileFp.size), std::byte{0});
        const std::uint8_t rCol = static_cast<std::uint8_t>(50u + frame * 50u);
        const std::uint8_t gCol = static_cast<std::uint8_t>(200u - frame * 40u);
        const std::uint8_t bCol = static_cast<std::uint8_t>(30u + frame * 60u);
        for (std::uint32_t y = 0; y < 8u; ++y) {
            for (std::uint32_t x = 0; x < 8u; ++x) {
                const std::size_t off = static_cast<std::size_t>(y) * tileFp.rowPitch + static_cast<std::size_t>(x) * 4u;
                tileBytes[off + 0] = static_cast<std::byte>(rCol);
                tileBytes[off + 1] = static_cast<std::byte>(gCol);
                tileBytes[off + 2] = static_cast<std::byte>(bCol);
                tileBytes[off + 3] = static_cast<std::byte>(255u);
            }
        }
        auto tileSlice = take(adapter.uploadData(tileBytes, 512), "upload tileSlice", totals);

        // 2. Record offset buffer copies (devBuf + ephemeral tempBuf)
        take(cmd->transition(devPtr, gfx::ResourceState::CopyDestination), "dev -> CopyDst", totals);
        take(cmd->transition(tempPtr, gfx::ResourceState::CopyDestination), "temp -> CopyDst", totals);
        take(cmd->copyBufferRegion(bufSlice.buffer, bufSlice.offset, devPtr, static_cast<std::uint64_t>(frame) * 256u, 128u),
             "copy bufSlice -> dev", totals);
        take(cmd->copyBufferRegion(bufSlice.buffer, bufSlice.offset, tempPtr, 0u, 128u),
             "copy bufSlice -> temp", totals);

        // Destroy ephemeral handle immediately after recording (DeferredDestructionQueue holds it until fence retires!)
        tempPtr.reset();
        adapter.destroy(tempH);
        check(!adapter.alive(tempH), "ephemeral handle immediately invalid", totals);

        // 3. RenderPass clear + dynamic setViewport/setScissor on frame 0
        if (frame == 0u) {
            take(cmd->transition(atlasPtr, gfx::ResourceState::RenderTarget), "atlas -> RT", totals);
            take(cmd->beginRenderPass(atlasPtr, {0.0f, 0.0f, 0.0f, 1.0f}), "clear atlas", totals);
            take(cmd->setViewport({0.0f, 0.0f, 32.0f, 32.0f, 0.0f, 1.0f}), "setViewport", totals);
            take(cmd->setScissor(0, 0, 32, 32), "setScissor", totals);
            take(cmd->endRenderPass(), "end clear atlas", totals);
        }

        // 4. Partial 8x8 tile upload via UploadRing (512-byte aligned offset) into quadrant `frame`
        const std::uint32_t tileX = (frame % 2u) * 16u + 4u;
        const std::uint32_t tileY = (frame / 2u) * 16u + 4u;
        take(cmd->transition(atlasPtr, gfx::ResourceState::CopyDestination), "atlas -> CopyDst", totals);
        take(cmd->copyBufferToTextureRegion(tileSlice.buffer, atlasPtr,
             gfx::TextureCopyRegion{tileSlice.offset, tileFp.rowPitch, tileX, tileY, 8, 8}), "upload 8x8 tile", totals);

        if (frame == 3u) {
            // On final frame, copy both atlas and devBuf to readback buffers.
            take(cmd->transition(atlasPtr, gfx::ResourceState::CopySource), "atlas -> CopySrc", totals);
            take(cmd->copyTextureToBuffer(atlasPtr, rbPtr), "readback atlas", totals);
            take(cmd->transition(devPtr, gfx::ResourceState::CopySource), "dev -> CopySrc", totals);
            take(cmd->copyBufferRegion(devPtr, 0, devRbPtr, 0, 1024), "readback devBuf", totals);
        }

        if (hasTs) {
            take(cmd->endTimestampScope(), "end UploadAndClear", totals);
            take(cmd->endTimestampScope(), "end AdapterFrame", totals);
            take(cmd->resolveTimestampScopes(), "resolveTimestampScopes", totals);
            adapter.markFrameHasTimestampScopes();
        }
        (void)take(adapter.endFrame(), "endFrame", totals);
        ++totals.framesRendered;
    }

    take(adapter.waitIdle(), "waitIdle after 4 frames", totals);
    if (hasTs) {
        check(adapter.completedTimestamps().valid() && adapter.completedTimestamps().frameNumber == 4u,
              "final frame timestamps retired on waitIdle", totals);
    }

    // Verify all 4 128-byte sub-ranges in devBuf!
    const auto devBytes = take(adapter.readBuffer(devRbH, 0, 1024), "read devRB", totals);
    for (std::uint32_t frame = 0; frame < 4u; ++frame) {
        for (std::size_t i = 0; i < 128u; ++i) {
            const std::uint8_t got = static_cast<std::uint8_t>(devBytes[frame * 256u + i]);
            const std::uint8_t exp = static_cast<std::uint8_t>((frame * 37u + i) & 0xFFu);
            if (got != exp) {
                throw std::runtime_error("Offset buffer copy mismatch at frame " + std::to_string(frame));
            }
        }
    }
    ++totals.assertions;

    // Verify full 32x32 atlas image: 4 distinct 8x8 tiles + black (0,0,0,255) outside tiles!
    const auto pixels = take(adapter.readBuffer(rbH, 0, fp.size), "read atlasRB", totals);
    for (std::uint32_t y = 0; y < 32u; ++y) {
        for (std::uint32_t x = 0; x < 32u; ++x) {
            int er = 0, eg = 0, eb = 0, ea = 255;
            for (std::uint32_t frame = 0; frame < 4u; ++frame) {
                const std::uint32_t tx = (frame % 2u) * 16u + 4u;
                const std::uint32_t ty = (frame / 2u) * 16u + 4u;
                if (x >= tx && x < tx + 8u && y >= ty && y < ty + 8u) {
                    er = static_cast<int>(50u + frame * 50u);
                    eg = static_cast<int>(200u - frame * 40u);
                    eb = static_cast<int>(30u + frame * 60u);
                    break;
                }
            }
            const std::size_t off = static_cast<std::size_t>(y) * fp.rowPitch + static_cast<std::size_t>(x) * 4u;
            if (static_cast<int>(pixels[off + 0]) != er || static_cast<int>(pixels[off + 1]) != eg ||
                static_cast<int>(pixels[off + 2]) != eb || static_cast<int>(pixels[off + 3]) != ea) {
                throw std::runtime_error("Atlas pixel mismatch at (" + std::to_string(x) + "," + std::to_string(y) + ")");
            }
        }
    }
    ++totals.assertions;

    // Test UploadRing boundary (InvalidArgument) and real ring saturation (OutOfMemory -> uploadRingDeferred++).
    {
        auto* cmd = take(adapter.beginFrame(), "exhaust beginFrame", totals);
        (void)cmd;
        // Ring capacity is 1 MiB across 2 segments (512 KiB each):
        // 1) Request exceeding single segment capacity -> InvalidArgument.
        reject(adapter.allocateUpload(768u * 1024u, 256u), gfx::ErrorCode::InvalidArgument,
               "single-segment overflow rejected with InvalidArgument", totals);
        // 2) Saturate both 512 KiB segments within one uncommitted frame -> 3rd 400 KiB request returns OutOfMemory!
        (void)take(adapter.allocateUpload(400u * 1024u, 256u), "fill seg 0", totals);
        (void)take(adapter.allocateUpload(400u * 1024u, 256u), "fill seg 1", totals);
        reject(adapter.allocateUpload(400u * 1024u, 256u), gfx::ErrorCode::OutOfMemory,
               "saturated upload ring returns OutOfMemory and increments uploadRingDeferred", totals);
        const auto st = take(adapter.stats(), "stats after upload exhaust", totals);
        check(st.uploadRingDeferred >= 1u, "uploadRingDeferred incremented", totals);
        (void)take(adapter.endFrame(), "exhaust endFrame", totals);
        take(adapter.waitIdle(), "exhaust waitIdle", totals);
    }

    // Destroy all 4 user handles and verify all 4 native SDK resources are reclaimed after waitIdle().
    const auto beforeDestroyStats = take(adapter.stats(), "stats before user destroy", totals);
    adapter.destroy(texH);
    adapter.destroy(rbH);
    adapter.destroy(devBufH);
    adapter.destroy(devRbH);
    take(adapter.waitIdle(), "final waitIdle", totals);

    const auto finalStats = take(adapter.stats(), "final adapter stats", totals);
    check(finalStats.liveBuffers == 0u && finalStats.liveTextures == 0u && finalStats.livePipelines == 0u,
          "all adapter handles destroyed", totals);
    check(finalStats.pendingDestructions == 0u, "deferred destruction queue empty", totals);
    check(beforeDestroyStats.sdkUniqueResources >= finalStats.sdkUniqueResources + 4u,
          "all 4 user GPU resources reclaimed from SDK resourceStatistics", totals);
    take(adapter.nativeDevice().setProfilingEnabled(false), "disable profiling", totals);
}
} // namespace

int main(int argc, char** argv) {
    gfx::Backend backend = gfx::Backend::Vulkan;
    bool validation = false;
    bool warp = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--d3d12") backend = gfx::Backend::D3D12;
        else if (arg == "--vulkan") backend = gfx::Backend::Vulkan;
        else if (arg == "--validation") validation = true;
        else if (arg == "--warp") { backend = gfx::Backend::D3D12; warp = true; }
    }

    Totals totals{};
    try {
        AdapterDeviceDesc desc{};
        desc.backend = backend;
        desc.validation = validation;
        desc.softwareAdapter = warp;
        desc.enableDeviceLossTesting = warp;
        desc.framesInFlight = 2;
        desc.uploadRingBytes = 1u << 20;

        const auto adapters = gfx::enumerateAdapters(backend);
        if (!adapters || adapters->empty()) {
            std::cout << "SKIP: no adapter available\n";
            return 77;
        }
        if (!warp) {
            for (const auto& a : *adapters) {
                if (!a.softwareAdapter) { desc.adapter = a.id; break; }
            }
            if (!desc.adapter) desc.adapter = (*adapters)[0].id;
        } else {
            for (const auto& a : *adapters) {
                if (a.softwareAdapter) { desc.adapter = a.id; break; }
            }
        }

        auto devRes = AdapterDevice::create(desc);
        if (!devRes) {
            if (!warp && devRes.error().code == gfx::ErrorCode::Unsupported) {
                std::cout << "SKIP: " << devRes.error().message << "\n";
                return 77;
            }
            throw std::runtime_error("AdapterDevice::create failed: " + devRes.error().message);
        }
        auto adapter = std::move(*devRes);

        testHandlePoolAndGapRejections(*adapter, totals);
        testMultiFrameUploadRingPartialCopyViewportAndTimestamps(*adapter, totals);

        std::uint64_t valErrors = 0;
        if (validation) {
            valErrors = take(adapter->nativeDevice().validationErrorCount(), "validationErrorCount", totals);
            check(valErrors == 0u, "validationErrorCount must be 0", totals);
        }

        if (warp) {
            take(adapter->nativeDevice().removeForTesting(), "WARP removeForTesting", totals);
            reject(adapter->beginFrame(), gfx::ErrorCode::DeviceLost, "beginFrame after WARP loss returns DeviceLost", totals);
        }

        std::cout << "PASS sandbox_adapter backend="
                  << (backend == gfx::Backend::D3D12 ? "D3D12" : "Vulkan")
                  << " adapter=\"" << adapter->caps().adapterName << "\""
                  << " validation=" << (validation ? "on" : "off")
                  << " warp=" << (warp ? "on" : "off")
                  << " val_errors=" << valErrors
                  << " frames=" << totals.framesRendered
                  << " rejections=" << totals.rejections
                  << " assertions=" << totals.assertions << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "FAIL sandbox_adapter: " << ex.what() << "\n";
        return 1;
    }
}
