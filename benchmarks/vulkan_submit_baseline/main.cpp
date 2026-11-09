#include "Adapter.hpp"
#include "Identity.hpp"
#include "Observer.hpp"
#include <graphics/Graphics.hpp>
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
using submitBaseline::Counts;
constexpr unsigned copies = 16, bytes = 4096, warmup = 4, repetitions = 20, checks = 3;
using Payload = std::array<std::byte, bytes>;
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
        const auto module = GetModuleHandleW(L"VkLayer_gfx_submit_baseline.dll");
        check(bool(module) == enabled, "Unexpected submit observer state");
        if (!enabled) return;
        wchar_t actual[32768]{}, expected[32768]{};
        const auto actualLength = GetModuleFileNameW(module, actual, static_cast<DWORD>(std::size(actual)));
        const auto expectedLength = GetEnvironmentVariableW(L"GFX_EXPECT_SUBMIT_LAYER", expected,
                                                             static_cast<DWORD>(std::size(expected)));
        check(actualLength && actualLength < std::size(actual) && expectedLength && expectedLength < std::size(expected) &&
              std::filesystem::equivalent(actual, expected), "Unexpected submit layer module path");
        query_ = reinterpret_cast<submitBaseline::Query>(GetProcAddress(module, "gfxSubmitSnapshot"));
        check(query_ != nullptr, "Missing submit observer query");
    }
    Counts snapshot() const {
        Counts result;
        if (query_) check(query_(&result, sizeof(result)) && !result.errors && !result.idle, "Native observer error or idle");
        return result;
    }
    explicit operator bool() const { return query_ != nullptr; }
private:
    submitBaseline::Query query_{};
};

struct Slot { gfx::BufferPtr upload, readback; };
using Workload = std::array<Slot, copies>;
Workload setup(gfx::Device& device) {
    Workload slots;
    for (auto& slot : slots) {
        slot.upload = take(device.createBuffer({bytes, gfx::Memory::Upload}));
        slot.readback = take(device.createBuffer({bytes, gfx::Memory::Readback}));
    }
    return slots;
}
Payload reference(unsigned iteration, unsigned slot) {
    Payload data;
    for (unsigned i = 0; i < bytes; ++i)
        data[i] = std::byte((i * 37 + (i >> 8) * 19 + iteration * 43 + slot * 61) & 255);
    return data;
}

