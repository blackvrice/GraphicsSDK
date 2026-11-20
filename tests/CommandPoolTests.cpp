#include <graphics/Graphics.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
struct Totals {
    std::uint64_t assertions{};
    std::uint64_t epochs{};
    std::uint64_t submissions{};
    std::uint64_t rejections{};
    std::uint64_t inFlightResetRejections{};
};

void check(bool condition, const char* message, Totals& totals) {
    if (!condition) throw std::runtime_error(message);
    ++totals.assertions;
}

template <class T>
T take(gfx::Result<T>&& value, const char* message, Totals& totals) {
    if (!value) {
        throw std::runtime_error(std::string(message) + ": " + value.error().message);
    }
    ++totals.assertions;
    return std::move(*value);
}

void take(gfx::Result<void>&& value, const char* message, Totals& totals) {
    if (!value) {
        throw std::runtime_error(std::string(message) + ": " + value.error().message);
    }
    ++totals.assertions;
}

template <class T>
void reject(const gfx::Result<T>& value, gfx::ErrorCode expected, const char* message, Totals& totals) {
    if (value) throw std::runtime_error(std::string("Unexpected success: ") + message);
    if (value.error().code != expected) {
        throw std::runtime_error(std::string(message) + " (got: " + value.error().message + ")");
    }
    ++totals.assertions;
    ++totals.rejections;
}

std::vector<std::byte> makePattern(std::size_t size, std::uint32_t seed) {
    std::vector<std::byte> bytes(size);
    for (std::size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<std::byte>((seed * 37u + i * 17u + 11u) & 0xFFu);
    }
    return bytes;
}

void verifyReclaimed(gfx::Device& device, Totals& totals) {
    take(device.queue().waitIdle(), "waitIdle before reclamation check", totals);
    const auto stats = take(device.resourceStatistics(), "resourceStatistics after cleanup", totals);
    check(stats.trackedObjects == 0, "All tracked objects must be reclaimed", totals);
    check(stats.uniqueResources == 0, "All unique GPU resources must be reclaimed", totals);
    check(stats.resourceAllocationBytes == 0, "All resource allocation bytes must be reclaimed", totals);
    check(stats.pendingSubmissions == 0, "No pending submissions may remain", totals);
    check(stats.pendingObjectReferences == 0, "No pending object references may remain", totals);
}

