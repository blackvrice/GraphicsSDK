#include "Client.hpp"
#include "Snapshot.hpp"
#include "Adapter.hpp"
#include "Identity.hpp"
#include <graphics/Graphics.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#ifndef NDEBUG
#error Release required
#endif

namespace {
using Clock = std::chrono::steady_clock;
using bench::quote;
constexpr std::array sizes{1u,63u,64u,65u,127u,128u,129u,255u,256u,257u,511u,512u,513u,
                           1023u,1024u,1025u,2047u,2048u,2049u,4095u,4096u,4097u,8192u,65537u};
constexpr std::array sparseSizes{64u, 256u, 1024u, 4096u};
constexpr std::array survivorIds{0u, 1u, 3u, 7u, 15u};

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

struct Item { unsigned id{}, size{}, seed{}; gfx::BufferPtr buffer; };
using Items = std::vector<Item>;
struct Native {
    traceclient::Client client;
    using Snapshot = int(__cdecl*)(churn::Snapshot*, unsigned);
    Snapshot snapshot = reinterpret_cast<Snapshot>(GetProcAddress(client.module, "gfxChurnSnapshot"));
    unsigned frames{};
    std::unique_ptr<churn::Snapshot> read() const {
        auto result = std::make_unique<churn::Snapshot>();
        check(snapshot && snapshot(result.get(), sizeof(*result)), "Incomplete native binding snapshot");
        return result;
    }
};
struct Sample {
    std::vector<std::int64_t> create, release, wait;
    std::uint64_t digest{14695981039346656037ull}, reads{}, bytes{};
    void note(std::uint64_t value) { digest = (digest ^ value) * 1099511628211ull; }
};
struct Trial {
    gfx::Device& device;
    Native* native;
    Sample sample;
    std::vector<std::string> checkpoints;
    unsigned round{}, nextId{};

    template<class Work> void interval(unsigned phase, Work work, std::vector<std::int64_t>* times = nullptr) {
        if (native) {
            native->client.set(phase, round);
            ++native->frames;
            work();
            native->client.set(0, round);
        } else if (times) {
            const auto begin = Clock::now();
            work();
            const auto elapsed = Clock::now() - begin;
            times->push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
        } else {
            work();
        }
    }
    template<class Input> std::span<Item> create(Items& items, unsigned count, Input input) {
        const auto begin = items.size();
        items.reserve(begin + count);
        sample.note(1);
        sample.note(count);
        for (unsigned i = 0; i < count; ++i) {
            const auto [size, seed] = input(i);
            items.push_back({nextId++, size, seed, {}});
            sample.note(items.back().id);
            sample.note(size);
            sample.note(seed);
        }
        auto batch = std::span(items).subspan(begin);
        interval(1, [&] {
            for (auto& item : batch) item.buffer = take(device.createBuffer({item.size}));
        }, &sample.create);
        return batch;
    }
    template<class Keep> void release(Items& items, Keep keep) {
        std::vector<gfx::BufferPtr*> dropping;
        dropping.reserve(items.size());
        sample.note(3);
        for (unsigned i = 0; i < items.size(); ++i) {
            if (!keep(i)) {
                dropping.push_back(&items[i].buffer);
                sample.note(items[i].id);
            }
        }
        sample.note(dropping.size());
        interval(3, [&] {
            for (auto* buffer : dropping) buffer->reset();
        }, &sample.release);
        std::erase_if(items, [](const Item& item) { return !item.buffer; });
    }
    void clear(Items& items) { release(items, [](unsigned) { return false; }); }

