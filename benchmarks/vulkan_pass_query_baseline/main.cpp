#include "Adapter.hpp"
#include "Identity.hpp"
#include "Observer.hpp"
#include <graphics/Graphics.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>

#ifndef NDEBUG
#error Release required
#endif

namespace {
using Clock = std::chrono::steady_clock;
using gfx::ResourceState;
using passQuery::Counts;
using passQuery::Objects;
constexpr unsigned width = 33, height = 9, warmup = 4, repetitions = 16, checks = 3;
using Pixel = std::array<unsigned, 4>;
using Sample = std::array<std::int64_t, 6>;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class T> T take(gfx::Result<T> result) {
    if (!result) throw std::runtime_error(result.error().message);
    return std::move(*result);
}
void take(gfx::Result<void> result) {
    if (!result) throw std::runtime_error(result.error().message);
}
std::int64_t elapsed(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
}

class Observer {
public:
    explicit Observer(bool enabled) {
        const auto module = GetModuleHandleW(L"VkLayer_gfx_pass_query_baseline.dll");
        check(bool(module) == enabled, "Unexpected pass/query observer state");
        if (!enabled) return;
        wchar_t actual[32768]{}, expected[32768]{};
        const auto actualSize = GetModuleFileNameW(module, actual, static_cast<DWORD>(std::size(actual)));
        const auto expectedSize = GetEnvironmentVariableW(L"GFX_EXPECT_PASS_QUERY_LAYER", expected,
                                                           static_cast<DWORD>(std::size(expected)));
        check(actualSize && actualSize < std::size(actual) && expectedSize && expectedSize < std::size(expected) &&
              std::filesystem::equivalent(actual, expected), "Unexpected pass/query layer module path");
        query_ = reinterpret_cast<passQuery::Query>(GetProcAddress(module, "gfxPassQuerySnapshot"));
        check(query_ != nullptr, "Missing pass/query observer query");
    }
    Counts verify(unsigned recordings, unsigned passes, unsigned timedRecordings, unsigned reads,
                  unsigned liveQueries, unsigned livePasses) const {
        if (!query_) return {};
        Counts counts;
        const Objects queryPools{recordings, recordings - liveQueries, liveQueries};
        const Objects passObjects{passes, passes - livePasses, livePasses};
        check(query_(&counts, sizeof(counts)) && counts == Counts{
                  queryPools, passObjects, passObjects, recordings, 2 * timedRecordings, reads, 0},
              "Native pass/query call or ownership mismatch");
        return counts;
    }
private:
    passQuery::Query query_{};
};

Pixel pixel(unsigned iteration, unsigned pass) {
    return {(iteration * 43 + pass * 17) % 256, (iteration * 7 + pass * 71) % 256,
            (iteration * 31 + pass * 19 + 1) % 256, 255};
}

struct Workload {
    gfx::TexturePtr target;
    std::vector<gfx::BufferPtr> readbacks;
    gfx::TextureFootprint footprint;
};
Workload setup(gfx::Device& device, unsigned passes) {
    Workload work;
    work.target = take(device.createTexture({width, height, gfx::Format::RGBA8UNorm, true}));
    work.footprint = work.target->footprint();
    check(work.footprint.rowBytes == width * 4 && work.footprint.height == height &&
          work.footprint.rowPitch >= width * 4 && work.footprint.size >= std::uint64_t(work.footprint.rowPitch) * height,
          "Unexpected texture footprint");
    for (unsigned i = 0; i < passes; ++i)
        work.readbacks.push_back(take(device.createBuffer({work.footprint.size, gfx::Memory::Readback})));
    return work;
}

void record(gfx::CommandList& commands, const Workload& work, unsigned passes, unsigned iteration, bool timestamps,
            bool timed, Sample& sample) {
    const auto start = timed ? Clock::now() : Clock::time_point{};
    take(commands.begin());
    if (timed) sample[0] = elapsed(start);
    if (timestamps) take(commands.writeTimestamp(0));
    for (unsigned i = 0; i < passes; ++i) {
        const auto rgba = pixel(iteration, i);
        const gfx::Color clear{float(rgba[0]) / 255.f, float(rgba[1]) / 255.f, float(rgba[2]) / 255.f, 1};
        take(commands.transition(work.target, ResourceState::RenderTarget));
        const auto startPass = timed ? Clock::now() : Clock::time_point{};
        take(commands.beginRenderPass(work.target, clear));
        if (timed) sample[1] += elapsed(startPass);
        take(commands.endRenderPass());
        take(commands.transition(work.target, ResourceState::CopySource));
        take(commands.transition(work.readbacks[i], ResourceState::CopyDestination));
        take(commands.copyTextureToBuffer(work.target, work.readbacks[i]));
    }
    if (timestamps) {
        take(commands.writeTimestamp(1));
        take(commands.resolveTimestamps());
    }
    take(commands.end());
    if (timed) sample[2] = elapsed(start);
}

void verify(const Workload& work, unsigned iteration) {
    for (unsigned pass = 0; pass < work.readbacks.size(); ++pass) {
        const auto data = take(work.readbacks[pass]->read(0, work.footprint.size));
        check(data.size() == work.footprint.size, "Incomplete mapped readback");
        const auto expected = pixel(iteration, pass);
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x)
                for (unsigned c = 0; c < expected.size(); ++c) {
                    const auto at = std::size_t(y) * work.footprint.rowPitch + x * 4 + c;
                    check(std::to_integer<unsigned>(data[at]) == expected[c], "Full pass image differs");
                }
    }
}

