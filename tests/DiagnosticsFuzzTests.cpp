#include <graphics/Graphics.hpp>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {

int gAssertions = 0;
int gRejections = 0;
int gFuzzCases = 0;

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

bool verifyBreadcrumbsAndLeakDetection(gfx::Device& device, const std::string& label) {
    device.clearBreadcrumbs();

    // 1. Invalid breadcrumb rejections.
    if (!requireError(device.recordBreadcrumb(""), gfx::ErrorCode::InvalidArgument, "empty breadcrumb label")) return false;
    if (!requireError(device.recordBreadcrumb(std::string(300, 'x')), gfx::ErrorCode::InvalidArgument, "oversized breadcrumb label")) return false;
    if (!requireError(device.recordBreadcrumb("bad_q", static_cast<gfx::QueueType>(99)), gfx::ErrorCode::InvalidArgument, "unknown queue breadcrumb")) return false;

    // 2. Real GPU submission paired with RenderDoc/PIX debugMarker + breadcrumb completion tracking.
    {
        auto targetRes = device.createTexture({64, 64, gfx::Format::RGBA8UNorm, true});
        if (!require(targetRes.has_value(), "create marker target")) return false;
        auto buf1 = device.createBuffer({4096, gfx::Memory::Device});
        auto buf2 = device.createBuffer({4096, gfx::Memory::Upload});
        if (!require(buf1.has_value() && buf2.has_value(), "create leak-probe buffers")) return false;

        // Intentional leak detection check while resources are live.
        const auto leakRep = device.diagnosticReport();
        if (!require(leakRep.hasLeaks && leakRep.liveUniqueResources >= 3u && leakRep.liveAllocationBytes > 0u,
                     "diagnosticReport must detect live unreleased resources")) return false;
        if (!require(!leakRep.deviceLost && leakRep.summary.find("hasLeaks=true") != std::string::npos,
                     "diagnosticReport summary must flag hasLeaks=true")) return false;

        auto cmdRes = device.createCommandList();
        if (!require(cmdRes.has_value(), "create marker cmd")) return false;
        auto& cmd = **cmdRes;
        if (!require(cmd.begin().has_value(), "cmd begin")) return false;
        if (!require(cmd.debugMarker("PIX_RenderDoc: FrameStart").has_value(), "debugMarker FrameStart")) return false;
        if (!require(cmd.transition(*targetRes, gfx::ResourceState::RenderTarget).has_value(), "tr RT")) return false;
        gfx::RenderPassDesc rp{};
        rp.colors = {gfx::ColorAttachment{*targetRes, gfx::LoadOp::Clear, {0.2f, 0.4f, 0.6f, 1.0f}}};
        if (!require(cmd.beginRenderPass(rp).has_value(), "beginRP")) return false;
        if (!require(cmd.debugMarker("PIX_RenderDoc: MainPass").has_value(), "debugMarker MainPass")) return false;
        if (!require(cmd.endRenderPass().has_value(), "endRP")) return false;
        if (!require(cmd.debugMarker("PIX_RenderDoc: FrameEnd").has_value(), "debugMarker FrameEnd")) return false;
        if (!require(cmd.end().has_value(), "cmd end")) return false;

        auto fence = device.queue().submit(cmd);
        if (!require(fence.has_value(), "submit marker cmd")) return false;
        auto bcSeq = device.recordBreadcrumb("Pass:MainClear", gfx::QueueType::Graphics, fence->value);
        if (!require(bcSeq.has_value() && *bcSeq >= 1u, "recordBreadcrumb MainClear")) return false;
        if (!require(device.queue().wait(*fence).has_value(), "wait marker cmd")) return false;

        const auto afterWork = device.diagnosticReport();
        if (!require(!afterWork.breadcrumbs.empty() && afterWork.breadcrumbs.back().completed,
                     "completed GPU fence must mark breadcrumb completed=true")) return false;
    }

    // 3. After releasing all resources, leak detection must report clean (hasLeaks=false).
    if (!require(device.queue().waitIdle().has_value(), "waitIdle")) return false;
    const auto cleanRep = device.diagnosticReport();
    if (!require(!cleanRep.hasLeaks && cleanRep.liveUniqueResources == 0u && cleanRep.liveTrackedObjects == 0u,
                 "diagnosticReport must report zero leaks after release: " + cleanRep.summary)) return false;

    // 4. Ring buffer bound check (160 entries -> capped at kMaxCrashBreadcrumbs = 128).
    device.clearBreadcrumbs();
    for (int i = 0; i < 160; ++i) {
        auto seq = device.recordBreadcrumb("RingEntry_" + std::to_string(i), gfx::QueueType::Graphics, 0u);
        if (!require(seq.has_value(), "record ring breadcrumb")) return false;
    }
    const auto ringRep = device.diagnosticReport();
    if (!require(ringRep.breadcrumbs.size() == gfx::kMaxCrashBreadcrumbs,
                 "breadcrumb ring must be capped at kMaxCrashBreadcrumbs")) return false;
    if (!require(ringRep.breadcrumbs.front().label == "RingEntry_32" &&
                 ringRep.breadcrumbs.back().label == "RingEntry_159",
                 "breadcrumb ring must keep newest 128 entries")) return false;
    device.clearBreadcrumbs();

    std::cout << "  [" << label << "] breadcrumbs + PIX/RenderDoc markers + leak detection: PASS\n";
    return true;
}