    void checkpoint(const char* label, const Items& live, const Items& held = {},
                    std::uint64_t helperBytes = 0, bool pending = false) {
        const auto count = live.size() + held.size();
        std::uint64_t payload{};
        sample.note(6);
        sample.note(count);
        for (const auto* items : {&live, &held}) {
            for (const auto& item : *items) {
                sample.note(item.id);
                payload += item.size;
            }
        }
        if (!native) return;
        const auto snap = native->read();
        check(snap->buffers == count + held.size() && snap->images == 0 &&
              snap->memories >= held.size() && snap->bytes >= helperBytes, "Native checkpoint ownership differs");
        std::uint64_t required{};
        for (unsigned i = 0; i < snap->count; ++i) required += snap->entries[i].required;
        check(required >= helperBytes && required - helperBytes >= payload && snap->bytes >= required,
              "Native checkpoint byte accounting differs");
        if (!pending) {
            const auto stats = take(device.resourceStatistics());
            check(stats.uniqueResources == snap->buffers && stats.resourceAllocationBytes == snap->bytes &&
                  !stats.pendingSubmissions, "Public/native checkpoint differs");
        }
        checkpoints.push_back("{\"label\":"+quote(label)+",\"round\":"+std::to_string(round)+
            ",\"frame_count\":"+std::to_string(native->frames)+",\"buffers\":"+std::to_string(count)+
            ",\"payload\":"+std::to_string(payload)+",\"required\":"+std::to_string(required-helperBytes)+
            ",\"reserved\":"+std::to_string(snap->bytes-helperBytes)+
            ",\"allocations\":"+std::to_string(snap->memories-held.size())+
            ",\"helper_bytes\":"+std::to_string(helperBytes)+"}");
    }
};

std::vector<std::byte> payload(const Item& item) {
    std::vector<std::byte> result(item.size);
    for (unsigned i = 0; i < item.size; ++i) {
        result[i] = std::byte((i*37u+item.seed*53u+(i>>8)*17u)&255u);
    }
    return result;
}

void verify(Trial& trial, std::span<const Item> items, bool initialize) {
    trial.interval(2, [&] {
        auto commands = take(trial.device.createCommandList());
        std::vector<gfx::BufferPtr> upload, readback;
        upload.reserve(items.size());
        readback.reserve(items.size());
        take(commands->begin());
        for (const auto& item : items) {
            if (initialize) {
                auto buffer = take(trial.device.createBuffer({item.size, gfx::Memory::Upload}));
                take(buffer->write(0, payload(item)));
                take(commands->transition(item.buffer, gfx::ResourceState::CopyDestination));
                take(commands->copyBuffer(buffer, item.buffer, item.size));
                upload.push_back(std::move(buffer));
            }
            auto target = take(trial.device.createBuffer({item.size, gfx::Memory::Readback}));
            take(commands->transition(item.buffer, gfx::ResourceState::CopySource));
            take(commands->copyBuffer(item.buffer, target, item.size));
            readback.push_back(std::move(target));
        }
        take(commands->end());
        take(trial.device.queue().wait(take(trial.device.queue().submit(*commands))));
        for (std::size_t i = 0; i < items.size(); ++i) {
            check(take(readback[i]->read(0, items[i].size)) == payload(items[i]), "Complete buffer readback differs");
            ++trial.sample.reads;
            trial.sample.bytes += items[i].size;
        }
    });
}

void mixed(Trial& trial) {
    Items live;
    for (unsigned round = 0; round < 16; ++round) {
        trial.round = round;
        const auto first = static_cast<unsigned>(live.size());
        const auto fresh = trial.create(live, 96-first, [&](unsigned i) {
            return std::pair{sizes[(first+i+round*7)%sizes.size()], 1000+round*101+first+i};
        });
        verify(trial, fresh, true);
        trial.checkpoint("full", live);
        trial.release(live, [&](unsigned i) { return (i+round)%(2+round%3) == 0; });
        trial.checkpoint("survivors", live);
        verify(trial, live, false);
        if (round%4 == 3) trial.clear(live);
        trial.checkpoint("released", live);
    }
}

void sparse(Trial& trial) {
    Items live;
    for (unsigned round = 0; round < 4; ++round) {
        trial.round = round;
        verify(trial, trial.create(live, 124, [&](unsigned i) {
            return std::pair{sparseSizes[i/31], round*1000+i+1};
        }), true);
        trial.checkpoint("full", live);
        trial.release(live, [](unsigned i) { return std::ranges::find(survivorIds, i%31) != survivorIds.end(); });
        trial.checkpoint("fixed-survivors", live);
        verify(trial, live, false);
        verify(trial, trial.create(live, 32, [&](unsigned i) {
            return std::pair{sparseSizes[i%4], round*1000+500+i};
        }), true);
        verify(trial, std::span(live).first(20), false);
        trial.checkpoint("holes-refilled", live);
        trial.clear(live);
        trial.checkpoint("empty", live);
        trial.create(live, 1, [&](unsigned) { return std::pair{4096u, round+3}; });
        trial.checkpoint("reset-singleton", live);
        trial.clear(live);
        trial.checkpoint("reset-empty", live);
    }
}

void held(Trial& trial) {
    for (unsigned round = 0; round < 8; ++round) {
        trial.round = round;
        Items live, metadata;
        verify(trial, trial.create(live, 24, [&](unsigned i) {
            return std::pair{sizes[i], round*101+i+1};
        }), true);
        std::unique_ptr<gfx::CommandList> commands;
        std::vector<gfx::BufferPtr> readback;
        const auto bytesBefore = trial.native ? trial.native->read()->bytes : 0;
        trial.interval(2, [&] {
            commands = take(trial.device.createCommandList());
            take(commands->begin());
            for (const auto& item : live) {
                auto target = take(trial.device.createBuffer({item.size, gfx::Memory::Readback}));
                take(commands->copyBuffer(item.buffer, target, item.size));
                readback.push_back(std::move(target));
                metadata.push_back({item.id, item.size, item.seed, {}});
            }
            take(commands->end());
        });
        const auto helperBytes = trial.native ? trial.native->read()->bytes - bytesBefore : 0;
        trial.clear(live);
        trial.checkpoint("ended-retained", live, metadata, helperBytes);
        verify(trial, trial.create(live, 72, [&](unsigned i) {
            return std::pair{sizes[(i+round)%sizes.size()], round*211+100+i};
        }), true);
        trial.checkpoint("replacements", live, metadata, helperBytes);
        const auto fence = take(trial.device.queue().submit(*commands));
        commands.reset();
        trial.checkpoint("pending-retained", live, metadata, helperBytes, true);
        trial.interval(4, [&] { take(trial.device.queue().wait(fence)); }, &trial.sample.wait);
        trial.interval(2, [&] {
            for (std::size_t i = 0; i < readback.size(); ++i) {
                check(take(readback[i]->read(0, metadata[i].size)) == payload(metadata[i]), "Retained buffer changed");
                ++trial.sample.reads;
                trial.sample.bytes += metadata[i].size;
            }
            readback.clear();
        });
        verify(trial, live, false);
        trial.checkpoint("completed", live);
        trial.clear(live);
        trial.checkpoint("empty", live);
    }
}

template<class T> void emit(const std::vector<T>& values) {
    std::cout << '[';
    for (std::size_t i = 0; i < values.size(); ++i) std::cout << (i ? "," : "") << values[i];
    std::cout << ']';
}
void emit(const Sample& sample) {
    std::cout << "{\"digest\":\"" << sample.digest << "\",\"reads\":" << sample.reads <<
        ",\"bytes\":" << sample.bytes << ",\"create\":";
    emit(sample.create);
    std::cout << ",\"release\":";
    emit(sample.release);
    std::cout << ",\"wait\":";
    emit(sample.wait);
    std::cout << '}';
}
}

