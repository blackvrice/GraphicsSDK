#include <graphics/Graphics.hpp>
#include "fixtures/SpirvFixtures.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
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

gfx::GraphicsPipelineDesc makeSolidPipelineDesc(
    gfx::Backend backend,
    const char* colorKind, // "red", "green", "half_red"
    bool alphaBlend,
    gfx::Format colorFormat,
    Totals& totals) {
    gfx::GraphicsPipelineDesc desc{};
    if (backend == gfx::Backend::D3D12) {
        const std::string vsSrc = R"(
float4 VS(uint id : SV_VertexID) : SV_Position {
    float2 p[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
    return float4(p[id % 3], 0.5, 1.0);
}
)";
        std::string psColor = "float4(1.0, 0.0, 0.0, 1.0)";
        if (std::string_view(colorKind) == "green") psColor = "float4(0.0, 1.0, 0.0, 1.0)";
        else if (std::string_view(colorKind) == "half_red") psColor = "float4(1.0, 0.0, 0.0, 0.5)";
        const std::string psSrc = "float4 PS() : SV_Target0 { return " + psColor + "; }\n";
        desc.vertex = take(gfx::compileHlsl(vsSrc, "VS", gfx::ShaderStage::Vertex), "compile HLSL VS", totals);
        desc.fragment = take(gfx::compileHlsl(psSrc, "PS", gfx::ShaderStage::Fragment), "compile HLSL PS", totals);
    } else {
        desc.vertex = spirvBinary(gfx::ShaderStage::Vertex, fixtures::fullscreen);
        if (std::string_view(colorKind) == "green") {
            desc.fragment = spirvBinary(gfx::ShaderStage::Fragment, fixtures::green);
        } else if (std::string_view(colorKind) == "half_red") {
            desc.fragment = spirvBinary(gfx::ShaderStage::Fragment, fixtures::half_red);
        } else {
            desc.fragment = spirvBinary(gfx::ShaderStage::Fragment, fixtures::red);
        }
    }
    desc.colorFormat = colorFormat;
    desc.alphaBlend = alphaBlend;
    return desc;
}

void renderAndVerifySolid(
    gfx::Device& device,
    const gfx::PipelinePtr& pipeline,
    std::uint8_t expR,
    std::uint8_t expG,
    std::uint8_t expB,
    std::uint8_t expA,
    const std::string& label,
    Totals& totals) {
    constexpr std::uint32_t kW = 64, kH = 64;
    gfx::TextureDesc td{kW, kH, gfx::Format::RGBA8UNorm, /*renderTarget=*/true};
    auto rt = take(device.createTexture(td), label + " create RT", totals);
    const auto fp = rt->footprint();
    auto rb = take(device.createBuffer({fp.size, gfx::Memory::Readback}), label + " create RB", totals);

    auto cmd = take(device.createCommandList(), label + " create cmd", totals);
    take(cmd->begin(), label + " begin", totals);
    take(cmd->transition(rt, gfx::ResourceState::RenderTarget), label + " RT transition", totals);
    gfx::RenderPassDesc rp{};
    rp.colors.push_back({rt, gfx::LoadOp::Clear, {0.0f, 0.0f, 0.0f, 1.0f}});
    take(cmd->beginRenderPass(rp), label + " beginRP", totals);
    take(cmd->setPipeline(pipeline), label + " setPipeline", totals);
    take(cmd->draw(3), label + " draw", totals);
    take(cmd->endRenderPass(), label + " endRP", totals);
    take(cmd->transition(rt, gfx::ResourceState::CopySource), label + " CopySrc", totals);
    take(cmd->copyTextureToBuffer(rt, rb), label + " copyTexToBuf", totals);
    take(cmd->end(), label + " end", totals);

    auto f = take(device.queue().submit(*cmd), label + " submit", totals);
    take(device.queue().wait(f, std::chrono::milliseconds(5000)), label + " wait", totals);
    const auto bytes = take(rb->read(0, fp.size), label + " readback", totals);

    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const std::size_t off = static_cast<std::size_t>(y) * fp.rowPitch + x * 4u;
            const auto r = std::to_integer<std::uint8_t>(bytes[off + 0]);
            const auto g = std::to_integer<std::uint8_t>(bytes[off + 1]);
            const auto b = std::to_integer<std::uint8_t>(bytes[off + 2]);
            const auto a = std::to_integer<std::uint8_t>(bytes[off + 3]);
            if (r != expR || g != expG || b != expB || a != expA) {
                throw std::runtime_error(label + " pixel mismatch at (" + std::to_string(x) + "," + std::to_string(y) +
                    "): got (" + std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) + "," + std::to_string(a) + ")");
            }
        }
    }
    ++totals.assertions;
}

