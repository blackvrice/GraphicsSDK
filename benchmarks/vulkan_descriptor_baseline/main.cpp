#include "Adapter.hpp"
#include "Identity.hpp"
#include "Observer.hpp"
#include "VulkanBindingSupport.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>

#ifndef NDEBUG
#error Release required
#endif

namespace {
using Clock = std::chrono::steady_clock;
using gfx::ResourceState;
using descriptorBaseline::Counts;
constexpr unsigned width = 256, height = 7, capacity = 256, warmup = 4, repetitions = 16, checks = 3;
constexpr std::array colors{gfx::Color{1, 0, 0, 1}, gfx::Color{0, 1, 0, 1}};
constexpr std::array pixels{std::array{255u, 0u, 0u, 255u}, std::array{0u, 255u, 0u, 255u}};
using Sample = std::array<std::int64_t, 4>;

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
        const auto module = GetModuleHandleW(L"VkLayer_gfx_descriptor_baseline.dll");
        check(bool(module) == enabled, "Unexpected descriptor observer state");
        if (!enabled) return;
        wchar_t actual[32768]{}, expected[32768]{};
        const auto actualLength = GetModuleFileNameW(module, actual, static_cast<DWORD>(std::size(actual)));
        const auto expectedLength = GetEnvironmentVariableW(L"GFX_EXPECT_DESCRIPTOR_LAYER", expected,
                                                             static_cast<DWORD>(std::size(expected)));
        check(actualLength && actualLength < std::size(actual) && expectedLength && expectedLength < std::size(expected) &&
              std::filesystem::equivalent(actual, expected), "Unexpected descriptor layer module path");
        query_ = reinterpret_cast<descriptorBaseline::Query>(GetProcAddress(module, "gfxDescriptorSnapshot"));
        check(query_ != nullptr, "Missing descriptor observer query");
    }
    Counts verify(unsigned created, unsigned destroyed) const {
        if (!query_) return {};
        Counts counts;
        check(query_(&counts, sizeof(counts)) && counts == Counts{
                  created, created, created, created, destroyed, created - destroyed, 0},
              "Descriptor native call or ownership mismatch");
        return counts;
    }
private:
    descriptorBaseline::Query query_{};
};

struct Workload {
    gfx::PipelinePtr pipeline;
    std::array<gfx::ResourceBindings, 2> bindings;
    gfx::TexturePtr target;
    gfx::BufferPtr readback;
    gfx::TextureFootprint footprint;
};

Workload setup(gfx::Device& device) {
    Workload work;
    work.pipeline = take(device.createGraphicsPipeline(bindingTest::pipeline()));
    auto commands = take(device.createCommandList());
    take(commands->begin());
    for (unsigned i = 0; i < colors.size(); ++i) {
        const auto texture = take(device.createTexture({1, 1, gfx::Format::RGBA8UNorm, true}));
        take(commands->transition(texture, ResourceState::RenderTarget));
        take(commands->beginRenderPass(texture, colors[i]));
        take(commands->endRenderPass());
        take(commands->transition(texture, ResourceState::ShaderRead));
        work.bindings[i].textures.push_back({0, 0, texture});
    }
    take(commands->end());
    take(device.queue().wait(take(device.queue().submit(*commands))));
    work.target = take(device.createTexture({width, height, gfx::Format::RGBA8UNorm, true}));
    work.footprint = work.target->footprint();
    check(work.footprint.rowBytes == width * 4 && work.footprint.height == height &&
          work.footprint.rowPitch >= width * 4 && work.footprint.size >= std::uint64_t(work.footprint.rowPitch) * height,
          "Unexpected texture footprint");
    work.readback = take(device.createBuffer({work.footprint.size, gfx::Memory::Readback}));
    return work;
}

void record(gfx::CommandList& commands, const Workload& work, unsigned count, unsigned first, bool alternating,
            bool capacityCheck, bool timed, Sample& sample) {
    const auto start = timed ? Clock::now() : Clock::time_point{};
    take(commands.begin());
    take(commands.transition(work.target, ResourceState::RenderTarget));
    take(commands.beginRenderPass(work.target, {0, 0, 1, 1}));
    take(commands.setPipeline(work.pipeline));
    const unsigned stripe = width / count;
    for (unsigned i = 0; i < count; ++i) {
        const unsigned binding = first ^ (alternating ? i % 2 : 0);
        const auto beginBind = timed ? Clock::now() : Clock::time_point{};
        take(commands.bindResources(work.bindings[binding]));
        if (timed) sample[0] += elapsed(beginBind);
        if (capacityCheck && i + 1 == count) {
            const auto extra = commands.bindResources(work.bindings[1 - binding]);
            check(!extra && extra.error().code == gfx::ErrorCode::InvalidState, "Snapshot capacity rejection missing");
        }
        take(commands.setScissor(i * stripe, 0, stripe, height));
        take(commands.draw(3));
    }
    take(commands.endRenderPass());
    take(commands.transition(work.target, ResourceState::CopySource));
    take(commands.transition(work.readback, ResourceState::CopyDestination));
    take(commands.copyTextureToBuffer(work.target, work.readback));
    take(commands.end());
    if (timed) sample[1] = elapsed(start);
}

