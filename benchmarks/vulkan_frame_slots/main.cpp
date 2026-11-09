#include "Adapter.hpp"
#include "Identity.hpp"
#include <graphics/Graphics.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <iostream>
#include <stdexcept>

#ifndef NDEBUG
#error Release required
#endif

namespace {
using Clock = std::chrono::steady_clock;
using gfx::ResourceState;
constexpr unsigned width = 257, height = 129, warmupFrames = 13, measuredFrames = 121;
constexpr unsigned rowBytes = width * 4;
using Row = std::array<std::byte, rowBytes>;

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

struct Reference {
    gfx::Color color;
    Row row;
};

Reference reference(unsigned frame) {
    const std::array pixel{frame, (frame * 71 + 3) % 256, 255 - frame, 255u};
    Reference result{{float(pixel[0]) / 255.f, float(pixel[1]) / 255.f, float(pixel[2]) / 255.f, 1.f}, {}};
    for (unsigned x = 0; x < width; ++x)
        for (unsigned c = 0; c < pixel.size(); ++c)
            result.row[x * 4 + c] = std::byte(pixel[c]);
    return result;
}

struct Slot {
    gfx::TexturePtr color;
    gfx::BufferPtr readback;
    std::unique_ptr<gfx::CommandList> commands;
    gfx::TextureFootprint footprint;
    gfx::Fence fence{};
    unsigned frame{};
};

Slot createSlot(gfx::Device& device) {
    auto color = take(device.createTexture({width, height, gfx::Format::RGBA8UNorm, true}));
    const auto footprint = color->footprint();
    check(footprint.rowBytes == rowBytes && footprint.height == height &&
          footprint.rowPitch >= rowBytes && footprint.size >= std::uint64_t(footprint.rowPitch) * height,
          "Unexpected texture readback footprint");
    auto readback = take(device.createBuffer({footprint.size, gfx::Memory::Readback}));
    return {std::move(color), std::move(readback), take(device.createCommandList()), footprint};
}

void record(const Slot& slot, gfx::Color color) {
    auto& commands = *slot.commands;
    take(commands.begin());
    take(commands.transition(slot.color, ResourceState::RenderTarget));
    take(commands.beginRenderPass(slot.color, color));
    take(commands.endRenderPass());
    take(commands.transition(slot.color, ResourceState::CopySource));
    take(commands.transition(slot.readback, ResourceState::CopyDestination));
    take(commands.copyTextureToBuffer(slot.color, slot.readback));
    take(commands.end());
}

void verify(const Slot& slot, const Row& expected) {
    const auto data = take(slot.readback->read(0, slot.footprint.size));
    check(data.size() == slot.footprint.size, "Incomplete mapped readback");
    for (unsigned y = 0; y < height; ++y)
        if (!std::equal(expected.begin(), expected.end(), data.data() + std::size_t(y) * slot.footprint.rowPitch))
            throw std::runtime_error("Pixel mismatch in frame " + std::to_string(slot.frame) +
                                     " row " + std::to_string(y));
}

using Sample = std::array<std::int64_t, 4>;
struct Run {
    std::vector<Sample> frames;
    std::int64_t wallNs{};
    unsigned peak{}, reuseWaits{}, drainWaits{};
};

Run stream(gfx::CommandQueue& queue, std::vector<Slot>& slots, std::span<const Reference> references) {
    const auto depth = static_cast<unsigned>(slots.size());
    const auto count = static_cast<unsigned>(references.size());
    Run result;
    result.frames.resize(count);
    unsigned outstanding{};
    gfx::Fence previous{};
    const auto finish = [&](Slot& slot) {
        check(slot.fence.value && outstanding, "Slot has no pending frame");
        auto& sample = result.frames[slot.frame];
        const auto start = Clock::now();
        take(queue.wait(slot.fence));
        const auto waited = Clock::now();
        verify(slot, references[slot.frame].row);
        sample[3] = elapsed(waited);
        sample[2] = std::chrono::duration_cast<std::chrono::nanoseconds>(waited - start).count();
        slot.fence = {};
        --outstanding;
    };
    const auto start = Clock::now();
    for (unsigned frame = 0; frame < count; ++frame) {
        auto& slot = slots[frame % depth];
        if (frame >= depth) {
            check(slot.frame == frame - depth, "Reusing a slot out of order");
            finish(slot);
            ++result.reuseWaits;
        }
        slot.frame = frame;
        const auto recordStart = Clock::now();
        record(slot, references[frame].color);
        const auto submitStart = Clock::now();
        slot.fence = take(queue.submit(*slot.commands));
        auto& sample = result.frames[frame];
        sample[1] = elapsed(submitStart);
        sample[0] = std::chrono::duration_cast<std::chrono::nanoseconds>(submitStart - recordStart).count();
        check(slot.fence.value > previous.value && (!previous.device || slot.fence.device == previous.device) &&
              slot.fence.device, "Submission fence identity or order differs");
        previous = slot.fence;
        result.peak = std::max(result.peak, ++outstanding);
    }
    for (unsigned frame = count - std::min(depth, count); frame < count; ++frame) {
        auto& slot = slots[frame % depth];
        check(slot.frame == frame, "Draining a slot out of order");
        finish(slot);
        ++result.drainWaits;
    }
    result.wallNs = elapsed(start);
    check(!outstanding && take(queue.completedValue()) >= previous.value, "Incomplete queue drain");
    return result;
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
    check(argc == 4, "Expected ADAPTER DEPTH timing|validation");
    unsigned depth{};
    const std::string_view depthText = argv[2], mode = argv[3];
    const auto parsed = std::from_chars(depthText.data(), depthText.data() + depthText.size(), depth);
    check(parsed.ec == std::errc{} && parsed.ptr == depthText.data() + depthText.size() &&
          (depth == 1 || depth == 2 || depth == 4), "Expected depth 1, 2 or 4");
    check(mode == "timing" || mode == "validation", "Unknown run mode");
    const bool validation = mode == "validation";
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
    check(bool(GetModuleHandleW(L"VkLayer_khronos_validation.dll")) == validation &&
          !GetModuleHandleW(L"VkLayer_gfx_buffer_fault.dll"), "Unexpected Vulkan layer state");

    std::vector<Slot> slots;
    slots.reserve(depth);
    for (unsigned i = 0; i < depth; ++i) slots.push_back(createSlot(*device));
    const auto setup = take(device->resourceStatistics());
    check(setup.uniqueResources == 2 * depth && setup.resourceAllocationBytes > 0, "Unexpected slot footprint");
    std::vector<Reference> references;
    for (unsigned frame = 0; frame < warmupFrames + measuredFrames; ++frame) references.push_back(reference(frame));
    const auto first = stream(device->queue(), slots, std::span{references}.first(warmupFrames));
    const auto second = validation ? Run{} : stream(device->queue(), slots, std::span{references}.subspan(warmupFrames));
    const auto& run = validation ? first : second;
    const auto verifiedFrames = first.frames.size() + second.frames.size();
    slots.clear();
    take(device->queue().waitIdle());
    const auto final = take(device->resourceStatistics());
    check(!final.trackedObjects && !final.uniqueResources && !final.resourceAllocationBytes &&
          !final.pendingSubmissions && !final.pendingObjectReferences, "Final SDK ownership remains");
    if (validation) check(take(device->validationErrorCount()) == 0, "Vulkan validation errors");
    device.reset();
    std::cout << "{\"SDK\":" << quote(gfx::buildFingerprint()) << ",\"adapter\":" << quote(adapter->name)
              << ",\"device_uuid\":" << quote(identity.uuid) << ",\"driver_uuid\":" << quote(identity.driverUuid)
              << ",\"vendor\":" << identity.vendor << ",\"device\":" << identity.device
              << ",\"driver\":" << identity.driver << ",\"api\":" << identity.api
              << ",\"mode\":" << quote(mode) << ",\"depth\":" << depth
              << ",\"verified_frames\":" << verifiedFrames << ",\"verified_bytes\":" << verifiedFrames * rowBytes * height
              << ",\"sdk_resource_bytes\":" << setup.resourceAllocationBytes
              << ",\"peak_outstanding\":" << run.peak << ",\"reuse_waits\":" << run.reuseWaits
              << ",\"drain_waits\":" << run.drainWaits << ",\"wall_ns\":" << (validation ? 0 : run.wallNs)
              << ",\"final_sdk_ownership_zero\":true,\"validation_errors\":" << (validation ? "0" : "null")
              << ",\"samples\":[";
    if (!validation) {
        for (std::size_t i = 0; i < run.frames.size(); ++i) {
            const auto& s = run.frames[i];
            std::cout << (i ? ",[" : "[") << s[0] << ',' << s[1] << ',' << s[2] << ',' << s[3] << ']';
        }
    }
    std::cout << "]}\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