int main(int argc, char** argv) try {
    const auto adapters = take(gfx::enumerateAdapters(gfx::Backend::Vulkan));
    if (argc == 2 && std::string_view(argv[1]) == "--probe") {
        std::vector<std::string> names;
        for (const auto& adapter : adapters) if (!adapter.softwareAdapter) names.push_back(quote(adapter.name));
        emit(names);
        return 0;
    }
    check(argc == 4, "Expected PROFILE EXACT_ADAPTER timing|observe");
    const std::string_view profile = argv[1], mode = argv[3];
    check(profile == "mixed" || profile == "sparse" || profile == "held", "Unknown profile");
    check(mode == "timing" || mode == "observe", "Unknown run mode");
    const auto adapter = std::ranges::find_if(adapters, [&](const auto& row) { return row.name == argv[2]; });
    check(adapter != adapters.end() && !adapter->softwareAdapter &&
          std::ranges::count_if(adapters, [&](const auto& row) { return row.name == argv[2]; }) == 1,
          "Expected a unique hardware adapter");
    const auto identity = upload::identify(adapter->name);
    gfx::DeviceDesc selection;
    selection.backend = gfx::Backend::Vulkan;
    selection.adapter = adapter->id;
    auto device = take(gfx::createDevice(selection));
    check(!GetModuleHandleW(L"VkLayer_khronos_validation.dll"), "Unexpected Vulkan validation layer");
    auto native = mode == "observe" ? std::make_unique<Native>() : nullptr;
    check(native || !GetModuleHandleW(L"VkLayer_gfx_buffer_fault.dll"), "Timing process loaded the observer");
    const auto work = profile == "mixed" ? mixed : profile == "sparse" ? sparse : held;
    std::vector<Sample> samples;
    std::vector<std::string> checkpoints;
    const unsigned sweeps = native ? 1 : 11;
    for (unsigned i = 0; i < sweeps; ++i) {
        Trial trial{*device, native.get()};
        work(trial);
        take(device->queue().waitIdle());
        const auto stats = take(device->resourceStatistics());
        check(!stats.trackedObjects && !stats.uniqueResources && !stats.resourceAllocationBytes &&
              !stats.pendingSubmissions && !stats.pendingObjectReferences,
              "Final native resources or submissions remain");
        if (native || i) samples.push_back(std::move(trial.sample));
        checkpoints = std::move(trial.checkpoints);
    }
    if (native) native->client.set(6, 99);
    device.reset();
    if (native) native->client.set(0, 99);
    std::cout << "{\"SDK\":" << quote(gfx::buildFingerprint()) << ",\"profile\":" << quote(profile) <<
        ",\"mode\":" << quote(mode) << ",\"adapter\":" << quote(adapter->name) <<
        ",\"device_uuid\":" << quote(identity.uuid) << ",\"driver_uuid\":" << quote(identity.driverUuid) <<
        ",\"samples\":[";
    for (std::size_t i = 0; i < samples.size(); ++i) {
        if (i) std::cout << ',';
        emit(samples[i]);
    }
    std::cout << "],\"checkpoints\":";
    emit(checkpoints);
    if (native) {
        check(!native->client.status(), "Unexpected injected fault or observer error");
        std::cout << ",\"trace\":" << native->client.result();
    }
    std::cout << "}\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