void verify(const Workload& work, unsigned count, unsigned first, bool alternating) {
    const auto data = take(work.readback->read(0, work.footprint.size));
    check(data.size() == work.footprint.size, "Incomplete mapped readback");
    const auto stripe = width / count;
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            const auto& expected = pixels[first ^ (alternating ? (x / stripe) % 2 : 0)];
            const auto at = std::size_t(y) * work.footprint.rowPitch + x * 4;
            for (unsigned channel = 0; channel < expected.size(); ++channel)
                check(std::to_integer<unsigned>(data[at + channel]) == expected[channel], "Full image differs");
        }
    }
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
    check(argc == 5, "Expected ADAPTER COUNT same|alternating timing|observe|validation");
    const std::string_view countText = argv[2], pattern = argv[3], mode = argv[4];
    check(countText == "1" || countText == "64" || countText == "256", "Expected 1, 64 or 256 bindings");
    check(pattern == "same" || pattern == "alternating", "Unknown binding pattern");
    check(mode == "timing" || mode == "observe" || mode == "validation", "Unknown run mode");
    const unsigned count = countText == "1" ? 1 : countText == "64" ? 64 : 256;
    const bool timing = mode == "timing", validation = mode == "validation", alternating = pattern == "alternating";
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
    check(device->caps().adapterId == adapter->id, "Unexpected selected adapter");
    check(bool(GetModuleHandleW(L"VkLayer_khronos_validation.dll")) == validation, "Unexpected validation layer state");
    const Observer observer(mode == "observe");

    auto work = setup(*device);
    unsigned created{};
    if (!timing) {
        auto commands = take(device->createCommandList());
        Sample ignored{};
        record(*commands, work, 1, 0, false, false, false, ignored);
        observer.verify(++created, 0);
        commands.reset();
        observer.verify(created, created);
    }
    const auto batches = timing ? warmup + repetitions : checks;
    std::vector<Sample> samples;
    samples.reserve(repetitions);
    for (unsigned i = 0; i < batches; ++i) {
        auto commands = take(device->createCommandList());
        Sample sample{};
        const bool timed = timing && i >= warmup;
        const auto first = i % 2;
        record(*commands, work, count, first, alternating, !timing && count == capacity, timed, sample);
        const auto fence = take(device->queue().submit(*commands));
        observer.verify(created + count, created);
        const auto start = timed ? Clock::now() : Clock::time_point{};
        commands.reset();
        if (timed) sample[2] = elapsed(start);
        observer.verify(created + count, created);
        const auto waitStart = timed ? Clock::now() : Clock::time_point{};
        take(device->queue().wait(fence));
        if (timed) sample[3] = elapsed(waitStart);
        created += count;
        observer.verify(created, created);
        verify(work, count, first, alternating);
        if (timed) samples.push_back(sample);
    }
    work = {};
    take(device->queue().waitIdle());
    const auto final = take(device->resourceStatistics());
    check(!final.trackedObjects && !final.uniqueResources && !final.resourceAllocationBytes &&
          !final.pendingSubmissions && !final.pendingObjectReferences, "Final SDK ownership remains");
    if (validation) check(take(device->validationErrorCount()) == 0, "Vulkan validation errors");
    const auto native = observer.verify(created, created);
    device.reset();
    std::cout << "{\"sdk\":" << quote(gfx::buildFingerprint()) << ",\"adapter\":" << quote(adapter->name)
              << ",\"device_uuid\":" << quote(identity.uuid) << ",\"driver_uuid\":" << quote(identity.driverUuid)
              << ",\"driver\":" << identity.driver << ",\"count\":" << count
              << ",\"mode\":" << quote(mode) << ",\"pattern\":" << quote(pattern)
              << ",\"verified_images\":" << batches << ",\"verified_bytes\":" << batches * width * height * 4
              << ",\"capacity_rejections\":" << (!timing && count == capacity ? batches : 0)
              << ",\"discarded_recordings\":" << (timing ? 0 : 1)
              << ",\"final_sdk_ownership_zero\":true,\"validation_errors\":" << (validation ? "0" : "null")
              << ",\"samples\":[";
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto& s = samples[i];
        std::cout << (i ? ",[" : "[") << s[0] << ',' << s[1] << ',' << s[2] << ',' << s[3] << ']';
    }
    std::cout << ']';
    if (mode == "observe")
        std::cout << ",\"native\":{\"create\":" << native.create << ",\"allocate\":" << native.allocate
                  << ",\"update\":" << native.update << ",\"bind\":" << native.bind << ",\"destroy\":" << native.destroy
                  << ",\"live\":" << native.live << ",\"errors\":" << native.errors << '}';
    std::cout << "}\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
