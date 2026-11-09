#include "Adapter.hpp"
#include "Client.hpp"
#include "Identity.hpp"
#include <graphics/Graphics.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <iostream>
#include <memory>

#ifndef NDEBUG
#error Release required
#endif

namespace {
using Clock = std::chrono::steady_clock;
using bench::quote;
using Buffers = std::vector<gfx::BufferPtr>;
using gfx::Memory;
using gfx::ResourceState;

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
struct Recorder {
    traceclient::Client* observer;
    unsigned iteration{};
    template<class Work> std::int64_t run(unsigned phase, bool timed, Work work) const {
        if (observer) observer->set(phase, iteration);
        const auto start = timed ? Clock::now() : Clock::time_point{};
        work();
        const auto elapsed = timed ? std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        Clock::now()-start).count() : 0;
        if (observer) observer->set(0, iteration);
        return elapsed;
    }
};

constexpr std::array sizes{256u, 1024u, 4096u};
unsigned slotSize(unsigned slot) { return sizes[slot % sizes.size()]; }

void allocate(gfx::Device& device, Buffers& buffers, unsigned count, Memory memory) {
    for (unsigned i = 0; i < count; ++i) {
        buffers.push_back(take(device.createBuffer({slotSize(i), memory})));
    }
}

void copies(gfx::CommandList& commands, const Buffers& source, const Buffers& destination) {
    take(commands.begin());
    for (unsigned i = 0; i < source.size(); ++i) {
        take(commands.transition(source[i], ResourceState::CopySource));
        take(commands.transition(destination[i], ResourceState::CopyDestination));
        take(commands.copyBuffer(source[i], destination[i], slotSize(i)));
    }
    take(commands.end());
}

std::vector<std::vector<std::byte>> payload(unsigned count, unsigned round) {
    std::vector<std::vector<std::byte>> result;
    for (unsigned slot = 0; slot < count; ++slot) {
        auto& bytes = result.emplace_back(slotSize(slot));
        for (unsigned i = 0; i < bytes.size(); ++i) {
            bytes[i] = std::byte((i*37u+(i>>8)*17u+round*43u+slot*59u)&255u);
        }
    }
    return result;
}

using Sample = std::array<std::int64_t, 6>;
Sample batch(gfx::Device& device, const Recorder& recorder, bool reuse, bool timed, Buffers& upload,
             const Buffers& targets, const Buffers& readback, std::uint64_t& verifiedBytes) {
    const auto count = static_cast<unsigned>(targets.size());
    auto expected = payload(count, recorder.iteration);
    Sample sample{};
    if (!reuse) sample[0] = recorder.run(1, timed, [&] { allocate(device, upload, count, Memory::Upload); });
    sample[1] = recorder.run(2, timed, [&] {
        for (unsigned i = 0; i < count; ++i) take(upload[i]->write(0, expected[i]));
    });
    auto commands = take(device.createCommandList());
    sample[2] = recorder.run(2, timed, [&] { copies(*commands, upload, targets); });
    gfx::Fence fence{};
    sample[3] = recorder.run(2, timed, [&] { fence = take(device.queue().submit(*commands)); });
    sample[4] = recorder.run(2, timed, [&] { take(device.queue().wait(fence)); });
    commands.reset();
    if (!reuse) sample[5] = recorder.run(3, timed, [&] { upload.clear(); });
    recorder.run(2, false, [&] {
        auto read = take(device.createCommandList());
        copies(*read, targets, readback);
        take(device.queue().wait(take(device.queue().submit(*read))));
        for (unsigned i = 0; i < count; ++i) {
            check(take(readback[i]->read(0, slotSize(i))) == expected[i], "Upload payload differs");
            verifiedBytes += expected[i].size();
        }
    });
    return sample;
}
}