bool verifyDeterministicInputFuzz(gfx::Device& device, const std::string& label) {
    std::mt19937_64 rng(0xC0FFEE11ULL);
    auto randU32 = [&](std::uint32_t lo, std::uint32_t hi) {
        return std::uniform_int_distribution<std::uint32_t>(lo, hi)(rng);
    };
    auto randFloat = [&](float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    };

    // 1. Fuzz createBuffer with adversarial sizes & invalid enums (100 cases).
    for (int i = 0; i < 100; ++i) {
        gfx::BufferDesc bd{};
        const int mode = i % 4;
        if (mode == 0) bd.size = 0u;
        else if (mode == 1) bd.size = UINT64_MAX - randU32(0u, 1024u);
        else if (mode == 2) { bd.size = 256u; bd.memory = static_cast<gfx::Memory>(3u + randU32(0u, 20u)); }
        else { bd.size = 256u; bd.memory = gfx::Memory::Upload; bd.storage = true; }
        auto res = device.createBuffer(bd);
        if (!require(!res.has_value(), "Fuzzed invalid BufferDesc must fail cleanly")) return false;
        ++gRejections;
        ++gFuzzCases;
    }

    // 2. Fuzz createTexture with invalid dimensions, formats, sampleCounts, conflicts (150 cases).
    for (int i = 0; i < 150; ++i) {
        gfx::TextureDesc td{};
        const int mode = i % 6;
        if (mode == 0) { td.width = 0u; td.height = randU32(0u, 64u); }
        else if (mode == 1) { td.width = 65536u + randU32(1u, 10000u); td.height = 16u; }
        else if (mode == 2) { td.width = 16u; td.height = 16u; td.format = static_cast<gfx::Format>(100u + randU32(0u, 50u)); }
        else if (mode == 3) { td.width = 16u; td.height = 16u; td.sampleCount = 3u + (randU32(0u, 10u) * 2u); td.renderTarget = true; }
        else if (mode == 4) { td.width = 16u; td.height = 16u; td.format = gfx::Format::D32Float; td.depthStencil = true; td.renderTarget = true; }
        else { td.width = 16u; td.height = 16u; td.dimension = static_cast<gfx::TextureDimension>(10u + randU32(0u, 20u)); }
        auto res = device.createTexture(td);
        if (!require(!res.has_value(), "Fuzzed invalid TextureDesc must fail cleanly")) return false;
        ++gRejections;
        ++gFuzzCases;
    }

    // 3. Fuzz copyBufferRegion with overflow/out-of-bounds offsets (100 cases).
    {
        auto src = device.createBuffer({512u, gfx::Memory::Upload});
        auto dst = device.createBuffer({512u, gfx::Memory::Device});
        auto cmd = device.createCommandList();
        if (!require(src.has_value() && dst.has_value() && cmd.has_value(), "fuzz copy setup")) return false;
        if (!require((*cmd)->begin().has_value(), "fuzz copy begin")) return false;
        if (!require((*cmd)->transition(*dst, gfx::ResourceState::CopyDestination).has_value(), "fuzz copy tr")) return false;
        for (int i = 0; i < 100; ++i) {
            const int mode = i % 4;
            std::uint64_t sOff = 0, dOff = 0, sz = 0;
            if (mode == 0) { sz = 0u; }
            else if (mode == 1) { sOff = UINT64_MAX - 16u; sz = 64u; }
            else if (mode == 2) { dOff = 500u; sz = 64u; }
            else { sOff = 256u; dOff = 256u; sz = 300u; }
            auto r = (*cmd)->copyBufferRegion(*src, sOff, *dst, dOff, sz);
            if (!require(!r.has_value(), "Fuzzed invalid copyBufferRegion must fail cleanly")) return false;
            ++gRejections;
            ++gFuzzCases;
        }
        if (!require((*cmd)->end().has_value(), "fuzz copy end")) return false;
    }

    // 4. Fuzz validateViewport with NaN, Inf, negative, out-of-bounds (150 cases).
    for (int i = 0; i < 150; ++i) {
        gfx::Viewport vp{};
        const int mode = i % 5;
        if (mode == 0) { vp = {std::numeric_limits<float>::quiet_NaN(), 0.0f, 16.0f, 16.0f, 0.0f, 1.0f}; }
        else if (mode == 1) { vp = {0.0f, 0.0f, std::numeric_limits<float>::infinity(), 16.0f, 0.0f, 1.0f}; }
        else if (mode == 2) { vp = {-randFloat(1.0f, 50.0f), 0.0f, 16.0f, 16.0f, 0.0f, 1.0f}; }
        else if (mode == 3) { vp = {0.0f, 0.0f, 16.0f, 16.0f, 0.8f, 0.2f}; }
        else { vp = {200.0f, 200.0f, 100.0f, 100.0f, 0.0f, 1.0f}; }
        auto r = gfx::validateViewport(vp, 128u, 128u);
        if (!require(!r.has_value(), "Fuzzed invalid Viewport must fail cleanly")) return false;
        ++gRejections;
        ++gFuzzCases;
    }

    const auto postFuzz = device.diagnosticReport();
    if (!require(!postFuzz.hasLeaks && postFuzz.liveUniqueResources == 0u,
                 "Fuzz suite must leave zero leaked resources")) return false;
    std::cout << "  [" << label << "] deterministic input fuzz (" << gFuzzCases << " cumulative cases): PASS\n";
    return true;
}