void runCommandPoolSuite(gfx::Device& device, bool validation, Totals& totals) {
    using namespace std::chrono_literals;
    const bool hasTimestamps = device.caps().timestampQueries;

    {
        // 1. 32-epoch reuse with two pooled command lists, color pass + buffer region + timestamps.
        auto pool = take(device.createCommandPool(), "createCommandPool", totals);
        auto listA = take(pool->createCommandList(), "pool->createCommandList A", totals);
        auto listB = take(pool->createCommandList(), "pool->createCommandList B", totals);

        constexpr std::uint64_t kBufSize = 4096;
        constexpr std::uint32_t kTexSize = 16;
        auto upload = take(device.createBuffer({kBufSize, gfx::Memory::Upload}), "create upload", totals);
        auto deviceBuf = take(device.createBuffer({kBufSize, gfx::Memory::Device}), "create deviceBuf", totals);
        auto readback = take(device.createBuffer({kBufSize, gfx::Memory::Readback}), "create readback", totals);
        auto colorTarget = take(device.createTexture({kTexSize, kTexSize, gfx::Format::RGBA8UNorm, true}), "create colorTarget", totals);
        const auto fp = colorTarget->footprint();
        auto texReadback = take(device.createBuffer({fp.size, gfx::Memory::Readback}), "create texReadback", totals);

        for (std::uint32_t epoch = 0; epoch < 32; ++epoch) {
            const auto pattern = makePattern(kBufSize, epoch + 1u);
            take(upload->write(0, pattern), "upload write", totals);

            const std::uint8_t cr = static_cast<std::uint8_t>((epoch * 7u + 13u) & 0xFFu);
            const std::uint8_t cg = static_cast<std::uint8_t>((epoch * 11u + 41u) & 0xFFu);
            const std::uint8_t cb = static_cast<std::uint8_t>((epoch * 19u + 73u) & 0xFFu);
            const gfx::Color clear{cr / 255.0f, cg / 255.0f, cb / 255.0f, 1.0f};

            const std::uint64_t splitOffset = (epoch % 4u) * 512u;
            const std::uint64_t firstBytes = splitOffset ? splitOffset : 1024u;
            const std::uint64_t secondBytes = kBufSize - firstBytes;

            take(listA->begin(), "listA begin", totals);
            if (hasTimestamps) take(listA->writeTimestamp(0), "writeTimestamp 0", totals);
            take(listA->transition(deviceBuf, gfx::ResourceState::CopyDestination), "deviceBuf -> CopyDst", totals);
            take(listA->copyBufferRegion(upload, 0, deviceBuf, 0, firstBytes), "copy region 1", totals);
            take(listA->copyBufferRegion(upload, firstBytes, deviceBuf, firstBytes, secondBytes), "copy region 2", totals);
            take(listA->transition(deviceBuf, gfx::ResourceState::CopySource), "deviceBuf -> CopySrc", totals);
            take(listA->transition(colorTarget, gfx::ResourceState::RenderTarget), "colorTarget -> RT", totals);
            take(listA->beginRenderPass(colorTarget, clear), "beginRenderPass", totals);
            take(listA->endRenderPass(), "endRenderPass", totals);
            if (hasTimestamps) {
                take(listA->writeTimestamp(1), "writeTimestamp 1", totals);
                take(listA->resolveTimestamps(), "resolveTimestamps", totals);
            }
            take(listA->end(), "listA end", totals);

            // Submit listA BEFORE recording listB so listB observes the post-listA resource versions.
            const auto fenceA = take(device.queue().submit(*listA), "submit listA", totals);
            ++totals.submissions;

            take(listB->begin(), "listB begin", totals);
            take(listB->copyBufferRegion(deviceBuf, 0, readback, 0, kBufSize), "deviceBuf -> readback", totals);
            take(listB->transition(colorTarget, gfx::ResourceState::CopySource), "colorTarget -> CopySrc", totals);
            take(listB->copyTextureToBuffer(colorTarget, texReadback), "copyTextureToBuffer", totals);
            take(listB->transition(colorTarget, gfx::ResourceState::RenderTarget), "colorTarget -> RT", totals);
            take(listB->end(), "listB end", totals);

            const auto fenceB = take(device.queue().submit(*listB), "submit listB", totals);
            ++totals.submissions;
            check(fenceB.value > fenceA.value, "Fence values must increase monotonically", totals);

            take(device.queue().wait(fenceB), "wait fenceB", totals);

            if (hasTimestamps) {
                const double ms = take(listA->elapsedGpuMilliseconds(), "elapsedGpuMilliseconds before reset", totals);
                check(std::isfinite(ms) && ms >= 0.0, "Elapsed GPU time must be finite and non-negative", totals);
            }

            const auto actualBytes = take(readback->read(0, kBufSize), "readback read", totals);
            check(actualBytes == pattern, "Pooled buffer copy bytes must match exactly", totals);

            const auto actualPixels = take(texReadback->read(0, fp.size), "texReadback read", totals);
            for (std::uint32_t y = 0; y < kTexSize; ++y) {
                for (std::uint32_t x = 0; x < kTexSize; ++x) {
                    const std::size_t off = static_cast<std::size_t>(y) * fp.rowPitch + static_cast<std::size_t>(x) * 4u;
                    const bool pixelOk =
                        static_cast<std::uint8_t>(actualPixels[off + 0]) == cr &&
                        static_cast<std::uint8_t>(actualPixels[off + 1]) == cg &&
                        static_cast<std::uint8_t>(actualPixels[off + 2]) == cb &&
                        static_cast<std::uint8_t>(actualPixels[off + 3]) == 255u;
                    if (!pixelOk) throw std::runtime_error("Pooled render target pixel mismatch");
                }
            }
            ++totals.assertions;

            // Re-recording before CommandPool::reset() must fail cleanly.
            reject(listA->begin(), gfx::ErrorCode::InvalidState, "listA begin before pool reset", totals);
            reject(listB->begin(), gfx::ErrorCode::InvalidState, "listB begin before pool reset", totals);

            take(pool->reset(), "pool reset", totals);
            ++totals.epochs;

            if (hasTimestamps) {
                reject(listA->elapsedGpuMilliseconds(), gfx::ErrorCode::InvalidState,
                    "elapsedGpuMilliseconds must fail after pool reset", totals);
            }
        }

        // 2. State machine rejections while listA is recording must preserve active recording.
        const auto checkPattern = makePattern(kBufSize, 999u);
        take(upload->write(0, checkPattern), "write checkPattern", totals);
        take(listA->begin(), "listA begin for state checks", totals);
        reject(pool->reset(), gfx::ErrorCode::InvalidState, "pool->reset while recording", totals);
        reject(pool->createCommandList(), gfx::ErrorCode::InvalidState, "pool->createCommandList while recording", totals);
        reject(listB->begin(), gfx::ErrorCode::InvalidState, "listB->begin while listA recording", totals);
        reject(listA->begin(), gfx::ErrorCode::InvalidState, "listA double begin", totals);

        // Verify listA still records and executes cleanly after rejected calls.
        take(listA->transition(deviceBuf, gfx::ResourceState::CopyDestination), "listA recovery CopyDst", totals);
        take(listA->copyBuffer(upload, deviceBuf, kBufSize), "listA recovery copy 1", totals);
        take(listA->transition(deviceBuf, gfx::ResourceState::CopySource), "listA recovery CopySrc", totals);
        take(listA->copyBuffer(deviceBuf, readback, kBufSize), "listA recovery copy 2", totals);
        take(listA->end(), "listA recovery end", totals);
        const auto recoveryFence = take(device.queue().submit(*listA), "listA recovery submit", totals);
        ++totals.submissions;
        take(device.queue().wait(recoveryFence), "listA recovery wait", totals);
        check(take(readback->read(0, kBufSize), "listA recovery read", totals) == checkPattern,
            "Recovery copy after rejected pool operations must match", totals);
        take(pool->reset(), "pool reset after state checks", totals);

        // 3. Unsubmitted recording discarded by pool->reset() cannot be submitted.
        take(listA->begin(), "listA unsubmitted begin", totals);
        take(listA->transition(deviceBuf, gfx::ResourceState::CopyDestination), "listA unsubmitted transition", totals);
        take(listA->end(), "listA unsubmitted end", totals);
        take(pool->reset(), "pool reset discards unsubmitted recording", totals);
        reject(device.queue().submit(*listA), gfx::ErrorCode::InvalidState,
            "Submitting discarded pooled recording after reset must fail", totals);

        // 4. Mid-recording destruction of a pooled CommandList frees pool's open state cleanly.
        {
            auto listC = take(pool->createCommandList(), "create listC", totals);
            take(listC->begin(), "listC begin before abrupt drop", totals);
            // Intentionally drop listC without calling end().
        }
        take(pool->reset(), "pool reset after dropping open listC", totals);
        take(listA->begin(), "listA begin after listC drop and pool reset", totals);
        take(listA->end(), "listA end after listC drop", totals);
        const auto afterDropFence = take(device.queue().submit(*listA), "submit after listC drop", totals);
        ++totals.submissions;
        take(device.queue().wait(afterDropFence), "wait after listC drop", totals);
        take(pool->reset(), "pool reset after listC drop verify", totals);

        // 5. Multi-pool independence on the same device.
        {
            auto pool2 = take(device.createCommandPool(), "create independent pool2", totals);
            auto list2 = take(pool2->createCommandList(), "pool2->createCommandList", totals);
            take(listA->begin(), "pool1 listA open while using pool2", totals);

            const auto p2Pattern = makePattern(kBufSize, 1234u);
            take(upload->write(0, p2Pattern), "write p2Pattern", totals);
            take(list2->begin(), "pool2 list2 begin while pool1 is open", totals);
            take(list2->transition(deviceBuf, gfx::ResourceState::CopyDestination), "pool2 CopyDst", totals);
            take(list2->copyBuffer(upload, deviceBuf, kBufSize), "pool2 copy 1", totals);
            take(list2->transition(deviceBuf, gfx::ResourceState::CopySource), "pool2 CopySrc", totals);
            take(list2->copyBuffer(deviceBuf, readback, kBufSize), "pool2 copy 2", totals);
            take(list2->end(), "pool2 list2 end", totals);
            const auto f2 = take(device.queue().submit(*list2), "submit pool2 list2", totals);
            ++totals.submissions;
            take(device.queue().wait(f2), "wait pool2", totals);
            take(pool2->reset(), "pool2 reset while pool1 is still open", totals);
            check(take(readback->read(0, kBufSize), "readback pool2", totals) == p2Pattern,
                "Independent pool2 execution must match", totals);

            take(listA->end(), "pool1 listA end", totals);
            const auto f1 = take(device.queue().submit(*listA), "submit pool1 listA", totals);
            ++totals.submissions;
            take(device.queue().wait(f1), "wait pool1", totals);
            take(pool->reset(), "pool1 reset", totals);
        }

        // 6. Off-thread rejections leave pool and lists intact.
        {
            gfx::Result<std::unique_ptr<gfx::CommandPool>> tCreatePool =
                std::unexpected(gfx::Error{gfx::ErrorCode::BackendFailure, "not run"});
            gfx::Result<std::unique_ptr<gfx::CommandList>> tCreateList =
                std::unexpected(gfx::Error{gfx::ErrorCode::BackendFailure, "not run"});
            gfx::Result<void> tReset = std::unexpected(gfx::Error{gfx::ErrorCode::BackendFailure, "not run"});
            gfx::Result<void> tBegin = std::unexpected(gfx::Error{gfx::ErrorCode::BackendFailure, "not run"});

            std::thread worker([&] {
                tCreatePool = device.createCommandPool();
                tCreateList = pool->createCommandList();
                tReset = pool->reset();
                tBegin = listA->begin();
            });
            worker.join();

            reject(tCreatePool, gfx::ErrorCode::InvalidState, "off-thread createCommandPool", totals);
            reject(tCreateList, gfx::ErrorCode::InvalidState, "off-thread pool->createCommandList", totals);
            reject(tReset, gfx::ErrorCode::InvalidState, "off-thread pool->reset", totals);
            reject(tBegin, gfx::ErrorCode::InvalidState, "off-thread pooled list->begin", totals);
        }
    }

    // 7. Unsubmitted pooled recording drops retained resource references on pool->reset().
    {
        auto reclaimPool = take(device.createCommandPool(), "create reclaimPool", totals);
        auto reclaimList = take(reclaimPool->createCommandList(), "create reclaimList", totals);
        const auto baseStats = take(device.resourceStatistics(), "baseStats before unsubmitted retention", totals);

        {
            auto tempBuf = take(device.createBuffer({4096, gfx::Memory::Device}), "tempBuf", totals);
            auto tempTex = take(device.createTexture({32, 32, gfx::Format::RGBA8UNorm, true}), "tempTex", totals);
            take(reclaimList->begin(), "reclaimList begin", totals);
            take(reclaimList->transition(tempBuf, gfx::ResourceState::CopySource), "retain tempBuf", totals);
            take(reclaimList->transition(tempTex, gfx::ResourceState::CopySource), "retain tempTex", totals);
            take(reclaimList->end(), "reclaimList end", totals);
        }

        const auto retainedStats = take(device.resourceStatistics(), "retainedStats before reset", totals);
        check(retainedStats.uniqueResources >= baseStats.uniqueResources + 2,
            "Unsubmitted pooled recording must retain referenced resources until reset", totals);

        take(reclaimPool->reset(), "reclaimPool reset releases unsubmitted references", totals);
        const auto afterResetStats = take(device.resourceStatistics(), "afterResetStats", totals);
        check(afterResetStats.uniqueResources == baseStats.uniqueResources,
            "CommandPool::reset() must release retained resources from unsubmitted recordings", totals);
        check(afterResetStats.resourceAllocationBytes == baseStats.resourceAllocationBytes,
            "CommandPool::reset() must reclaim bytes from unsubmitted recordings", totals);
    }

    // 8. Destroying CommandPool while submitted work is in-flight, and while an unsubmitted list survives.
    {
        constexpr std::uint64_t kBytes = 65536;
        auto upload = take(device.createBuffer({kBytes, gfx::Memory::Upload}), "ephemeral upload", totals);
        auto devBuf = take(device.createBuffer({kBytes, gfx::Memory::Device}), "ephemeral devBuf", totals);
        auto readback = take(device.createBuffer({kBytes, gfx::Memory::Readback}), "ephemeral readback", totals);
        const auto expected = makePattern(kBytes, 555u);
        take(upload->write(0, expected), "write ephemeral expected", totals);

        gfx::Fence inFlightFence{};
        {
            auto ephemeralPool = take(device.createCommandPool(), "create ephemeralPool", totals);
            auto ephemeralList = take(ephemeralPool->createCommandList(), "create ephemeralList", totals);
            take(ephemeralList->begin(), "ephemeralList begin", totals);
            take(ephemeralList->transition(devBuf, gfx::ResourceState::CopyDestination), "ephemeral CopyDst", totals);
            take(ephemeralList->copyBuffer(upload, devBuf, kBytes), "ephemeral copy 1", totals);
            take(ephemeralList->transition(devBuf, gfx::ResourceState::CopySource), "ephemeral CopySrc", totals);
            take(ephemeralList->copyBuffer(devBuf, readback, kBytes), "ephemeral copy 2", totals);
            take(ephemeralList->end(), "ephemeralList end", totals);
            inFlightFence = take(device.queue().submit(*ephemeralList), "submit ephemeralList", totals);
            ++totals.submissions;
            // Destroy both ephemeralPool and ephemeralList before waiting on GPU completion.
        }
        take(device.queue().wait(inFlightFence), "wait after destroying pool and list", totals);
        check(take(readback->read(0, kBytes), "readback after ephemeral pool drop", totals) == expected,
            "Submitted work must complete accurately after CommandPool destruction", totals);
    }
    {
        std::unique_ptr<gfx::CommandList> orphanList;
        {
            auto doomedPool = take(device.createCommandPool(), "create doomedPool", totals);
            orphanList = take(doomedPool->createCommandList(), "create orphanList", totals);
            take(orphanList->begin(), "orphanList begin", totals);
            take(orphanList->end(), "orphanList end", totals);
        }
        reject(device.queue().submit(*orphanList), gfx::ErrorCode::InvalidState,
            "Submitting command list whose CommandPool was destroyed must fail", totals);
        reject(orphanList->begin(), gfx::ErrorCode::InvalidState,
            "Beginning command list whose CommandPool was destroyed must fail", totals);
        orphanList.reset();
    }

    // 9. In-flight CommandPool::reset() rejection probe with real GPU workload.
    {
        constexpr std::uint64_t kHeavyBytes = 8ull * 1024ull * 1024ull;
        constexpr std::uint64_t kSentinelBytes = 4096;
        auto heavyPool = take(device.createCommandPool(), "create heavyPool", totals);
        auto heavyList = take(heavyPool->createCommandList(), "create heavyList", totals);
        auto heavySrc = take(device.createBuffer({kHeavyBytes, gfx::Memory::Device}), "heavySrc", totals);
        auto heavyDst = take(device.createBuffer({kHeavyBytes, gfx::Memory::Device}), "heavyDst", totals);
        auto sentinelUp = take(device.createBuffer({kSentinelBytes, gfx::Memory::Upload}), "sentinelUp", totals);
        auto sentinelRb = take(device.createBuffer({kSentinelBytes, gfx::Memory::Readback}), "sentinelRb", totals);
        const auto sentinel = makePattern(kSentinelBytes, 777u);
        take(sentinelUp->write(0, sentinel), "write sentinel", totals);

        for (unsigned attempt = 0; attempt < 3; ++attempt) {
            const unsigned copyRepeats = 16u << attempt;
            take(heavyList->begin(), "heavyList begin", totals);
            take(heavyList->transition(heavySrc, gfx::ResourceState::CopySource), "heavySrc -> CopySrc", totals);
            take(heavyList->transition(heavyDst, gfx::ResourceState::CopyDestination), "heavyDst -> CopyDst", totals);
            for (unsigned i = 0; i < copyRepeats; ++i) {
                take(heavyList->copyBuffer(heavySrc, heavyDst, kHeavyBytes), "heavy copy", totals);
            }
            take(heavyList->copyBuffer(sentinelUp, sentinelRb, kSentinelBytes), "sentinel copy", totals);
            take(heavyList->end(), "heavyList end", totals);

            const auto fence = take(device.queue().submit(*heavyList), "submit heavyList", totals);
            ++totals.submissions;

            const auto poll = device.queue().wait(fence, 0ms);
            if (!poll && poll.error().code == gfx::ErrorCode::Timeout) {
                reject(heavyPool->reset(), gfx::ErrorCode::InvalidState,
                    "CommandPool::reset() must reject while submitted work is in flight", totals);
                ++totals.inFlightResetRejections;
            }
            take(device.queue().wait(fence), "wait heavyList", totals);
            check(take(sentinelRb->read(0, kSentinelBytes), "read sentinel", totals) == sentinel,
                "Sentinel bytes after heavy copy must match", totals);
            take(heavyPool->reset(), "heavyPool reset after completion", totals);
            if (totals.inFlightResetRejections > 0) break;
        }
    }

    verifyReclaimed(device, totals);

    if (validation) {
        const auto errors = take(device.validationErrorCount(), "validationErrorCount", totals);
        check(errors == 0, "Validation error count must be zero", totals);
    }
}