Sample batch(gfx::Device& device, const Workload& slots, const Observer& observer, unsigned grouping,
             unsigned iteration, bool timed, Counts& native) {
    for (unsigned i = 0; i < slots.size(); ++i) take(slots[i].upload->write(0, reference(iteration, i)));
    const auto count = copies / grouping;
    std::vector<std::unique_ptr<gfx::CommandList>> commands;
    commands.reserve(count);
    for (unsigned i = 0; i < count; ++i) commands.push_back(take(device.createCommandList()));

    Sample sample{};
    const auto start = timed ? Clock::now() : Clock::time_point{};
    for (unsigned list = 0; list < count; ++list) {
        auto& command = *commands[list];
        const auto beginStart = timed ? Clock::now() : Clock::time_point{};
        take(command.begin());
        if (timed) sample[0] += elapsed(beginStart);
        for (const auto& slot : std::span{slots}.subspan(list * grouping, grouping)) {
            take(command.transition(slot.upload, ResourceState::CopySource));
            take(command.transition(slot.readback, ResourceState::CopyDestination));
            take(command.copyBuffer(slot.upload, slot.readback, bytes));
        }
        take(command.end());
    }
    if (timed) sample[1] = elapsed(start);

    gfx::Fence last{};
    auto serial = std::uint64_t(iteration - 1) * count;
    const auto submitStart = timed ? Clock::now() : Clock::time_point{};
    for (const auto& command : commands) {
        const auto fence = take(device.queue().submit(*command));
        ++serial;
        check(fence.value == serial && fence.device && (!last.device || fence.device == last.device),
              "Submission fence sequence differs");
        last = fence;
    }
    if (timed) sample[2] = elapsed(submitStart);
    if (observer) {
        const auto submitted = observer.snapshot();
        check(submitted.livePools == count && submitted.pools == native.pools + count &&
              submitted.commandBuffers == native.commandBuffers + count && submitted.copies == native.copies + copies &&
              submitted.fences == native.fences + count && submitted.submissions == native.submissions + count &&
              submitted.waits == native.waits, "Native submission accounting differs");
    }

    const auto waitStart = timed ? Clock::now() : Clock::time_point{};
    take(device.queue().wait(last));
    if (timed) sample[3] = elapsed(waitStart);
    if (observer) {
        const auto waited = observer.snapshot();
        check(!waited.liveFences && waited.livePools == count && waited.destroyedFences == native.destroyedFences + count &&
              waited.ready == native.ready + count && waited.waits <= native.waits + 1, "Native fence drain differs");
    }
    const auto releaseStart = timed ? Clock::now() : Clock::time_point{};
    commands.clear();
    if (timed) {
        const auto stop = Clock::now();
        sample[4] = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - releaseStart).count();
        sample[5] = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
    }
    if (observer) {
        const auto released = observer.snapshot();
        check(!released.livePools && released.destroyedPools == native.destroyedPools + count, "Native command release differs");
        native = released;
    }
    check(take(device.queue().completedValue()) == last.value, "Last fence did not complete the batch");
    for (unsigned i = 0; i < slots.size(); ++i)
        check(std::ranges::equal(take(slots[i].readback->read(0, bytes)), reference(iteration, i)), "Full readback differs");
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
    check(argc == 4, "Expected ADAPTER COPIES_PER_COMMAND timing|observe|validation");
    const std::string_view groupingText = argv[2], mode = argv[3];
    check(groupingText == "1" || groupingText == "4" || groupingText == "16", "Expected grouping 1, 4 or 16");
    check(mode == "timing" || mode == "observe" || mode == "validation", "Unknown run mode");
    const unsigned grouping = groupingText == "1" ? 1 : groupingText == "4" ? 4 : 16;
    const bool timing = mode == "timing", validation = mode == "validation";
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

    auto slots = setup(*device);
    const auto batches = timing ? warmup + repetitions : checks;
    std::vector<Sample> samples;
    samples.reserve(repetitions);
    Counts native;
    for (unsigned i = 1; i <= batches; ++i) {
        const bool timed = timing && i > warmup;
        const auto sample = batch(*device, slots, observer, grouping, i, timed, native);
        if (timed) samples.push_back(sample);
    }
    slots = {};
    const auto stats = take(device->resourceStatistics());
    check(!stats.trackedObjects && !stats.uniqueResources && !stats.resourceAllocationBytes &&
          !stats.pendingSubmissions && !stats.pendingObjectReferences, "Final SDK ownership remains");
    if (validation) check(take(device->validationErrorCount()) == 0, "Vulkan validation errors");
    if (observer) native = observer.snapshot();
    device.reset();

    std::cout << "{\"sdk\":" << quote(gfx::buildFingerprint()) << ",\"adapter\":" << quote(adapter->name)
              << ",\"device_uuid\":" << quote(identity.uuid) << ",\"driver_uuid\":" << quote(identity.driverUuid)
              << ",\"driver\":" << identity.driver << ",\"grouping\":" << grouping << ",\"mode\":" << quote(mode)
              << ",\"verified_bytes\":" << batches * copies * bytes << ",\"submissions\":" << batches * copies / grouping
              << ",\"final_sdk_ownership_zero\":true,\"validation_errors\":" << (validation ? "0" : "null")
              << ",\"samples\":[";
    for (std::size_t i = 0; i < samples.size(); ++i) {
        std::cout << (i ? ",[" : "[");
        for (std::size_t j = 0; j < samples[i].size(); ++j) std::cout << (j ? "," : "") << samples[i][j];
        std::cout << ']';
    }
    std::cout << ']';
    if (observer) {
        std::cout << ",\"native\":{\"pools\":" << native.pools << ",\"destroyed_pools\":" << native.destroyedPools
                  << ",\"command_buffers\":" << native.commandBuffers << ",\"fences\":" << native.fences
                  << ",\"destroyed_fences\":" << native.destroyedFences << ",\"submissions\":" << native.submissions
                  << ",\"copies\":" << native.copies << ",\"ready\":" << native.ready << ",\"not_ready\":" << native.notReady
                  << ",\"waits\":" << native.waits << ",\"idle\":" << native.idle << ",\"live_pools\":" << native.livePools
                  << ",\"live_fences\":" << native.liveFences << ",\"errors\":" << native.errors << '}';
    }
    std::cout << "}\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
