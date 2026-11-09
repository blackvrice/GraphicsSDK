#include "Adapter.hpp"
#include "Client.hpp"
#include "Identity.hpp"
#include <graphics/Graphics.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <memory>

#ifndef NDEBUG
#error Release required
#endif

namespace {
using Clock = std::chrono::steady_clock;
using gfx::Memory;
using gfx::ResourceState;
using Buffers = std::vector<gfx::BufferPtr>;
using Sample = std::array<std::int64_t, 4>;
constexpr unsigned bytes = 4096, warmup = 5, repetitions = 24, checks = 5;
enum Phase : unsigned { createTransient = 1, dropBefore, waitFence, dropAfter, persistent, retire };

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

struct Recorder {
    traceclient::Client* observer;
    unsigned iteration{};

    template<class Work> std::int64_t run(Phase phase, bool timed, Work&& work) const {
        if (observer) observer->set(phase, iteration);
        const auto start = timed ? Clock::now() : Clock::time_point{};
        work();
        const auto result = timed ? elapsed(start) : 0;
        if (observer) observer->set(0, iteration);
        return result;
    }
};

using Payload = std::array<std::byte, bytes>;
Payload reference(unsigned iteration, unsigned slot) {
    Payload result;
    for (unsigned i = 0; i < bytes; ++i)
        result[i] = std::byte((i * 37 + (i >> 8) * 17 + iteration * 43 + slot * 59) & 255);
    return result;
}

struct Batch {
    Buffers upload, target;
    std::unique_ptr<gfx::CommandList> commands;

    void dropResources() {
        upload.clear();
        target.clear();
    }
    void drop() {
        dropResources();
        commands.reset();
    }
};

Batch prepare(gfx::Device& device, const Recorder& recorder, const Buffers& readback) {
    Batch batch;
    batch.upload.reserve(readback.size());
    batch.target.reserve(readback.size());
    recorder.run(createTransient, false, [&] {
        for (unsigned i = 0; i < readback.size(); ++i) {
            batch.upload.push_back(take(device.createBuffer({bytes, Memory::Upload})));
            batch.target.push_back(take(device.createBuffer({bytes, Memory::Device})));
        }
    });
    for (unsigned i = 0; i < readback.size(); ++i)
        take(batch.upload[i]->write(0, reference(recorder.iteration, i)));
    batch.commands = take(device.createCommandList());
    auto& commands = *batch.commands;
    take(commands.begin());
    for (unsigned i = 0; i < readback.size(); ++i) {
        take(commands.transition(batch.upload[i], ResourceState::CopySource));
        take(commands.transition(batch.target[i], ResourceState::CopyDestination));
        take(commands.copyBuffer(batch.upload[i], batch.target[i], bytes));
        take(commands.transition(batch.target[i], ResourceState::CopySource));
        take(commands.transition(readback[i], ResourceState::CopyDestination));
        take(commands.copyBuffer(batch.target[i], readback[i], bytes));
    }
    take(commands.end());
    return batch;
}

void discarded(gfx::Device& device, const Recorder& recorder, const Buffers& readback) {
    auto batch = prepare(device, recorder, readback);
    recorder.run(dropBefore, false, [&] { batch.dropResources(); });
    const auto held = take(device.resourceStatistics());
    check(held.uniqueResources == 3 * readback.size() && !held.pendingSubmissions,
          "Unsubmitted recording lost its buffer references");
    recorder.run(dropAfter, false, [&] { batch.drop(); });
    const auto released = take(device.resourceStatistics());
    check(released.uniqueResources == readback.size() && !released.pendingSubmissions,
          "Discarded recording retained transient resources");
}

Sample submit(gfx::Device& device, const Recorder& recorder, const Buffers& readback, bool early, bool timed) {
    auto batch = prepare(device, recorder, readback);
    const auto fence = take(device.queue().submit(*batch.commands));
    const auto start = timed ? Clock::now() : Clock::time_point{};
    Sample sample{};
    sample[0] = recorder.run(dropBefore, timed && early, [&] { if (early) batch.drop(); });
    sample[1] = recorder.run(waitFence, timed, [&] { take(device.queue().wait(fence)); });
    sample[2] = recorder.run(dropAfter, timed && !early, [&] { if (!early) batch.drop(); });
    if (timed) sample[3] = elapsed(start);
    for (unsigned i = 0; i < readback.size(); ++i) {
        const auto actual = take(readback[i]->read(0, bytes));
        check(std::ranges::equal(actual, reference(recorder.iteration, i)), "Full readback differs");
    }
    return sample;
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
    check(argc == 5, "Expected ADAPTER COUNT early|late timing|observe|validation");
    const std::string_view countText = argv[2], policy = argv[3], mode = argv[4];
    check(countText == "1" || countText == "32", "Expected batch size 1 or 32");
    check(policy == "early" || policy == "late", "Unknown release policy");
    check(mode == "timing" || mode == "observe" || mode == "validation", "Unknown run mode");
    const unsigned count = countText == "1" ? 1 : 32;
    const bool timing = mode == "timing", validation = mode == "validation", early = policy == "early";
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
    auto observer = mode == "observe" ? std::make_unique<traceclient::Client>() : nullptr;
    check(bool(GetModuleHandleW(L"VkLayer_gfx_buffer_fault.dll")) == bool(observer) &&
          bool(GetModuleHandleW(L"VkLayer_khronos_validation.dll")) == validation, "Unexpected Vulkan layer state");
    Recorder recorder{observer.get()};
    Buffers readback;
    readback.reserve(count);
    recorder.run(persistent, false, [&] {
        for (unsigned i = 0; i < count; ++i)
            readback.push_back(take(device->createBuffer({bytes, Memory::Readback})));
    });
    if (!timing) discarded(*device, recorder, readback);
    const auto batches = timing ? warmup + repetitions : checks;
    std::vector<Sample> samples;
    samples.reserve(repetitions);
    for (unsigned i = 1; i <= batches; ++i) {
        recorder.iteration = i;
        const auto sample = submit(*device, recorder, readback, early, timing && i > warmup);
        if (timing && i > warmup) samples.push_back(sample);
    }
    recorder.iteration = 0;
    recorder.run(persistent, false, [&] { readback.clear(); });
    take(device->queue().waitIdle());
    const auto final = take(device->resourceStatistics());
    check(!final.trackedObjects && !final.uniqueResources && !final.resourceAllocationBytes &&
          !final.pendingSubmissions && !final.pendingObjectReferences, "Final SDK ownership remains");
    if (validation) check(take(device->validationErrorCount()) == 0, "Vulkan validation errors");
    recorder.run(retire, false, [&] { device.reset(); });
    std::cout << "{\"SDK\":" << quote(gfx::buildFingerprint()) << ",\"adapter\":" << quote(adapter->name)
              << ",\"device_uuid\":" << quote(identity.uuid) << ",\"driver_uuid\":" << quote(identity.driverUuid)
              << ",\"driver\":" << identity.driver << ",\"count\":" << count
              << ",\"mode\":" << quote(mode) << ",\"policy\":" << quote(policy)
              << ",\"verified_bytes\":" << std::uint64_t(batches) * count * bytes
              << ",\"discarded_recordings\":" << (timing ? 0 : 1)
              << ",\"final_sdk_ownership_zero\":true,\"validation_errors\":" << (validation ? "0" : "null")
              << ",\"samples\":[";
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto& s = samples[i];
        std::cout << (i ? ",[" : "[") << s[0] << ',' << s[1] << ',' << s[2] << ',' << s[3] << ']';
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