void rejectUnready(gfx::CommandList& commands) {
    const auto result = commands.elapsedGpuMilliseconds();
    check(!result && result.error().code == gfx::ErrorCode::InvalidState, "Unexpected timestamp result");
}

void writeObjects(passQuery::Objects objects) {
    std::cout << '[' << objects.created << ',' << objects.destroyed << ',' << objects.live << ']';
}
}

int main(int argc, char** argv) try {
    using bench::quote;
    const auto adapters = take(gfx::enumerateAdapters(gfx::Backend::Vulkan));
    if (argc == 2 && std::string_view(argv[1]) == "--probe") {
        std::cout << '[';
        bool comma{};
        for (const auto& adapter : adapters) {
            if (adapter.softwareAdapter) continue;
            std::cout << (comma ? "," : "") << quote(adapter.name);
            comma = true;
        }
        std::cout << "]\n";
        return 0;
    }
    check(argc == 5, "Expected ADAPTER PASSES plain|timestamp timing|observe|validation");
    const std::string_view passText = argv[2], profile = argv[3], mode = argv[4];
    check(passText == "1" || passText == "16", "Expected 1 or 16 passes");
    check(profile == "plain" || profile == "timestamp", "Unknown query profile");
    check(mode == "timing" || mode == "observe" || mode == "validation", "Unknown run mode");
    const unsigned passCount = passText == "1" ? 1 : 16;
    const bool timing = mode == "timing", validation = mode == "validation", timestamps = profile == "timestamp";
    const auto sameName = [&](const auto& adapter) { return adapter.name == argv[1]; };
    const auto adapter = std::ranges::find_if(adapters, sameName);
    check(adapter != adapters.end() && !adapter->softwareAdapter &&
          std::ranges::count_if(adapters, sameName) == 1, "Expected a unique hardware adapter");
    const auto identity = upload::identify(adapter->name);
    gfx::DeviceDesc desc;
    desc.backend = gfx::Backend::Vulkan;
    desc.adapter = adapter->id;
    desc.validation = validation;
    auto device = take(gfx::createDevice(desc));
    check(device->caps().adapterId == adapter->id && device->caps().timestampQueries,
          "Expected exact adapter with timestamp support");
    check(bool(GetModuleHandleW(L"VkLayer_khronos_validation.dll")) == validation, "Unexpected validation layer state");
    const Observer observer(mode == "observe");

    auto work = setup(*device, passCount);
    const unsigned discarded = timing ? 0 : 1;
    if (discarded) {
        auto commands = take(device->createCommandList());
        Sample ignored{};
        record(*commands, work, 1, 0, false, false, ignored);
        rejectUnready(*commands);
        observer.verify(1, 1, 0, 0, 1, 1);
        commands.reset();
        observer.verify(1, 1, 0, 0, 0, 0);
    }
    const auto batches = timing ? warmup + repetitions : checks;
    std::vector<Sample> samples;
    std::vector<std::int64_t> gpuSamples;
    samples.reserve(repetitions);
    for (unsigned i = 0; i < batches; ++i) {
        auto commands = take(device->createCommandList());
        Sample sample{};
        const bool timed = timing && i >= warmup;
        record(*commands, work, passCount, i + 1, timestamps, timed, sample);
        if (!timing) rejectUnready(*commands);
        const auto recordings = discarded + i + 1;
        const auto passes = discarded + (i + 1) * passCount;
        const auto writes = timestamps ? i + 1 : 0;
        const auto fence = take(device->queue().submit(*commands));
        observer.verify(recordings, passes, writes, timestamps ? i : 0, 1, passCount);
        const auto waitStart = timed ? Clock::now() : Clock::time_point{};
        take(device->queue().wait(fence));
        if (timed) sample[3] = elapsed(waitStart);
        observer.verify(recordings, passes, writes, timestamps ? i : 0, 1, passCount);
        if (timestamps) {
            const auto readStart = timed ? Clock::now() : Clock::time_point{};
            const auto gpuMs = take(commands->elapsedGpuMilliseconds());
            if (timed) sample[4] = elapsed(readStart);
            check(std::isfinite(gpuMs) && gpuMs >= 0 && gpuMs <= 30000, "Invalid GPU timestamp interval");
            if (!timing) check(take(commands->elapsedGpuMilliseconds()) == gpuMs, "Cached timestamp result differs");
            gpuSamples.push_back(std::llround(gpuMs * 1000000.));
        } else if (!timing) {
            rejectUnready(*commands);
        }
        const auto dropStart = timed ? Clock::now() : Clock::time_point{};
        commands.reset();
        if (timed) sample[5] = elapsed(dropStart);
        observer.verify(recordings, passes, writes, writes, 0, 0);
        verify(work, i + 1);
        if (timed) samples.push_back(sample);
    }
    work = {};
    take(device->queue().waitIdle());
    const auto stats = take(device->resourceStatistics());
    check(!stats.trackedObjects && !stats.uniqueResources && !stats.resourceAllocationBytes &&
          !stats.pendingSubmissions && !stats.pendingObjectReferences, "Final SDK ownership remains");
    if (validation) check(take(device->validationErrorCount()) == 0, "Vulkan validation errors");
    const auto native = observer.verify(discarded + batches, discarded + batches * passCount,
                                        timestamps ? batches : 0, timestamps ? batches : 0, 0, 0);
    device.reset();
    const auto images = batches * passCount;
    std::cout << "{\"sdk\":" << quote(gfx::buildFingerprint()) << ",\"adapter\":" << quote(adapter->name)
              << ",\"device_uuid\":" << quote(identity.uuid) << ",\"driver_uuid\":" << quote(identity.driverUuid)
              << ",\"driver\":" << identity.driver << ",\"passes\":" << passCount
              << ",\"mode\":" << quote(mode) << ",\"profile\":" << quote(profile)
              << ",\"verified_images\":" << images << ",\"verified_bytes\":" << images * width * height * 4
              << ",\"discarded_recordings\":" << discarded << ",\"final_sdk_ownership_zero\":true"
              << ",\"validation_errors\":" << (validation ? "0" : "null") << ",\"gpu_ns\":[";
    for (std::size_t i = 0; i < gpuSamples.size(); ++i) std::cout << (i ? "," : "") << gpuSamples[i];
    std::cout << "],\"samples\":[";
    for (std::size_t i = 0; i < samples.size(); ++i) {
        std::cout << (i ? ",[" : "[");
        for (std::size_t j = 0; j < samples[i].size(); ++j) std::cout << (j ? "," : "") << samples[i][j];
        std::cout << ']';
    }
    std::cout << ']';
    if (mode == "observe") {
        std::cout << ",\"native\":{\"queries\":";
        writeObjects(native.queries);
        std::cout << ",\"passes\":";
        writeObjects(native.passes);
        std::cout << ",\"framebuffers\":";
        writeObjects(native.framebuffers);
        std::cout << ",\"resets\":" << native.resets << ",\"writes\":" << native.writes
                  << ",\"reads\":" << native.reads << ",\"errors\":" << native.errors << '}';
    }
    std::cout << "}\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