std::uint64_t fnv1a64Local(const std::vector<std::byte>& data, std::size_t startOffset) {
    std::uint64_t h = 14695981039346656037ull;
    for (std::size_t i = startOffset; i < data.size(); ++i) {
        h ^= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(data[i]));
        h *= 1099511628211ull;
    }
    return h;
}

void runShaderCacheSuite(gfx::Backend backend, const gfx::AdapterInfo& adapter, bool validation, bool warp, Totals& totals) {
    const std::string tag = std::string(backend == gfx::Backend::D3D12 ? "D3D12/" : "Vulkan/") + adapter.name;
    std::cout << "=== Persistent Shader/Pipeline Cache " << tag << (validation ? " [validation]" : "") << " ===\n";

    const std::filesystem::path cacheDir = std::filesystem::temp_directory_path() / "gfx_p9_shader_cache_tests";
    std::filesystem::create_directories(cacheDir);
    const std::string safeSuffix = std::to_string(adapter.vendorId) + "_" + std::to_string(adapter.deviceId) +
        "_" + std::to_string(static_cast<int>(backend)) + (validation ? "_val" : "") + (warp ? "_warp" : "");
    const std::filesystem::path cacheFile = cacheDir / ("pipeline_cache_" + safeSuffix + ".bin");
    std::error_code ec;
    std::filesystem::remove(cacheFile, ec);

    gfx::DeviceDesc desc{};
    desc.backend = backend;
    desc.adapter = adapter.id;
    desc.softwareAdapter = warp;
    desc.validation = validation;

    // Build 8 distinct shader/pipeline variants for cold vs warm prewarming benchmark.
    std::vector<std::pair<gfx::ShaderVariantKey, gfx::GraphicsPipelineDesc>> variants;
    const std::array<const char*, 3> colors{"red", "green", "half_red"};
    const std::array<bool, 2> blends{false, true};
    const std::array<gfx::Format, 2> formats{gfx::Format::RGBA8UNorm, gfx::Format::BGRA8UNorm};
    for (std::size_t ci = 0; ci < colors.size(); ++ci) {
        for (bool bl : blends) {
            for (gfx::Format fmt : formats) {
                if (variants.size() >= 8) break;
                auto pDesc = makeSolidPipelineDesc(backend, colors[ci], bl, fmt, totals);
                gfx::ShaderVariantKey key{};
                key.name = std::string("solid_") + colors[ci];
                key.defines = {
                    {"ALPHA_BLEND", bl ? "1" : "0"},
                    {"FORMAT", fmt == gfx::Format::RGBA8UNorm ? "RGBA8" : "BGRA8"},
                };
                key.sourceHash = gfx::ShaderVariantCache::hashBytes(pDesc.fragment.bytes);
                variants.emplace_back(std::move(key), std::move(pDesc));
            }
        }
    }

    // 1. Cold device: load missing file (no-op), prewarm 8 variants, measure cold time, save to disk.
    double coldMs = 0.0;
    std::uint64_t savedBytes = 0;
    {
        auto devCold = take(gfx::createDevice(desc), "create cold device", totals);
        const auto fpCold = take(devCold->pipelineCacheFingerprint(), "cold fingerprint", totals);
        check(!fpCold.sdkVersion.empty() && fpCold.backend == backend, "valid cold fingerprint", totals);

        gfx::ShaderVariantCache cacheCold;
        const auto missingLoad = take(cacheCold.loadFromDisk(*devCold, cacheFile.string()), "load missing cache", totals);
        check(!missingLoad.loaded && !missingLoad.invalidated && !missingLoad.recoveredFromCorruption, "missing file is clean no-op", totals);

        const auto prewarmCold = take(cacheCold.prewarm(*devCold, variants), "cold prewarm", totals);
        check(prewarmCold.requested == 8 && prewarmCold.compiled == 8 && prewarmCold.failed == 0, "cold prewarm compiled 8/8", totals);
        coldMs = prewarmCold.elapsedMilliseconds;

        // Second prewarm in same cache must hit 8/8 in-memory.
        const auto prewarmMemHit = take(cacheCold.prewarm(*devCold, variants), "in-memory prewarm hit", totals);
        check(prewarmMemHit.cacheHits == 8 && prewarmMemHit.compiled == 0, "in-memory prewarm 8/8 cache hits", totals);

        // Verify cold rendering matches full 64x64 pixel reference.
        auto redPipe = cacheCold.find(variants[0].first);
        check(redPipe != nullptr, "find red variant", totals);
        renderAndVerifySolid(*devCold, redPipe, 255, 0, 0, 255, tag + "/ColdRed", totals);

        take(cacheCold.saveToDisk(*devCold, cacheFile.string()), "save pipeline cache to disk", totals);
        savedBytes = static_cast<std::uint64_t>(std::filesystem::file_size(cacheFile));
        check(savedBytes > 20, "saved cache file must contain header and native payload", totals);

        if (validation) {
            const auto v = take(devCold->validationErrorCount(), "cold validationErrorCount", totals);
            check(v == 0, "cold validationErrorCount == 0", totals);
        }
    }

    // 2. Warm device (brand new device instance): load disk cache, prewarm 8 variants, verify full image.
    double warmMs = 0.0;
    std::uint64_t loadedNativeBytes = 0;
    {
        auto devWarm = take(gfx::createDevice(desc), "create warm device", totals);
        gfx::ShaderVariantCache cacheWarm;
        const auto loadRes = take(cacheWarm.loadFromDisk(*devWarm, cacheFile.string()), "load warm cache", totals);
        check(loadRes.loaded && !loadRes.invalidated && !loadRes.recoveredFromCorruption, "warm cache loaded cleanly", totals);
        loadedNativeBytes = loadRes.bytesLoaded;

        const auto prewarmWarm = take(cacheWarm.prewarm(*devWarm, variants), "warm prewarm", totals);
        check(prewarmWarm.requested == 8 && prewarmWarm.compiled == 8 && prewarmWarm.failed == 0, "warm prewarm compiled 8/8", totals);
        warmMs = prewarmWarm.elapsedMilliseconds;

        auto redPipe = cacheWarm.find(variants[0].first);
        check(redPipe != nullptr, "find warm red variant", totals);
        renderAndVerifySolid(*devWarm, redPipe, 255, 0, 0, 255, tag + "/WarmRed", totals);

        // 3. Hot Reload: red -> green -> corrupted bytecode failure (must preserve green!).
        const auto redDesc = makeSolidPipelineDesc(backend, "red", false, gfx::Format::RGBA8UNorm, totals);
        const auto greenDesc = makeSolidPipelineDesc(backend, "green", false, gfx::Format::RGBA8UNorm, totals);
        gfx::ShaderVariantKey heroRed{"hero_material", {{"QUALITY", "HIGH"}}, gfx::ShaderVariantCache::hashBytes(redDesc.fragment.bytes)};
        gfx::ShaderVariantKey heroGreen{"hero_material", {{"QUALITY", "HIGH"}}, gfx::ShaderVariantCache::hashBytes(greenDesc.fragment.bytes)};
        gfx::ShaderVariantKey heroBad{"hero_material", {{"QUALITY", "HIGH"}}, 0xDEAD0001ull};

        auto hr1 = take(cacheWarm.getOrCompile(*devWarm, heroRed, redDesc), "compile hero red", totals);
        renderAndVerifySolid(*devWarm, hr1.pipeline, 255, 0, 0, 255, tag + "/HotReloadBefore", totals);

        auto hr2 = take(cacheWarm.hotReload(*devWarm, heroGreen, greenDesc), "hotReload hero -> green", totals);
        renderAndVerifySolid(*devWarm, hr2, 0, 255, 0, 255, tag + "/HotReloadAfter", totals);

        // Attempt hot reload with invalid bytecode: must fail AND preserve existing green pipeline!
        gfx::GraphicsPipelineDesc badDesc = greenDesc;
        badDesc.fragment.bytes = {std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}};
        auto badRes = cacheWarm.hotReload(*devWarm, heroBad, badDesc);
        check(!badRes.has_value(), "hotReload with invalid bytecode must fail", totals);
        ++totals.rejections;

        auto preserved = cacheWarm.find(heroGreen);
        check(preserved != nullptr, "previous valid variant must be preserved after failed hotReload", totals);
        renderAndVerifySolid(*devWarm, preserved, 0, 255, 0, 255, tag + "/HotReloadPreserved", totals);

        // 4. Driver/adapter fingerprint mismatch invalidation test.
        {
            std::ifstream ifs(cacheFile, std::ios::binary | std::ios::ate);
            const auto sz = ifs.tellg();
            std::vector<std::byte> raw(static_cast<std::size_t>(sz));
            ifs.seekg(0, std::ios::beg);
            ifs.read(reinterpret_cast<char*>(raw.data()), sz);
            // Flip driverFingerprint bit at body offset 12 (file offset 20 + 12 = 32) and recompute checksum at offset 12.
            raw[32] ^= std::byte{0x01};
            const std::uint64_t newChecksum = fnv1a64Local(raw, 20);
            std::memcpy(raw.data() + 12, &newChecksum, sizeof(newChecksum));
            const std::filesystem::path mismatchFile = cacheDir / ("mismatch_" + safeSuffix + ".bin");
            {
                std::ofstream ofs(mismatchFile, std::ios::binary | std::ios::trunc);
                ofs.write(reinterpret_cast<const char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
            }
            const auto invRes = take(devWarm->loadPipelineCacheFromFile(mismatchFile.string()), "load mismatched fingerprint cache", totals);
            check(!invRes.loaded && invRes.invalidated && !invRes.recoveredFromCorruption, "mismatched fingerprint must invalidate cleanly", totals);
            std::filesystem::remove(mismatchFile, ec);
        }

        // 5. Corrupted cache recovery tests (truncated, bad magic, 1-bit payload flip).
        {
            const std::filesystem::path corruptFile = cacheDir / ("corrupt_" + safeSuffix + ".bin");
            // 5a. Truncated file
            {
                std::ofstream ofs(corruptFile, std::ios::binary | std::ios::trunc);
                ofs.write("GFXPC001\x01\x00", 10);
            }
            auto r1 = take(devWarm->loadPipelineCacheFromFile(corruptFile.string()), "recover truncated cache", totals);
            check(!r1.loaded && r1.recoveredFromCorruption, "truncated cache must recover cleanly", totals);

            // 5b. Bad magic
            {
                std::ofstream ofs(corruptFile, std::ios::binary | std::ios::trunc);
                std::array<char, 64> junk{};
                std::memcpy(junk.data(), "BADMAGIC", 8);
                ofs.write(junk.data(), static_cast<std::streamsize>(junk.size()));
            }
            auto r2 = take(devWarm->loadPipelineCacheFromFile(corruptFile.string()), "recover bad magic cache", totals);
            check(!r2.loaded && r2.recoveredFromCorruption, "bad magic cache must recover cleanly", totals);

            // 5c. 1-bit flip without updating checksum
            {
                std::ifstream ifs(cacheFile, std::ios::binary | std::ios::ate);
                const auto sz = ifs.tellg();
                std::vector<std::byte> raw(static_cast<std::size_t>(sz));
                ifs.seekg(0, std::ios::beg);
                ifs.read(reinterpret_cast<char*>(raw.data()), sz);
                raw.back() ^= std::byte{0x80};
                std::ofstream ofs(corruptFile, std::ios::binary | std::ios::trunc);
                ofs.write(reinterpret_cast<const char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
            }
            auto r3 = take(devWarm->loadPipelineCacheFromFile(corruptFile.string()), "recover checksum mismatch cache", totals);
            check(!r3.loaded && r3.recoveredFromCorruption, "bit-flipped cache must recover cleanly", totals);
            std::filesystem::remove(corruptFile, ec);
        }

        if (validation) {
            const auto v = take(devWarm->validationErrorCount(), "warm validationErrorCount", totals);
            check(v == 0, "warm validationErrorCount == 0", totals);
            std::cout << "  [" << tag << "] validationErrorCount=0\n";
        }
    }

    std::filesystem::remove(cacheFile, ec);
    std::cout << std::fixed << std::setprecision(2)
              << "  [" << tag << "] 8-variant prewarm cold=" << coldMs << "ms warm=" << warmMs
              << "ms (file=" << savedBytes << "B, native=" << loadedNativeBytes << "B): PASS\n";
    ++totals.adapters;
}

} // namespace

int main(int argc, char** argv) {
    bool useD3D12 = false;
    bool validation = false;
    bool warp = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--d3d12") useD3D12 = true;
        else if (arg == "--vulkan") useD3D12 = false;
        else if (arg == "--validation") validation = true;
        else if (arg == "--warp") { warp = true; useD3D12 = true; }
    }

    try {
        Totals totals{};
        const gfx::Backend backend = useD3D12 ? gfx::Backend::D3D12 : gfx::Backend::Vulkan;
        auto adapters = take(gfx::enumerateAdapters(backend), "enumerateAdapters", totals);
        for (const auto& a : adapters) {
            if (warp ? !a.softwareAdapter : a.softwareAdapter) continue;
            runShaderCacheSuite(backend, a, validation, warp, totals);
        }
        check(totals.adapters > 0, "Expected at least one matching adapter", totals);
        std::cout << "PASS gfx_shader_cache_tests: adapters=" << totals.adapters
                  << " assertions=" << totals.assertions
                  << " rejections=" << totals.rejections << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