int main(int argc, char** argv) try {
    const auto adapters = take(gfx::enumerateAdapters(gfx::Backend::Vulkan));
    if (argc == 2 && std::string_view(argv[1]) == "--probe") {
        std::cout << '[';
        bool comma{};
        for (const auto& row : adapters) {
            if (row.softwareAdapter) continue;
            std::cout << (comma ? "," : "") << quote(row.name);
            comma = true;
        }
        std::cout << "]\n";
        return 0;
    }
    check(argc == 5, "Expected ADAPTER BATCH transient|reuse timing|observe|validation");
    unsigned count{};
    const std::string_view countText = argv[2], policy = argv[3], mode = argv[4];
    const auto end = countText.data()+countText.size();
    const auto parsed = std::from_chars(countText.data(), end, count);
    check(parsed.ec == std::errc{} && parsed.ptr == end && (count == 1 || count == 16 || count == 64),
          "Unsupported batch size");
    check(policy == "transient" || policy == "reuse", "Unknown staging policy");
    check(mode == "timing" || mode == "observe" || mode == "validation", "Unknown run mode");
    const bool reuse = policy == "reuse";
    const bool timing = mode == "timing";
    const bool validation = mode == "validation";
    const auto adapter = std::ranges::find_if(adapters, [&](const auto& row) { return row.name == argv[1]; });
    check(adapter != adapters.end() && !adapter->softwareAdapter &&
          std::ranges::count_if(adapters, [&](const auto& row) { return row.name == argv[1]; }) == 1,
          "Expected a unique hardware adapter");
    const auto identity = upload::identify(adapter->name);
    gfx::DeviceDesc selection;
    selection.backend = gfx::Backend::Vulkan;
    selection.adapter = adapter->id;
    selection.validation = validation;
    auto device = take(gfx::createDevice(selection));
    auto observer = mode == "observe" ? std::make_unique<traceclient::Client>() : nullptr;
    check(observer || !GetModuleHandleW(L"VkLayer_gfx_buffer_fault.dll"), "Unexpected observer");
    check(bool(GetModuleHandleW(L"VkLayer_khronos_validation.dll")) == validation, "Wrong validation mode");
    Recorder recorder{observer.get(), 99};
    Buffers upload, targets, readback;
    for (auto* buffers : {&upload, &targets, &readback}) buffers->reserve(count);
    recorder.run(2, false, [&] {
        allocate(*device, targets, count, Memory::Device);
        allocate(*device, readback, count, Memory::Readback);
    });
    if (reuse) recorder.run(1, false, [&] { allocate(*device, upload, count, Memory::Upload); });
    const unsigned warmup = timing ? 5 : 0;
    const unsigned repetitions = timing ? 30 : 5;
    std::uint64_t verifiedBytes{};
    std::vector<Sample> samples;
    samples.reserve(repetitions);
    for (unsigned i = 0; i < warmup+repetitions; ++i) {
        recorder.iteration = i;
        const auto sample = batch(*device, recorder, reuse, timing && i >= warmup, upload, targets, readback, verifiedBytes);
        if (timing && i >= warmup) samples.push_back(sample);
    }
    recorder.iteration = 99;
    if (reuse) recorder.run(3, false, [&] { upload.clear(); });
    recorder.run(2, false, [&] { targets.clear(); readback.clear(); });
    take(device->queue().waitIdle());
    const auto stats = take(device->resourceStatistics());
    check(!stats.trackedObjects && !stats.uniqueResources && !stats.resourceAllocationBytes &&
          !stats.pendingSubmissions && !stats.pendingObjectReferences, "Final SDK resources remain");
    if (validation) check(take(device->validationErrorCount()) == 0, "Vulkan validation errors");
    recorder.run(6, false, [&] { device.reset(); });
    std::cout << "{\"SDK\":" << quote(gfx::buildFingerprint()) << ",\"adapter\":" << quote(adapter->name)
        << ",\"device_uuid\":" << quote(identity.uuid) << ",\"driver_uuid\":" << quote(identity.driverUuid)
        << ",\"count\":" << count << ",\"policy\":" << quote(policy) << ",\"mode\":" << quote(mode)
        << ",\"verified_bytes\":" << verifiedBytes << ",\"samples\":[";
    for (std::size_t i = 0; i < samples.size(); ++i) {
        std::cout << (i ? ",[" : "[");
        for (std::size_t j = 0; j < samples[i].size(); ++j) std::cout << (j ? "," : "") << samples[i][j];
        std::cout << ']';
    }
    std::cout << ']';
    if (observer) {
        check(!observer->status(), "Observer fault or error");
        std::cout << ",\"trace\":" << observer->result();
    }
    std::cout << "}\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