void runWarpLossRecoveryCheck(Totals& totals) {
    gfx::DeviceDesc desc{};
    desc.backend = gfx::Backend::D3D12;
    desc.softwareAdapter = true;
    desc.enableDeviceLossTesting = true;
    const auto adapters = take(gfx::enumerateAdapters(gfx::Backend::D3D12), "enumerate D3D12 for loss check", totals);
    bool foundWarp = false;
    for (const auto& a : adapters) {
        if (a.softwareAdapter) {
            desc.adapter = a.id;
            foundWarp = true;
            break;
        }
    }
    check(foundWarp, "WARP adapter required for loss recovery check", totals);

    auto device = take(gfx::createDevice(desc), "create WARP loss device", totals);
    auto pool = take(device->createCommandPool(), "create pool before loss", totals);
    auto list = take(pool->createCommandList(), "create list before loss", totals);
    take(list->begin(), "begin before loss", totals);
    take(list->end(), "end before loss", totals);

    take(device->removeForTesting(), "removeForTesting", totals);
    reject(pool->createCommandList(), gfx::ErrorCode::DeviceLost, "pool->createCommandList after loss", totals);
    reject(pool->reset(), gfx::ErrorCode::DeviceLost, "pool->reset after loss", totals);
    reject(list->begin(), gfx::ErrorCode::DeviceLost, "pooled list->begin after loss", totals);

    auto recovered = take(device->recover(), "recover WARP device", totals);
    reject(recovered->queue().submit(*list), gfx::ErrorCode::WrongDevice,
        "Submitting old generation pooled list to recovered device must fail", totals);

    {
        auto freshPool = take(recovered->createCommandPool(), "create freshPool on recovered device", totals);
        auto freshList = take(freshPool->createCommandList(), "create freshList on recovered device", totals);
        auto up = take(recovered->createBuffer({256, gfx::Memory::Upload}), "fresh upload", totals);
        auto rb = take(recovered->createBuffer({256, gfx::Memory::Readback}), "fresh readback", totals);
        const auto expected = makePattern(256, 42u);
        take(up->write(0, expected), "write fresh upload", totals);
        take(freshList->begin(), "freshList begin", totals);
        take(freshList->copyBuffer(up, rb, 256), "fresh copy", totals);
        take(freshList->end(), "freshList end", totals);
        const auto f = take(recovered->queue().submit(*freshList), "fresh submit", totals);
        ++totals.submissions;
        take(recovered->queue().wait(f), "fresh wait", totals);
        check(take(rb->read(0, 256), "fresh read", totals) == expected, "Recovered pooled copy must match", totals);
        take(freshPool->reset(), "freshPool reset", totals);
    }
    verifyReclaimed(*recovered, totals);
}
} // namespace