bool verifyFaultInjectionAndRecovery(gfx::Device& device, const std::string& label) {
    device.clearBreadcrumbs();
    // Record an uncompleted GPU breadcrumb before injecting DeviceLost fault.
    auto bc = device.recordBreadcrumb("CrashSite:ShadowPass", gfx::QueueType::Graphics, 999999u);
    if (!require(bc.has_value(), "record crash breadcrumb")) return false;
    if (!require(device.removeForTesting().has_value(), "removeForTesting fault injection")) return false;
    if (!requireError(device.checkStatus(), gfx::ErrorCode::DeviceLost, "checkStatus after fault injection")) return false;

    // diagnosticReport() must remain callable after DeviceLost and surface the uncompleted breadcrumb.
    const auto crashRep = device.diagnosticReport();
    if (!require(crashRep.deviceLost, "diagnosticReport must report deviceLost=true")) return false;
    if (!require(crashRep.breadcrumbs.size() == 1u && !crashRep.breadcrumbs[0].completed &&
                 crashRep.breadcrumbs[0].label == "CrashSite:ShadowPass",
                 "diagnosticReport must preserve uncompleted crash breadcrumb")) return false;
    if (!require(crashRep.summary.find("CrashSite:ShadowPass") != std::string::npos,
                 "diagnosticReport summary must include lastIncomplete label")) return false;

    auto recovered = device.recover();
    if (!require(recovered.has_value(), "recover() after fault injection")) return false;
    const auto recRep = (*recovered)->diagnosticReport();
    if (!require(!recRep.deviceLost && !recRep.hasLeaks, "recovered device must be healthy with zero leaks")) return false;
    std::cout << "  [" << label << "] fault injection + post-crash breadcrumb + recovery: PASS (" << crashRep.summary << ")\n";
    return true;
}

bool runAdapter(gfx::Backend backend, const gfx::AdapterInfo& info, bool validation, bool testFaultInjection) {
    gfx::DeviceDesc dd{};
    dd.backend = backend;
    dd.validation = validation;
    dd.softwareAdapter = info.softwareAdapter;
    dd.enableDeviceLossTesting = testFaultInjection;
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

    if (!verifyBreadcrumbsAndLeakDetection(dev, label)) return false;
    if (!verifyDeterministicInputFuzz(dev, label)) return false;
    if (testFaultInjection) {
        if (!verifyFaultInjectionAndRecovery(dev, label)) return false;
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
    std::cout << "PASS gfx_diagnostics_fuzz_tests: adapters=" << ran
              << " assertions=" << gAssertions << " rejections=" << gRejections
              << " fuzzCases=" << gFuzzCases << "\n";
    return 0;
}