int main(int argc, char** argv) {
    try {
        gfx::Backend backend = gfx::Backend::Vulkan;
        bool validation = false;
        bool warp = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--d3d12") backend = gfx::Backend::D3D12;
            else if (arg == "--vulkan") backend = gfx::Backend::Vulkan;
            else if (arg == "--metal") backend = gfx::Backend::Metal;
            else if (arg == "--validation") validation = true;
            else if (arg == "--warp") warp = true;
            else throw std::runtime_error("Unknown argument: " + arg);
        }

        const auto adapters = gfx::enumerateAdapters(backend);
        if (!adapters) {
            std::cerr << "Adapter enumeration failed: " << adapters.error().message << '\n';
            return 1;
        }

        std::vector<gfx::AdapterInfo> selected;
        for (const auto& adapter : *adapters) {
            if (warp) {
                if (adapter.softwareAdapter) selected.push_back(adapter);
            } else {
                if (!adapter.softwareAdapter) selected.push_back(adapter);
            }
        }
        if (selected.empty()) {
            std::cout << "SKIP no matching adapters\n";
            return 77;
        }

        Totals totals;
        for (const auto& adapter : selected) {
            gfx::DeviceDesc desc{};
            desc.backend = backend;
            desc.adapter = adapter.id;
            desc.softwareAdapter = warp;
            desc.validation = validation;
            auto device = take(gfx::createDevice(desc), "createDevice", totals);
            runCommandPoolSuite(*device, validation, totals);
        }

        if (backend == gfx::Backend::D3D12 && warp && !validation) {
            runWarpLossRecoveryCheck(totals);
        }

        std::cout << "PASS command_pool adapters=" << selected.size()
                  << " assertions=" << totals.assertions
                  << " epochs=" << totals.epochs
                  << " submissions=" << totals.submissions
                  << " rejections=" << totals.rejections
                  << " inflight_reset_rejections=" << totals.inFlightResetRejections
                  << " errors=0\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL command_pool: " << error.what() << '\n';
        return 1;
    }
}
