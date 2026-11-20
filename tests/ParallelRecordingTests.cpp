#include <graphics/Graphics.hpp>
#include "fixtures/VertexFixtures.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
unsigned assertions{};
unsigned batches{};
unsigned images{};
unsigned rejections{};

void check(bool condition, const char* message) {
    ++assertions;
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <class T>
T take(gfx::Result<T> result) {
    if (!result) {
        throw std::runtime_error(
            result.error().message + " (code=" +
            std::to_string(static_cast<int>(result.error().code)) +
            ", native=" + std::to_string(result.error().nativeCode) + ")");
    }
    return std::move(*result);
}

void take(gfx::Result<void> result) {
    if (!result) {
        throw std::runtime_error(
            result.error().message + " (code=" +
            std::to_string(static_cast<int>(result.error().code)) +
            ", native=" + std::to_string(result.error().nativeCode) + ")");
    }
}

template <class T>
void expectError(const gfx::Result<T>& result, gfx::ErrorCode expected, const char* message) {
    check(!result && result.error().code == expected, message);
    ++rejections;
}

template <std::size_t N>
gfx::ShaderBinary spirvBinary(const std::array<std::uint32_t, N>& words, gfx::ShaderStage stage) {
    const auto bytes = std::as_bytes(std::span{words});
    return {stage, {bytes.begin(), bytes.end()}, gfx::ShaderFormat::SPIRV};
}

constexpr auto kHlslVertex = R"(
struct VsIn {
    float3 pos : POSITION;
    float4 col : COLOR;
};
struct VsOut {
    float4 pos : SV_Position;
    float4 col : COLOR;
};
VsOut main(VsIn input) {
    VsOut output;
    output.pos = float4(input.pos, 1.0);
    output.col = input.col;
    return output;
}
)";

constexpr auto kHlslFragment = R"(
struct PsIn {
    float4 pos : SV_Position;
    float4 col : COLOR;
};
float4 main(PsIn input) : SV_Target0 {
    return input.col;
}
)";

struct Vertex {
    float x, y, z;
    float r, g, b, a;
};

gfx::VertexLayout vertexLayout() {
    return {
        {{0, sizeof(Vertex), gfx::VertexInputRate::Vertex}},
        {
            {"POSITION", 0, gfx::VertexFormat::Float3, 0, 0},
            {"COLOR", 0, gfx::VertexFormat::Float4, 0, 12},
        },
    };
}

gfx::PipelinePtr makeSharedPipeline(gfx::Device& device, gfx::Backend backend) {
    gfx::GraphicsPipelineDesc desc;
    if (backend == gfx::Backend::D3D12) {
        desc.vertex = take(gfx::compileHlsl(kHlslVertex, "main", gfx::ShaderStage::Vertex));
        desc.fragment = take(gfx::compileHlsl(kHlslFragment, "main", gfx::ShaderStage::Fragment));
    } else {
        desc.vertex = spirvBinary(vertexFixtures::position3, gfx::ShaderStage::Vertex);
        desc.fragment = spirvBinary(vertexFixtures::color, gfx::ShaderStage::Fragment);
    }
    desc.colorFormat = gfx::Format::RGBA8UNorm;
    desc.vertexLayout = vertexLayout();
    desc.renderTargets = gfx::RenderTargetLayout{{gfx::Format::RGBA8UNorm}, std::nullopt, 1u};
    return take(device.createGraphicsPipeline(desc));
}

void verifyEmptyStats(gfx::Device& device) {
    const auto stats = take(device.resourceStatistics());
    check(stats.trackedObjects == 0 &&
          stats.uniqueResources == 0 &&
          stats.resourceAllocationBytes == 0 &&
          stats.pendingSubmissions == 0 &&
          stats.pendingObjectReferences == 0,
          "All parallel recording resources, pools, and submissions must reclaim to zero");
}

void verifyAdapter(gfx::Device& device, gfx::Device& otherDevice, gfx::Backend backend, bool validation) {
    constexpr std::uint32_t kWorkers = 4;
    constexpr std::uint32_t kTexSize = 32;
    constexpr std::uint32_t kEpochs = 6;

    auto pipeline = makeSharedPipeline(device, backend);

    // Build shared read-only vertex buffer: 4 distinct fullscreen triangles (one per worker color).
    const std::array<std::array<float, 4>, kWorkers> workerColors{{
        {1.0f, 0.25f, 0.0f, 1.0f},
        {0.0f, 1.0f, 0.25f, 1.0f},
        {0.25f, 0.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 0.25f, 1.0f},
    }};
    std::vector<Vertex> sharedVerts;
    sharedVerts.reserve(kWorkers * 3);
    for (std::uint32_t w = 0; w < kWorkers; ++w) {
        const auto& c = workerColors[w];
        sharedVerts.push_back({-1.0f, -1.0f, 0.5f, c[0], c[1], c[2], c[3]});
        sharedVerts.push_back({ 3.0f, -1.0f, 0.5f, c[0], c[1], c[2], c[3]});
        sharedVerts.push_back({-1.0f,  3.0f, 0.5f, c[0], c[1], c[2], c[3]});
    }
    auto sharedVb = take(device.createBuffer({sharedVerts.size() * sizeof(Vertex), gfx::Memory::Upload}));
    take(sharedVb->write(0, std::as_bytes(std::span{sharedVerts})));

    // Shared read-only upload buffer for parallel transfer test
    constexpr std::uint64_t kCopyBytes = 4096;
    std::vector<std::byte> sharedPayload(static_cast<std::size_t>(kCopyBytes * kWorkers));
    for (std::size_t i = 0; i < sharedPayload.size(); ++i) {
        sharedPayload[i] = static_cast<std::byte>((i * 37u + 13u) & 0xFFu);
    }
    auto sharedUpload = take(device.createBuffer({sharedPayload.size(), gfx::Memory::Upload}));
    take(sharedUpload->write(0, sharedPayload));

    // Pre-transition shared read-only VB to VertexBuffer on creating thread before parallel phase.
    {
        auto initCmd = take(device.createCommandList());
        take(initCmd->begin());
        take(initCmd->transition(sharedVb, gfx::ResourceState::VertexBuffer));
        take(initCmd->end());
        take(device.queue().wait(take(device.queue().submit(*initCmd)), std::chrono::seconds{10}));
    }

    // 1. 4-Worker Parallel Graphics + Transfer Recording & Single Ordered Batch Submit over 6 Epochs
    {
        std::vector<std::unique_ptr<gfx::CommandPool>> pools(kWorkers);
        std::vector<std::unique_ptr<gfx::CommandList>> lists(kWorkers);
        std::vector<gfx::TexturePtr> targets(kWorkers);
        std::vector<gfx::BufferPtr> texReadbacks(kWorkers);
        std::vector<gfx::BufferPtr> devBuffers(kWorkers);
        std::vector<gfx::BufferPtr> bufReadbacks(kWorkers);

        for (std::uint32_t w = 0; w < kWorkers; ++w) {
            pools[w] = take(device.createCommandPool());
            lists[w] = take(pools[w]->createCommandList());
            targets[w] = take(device.createTexture({kTexSize, kTexSize, gfx::Format::RGBA8UNorm, true}));
            texReadbacks[w] = take(device.createBuffer({targets[w]->footprint().size, gfx::Memory::Readback}));
            devBuffers[w] = take(device.createBuffer({kCopyBytes, gfx::Memory::Device}));
            bufReadbacks[w] = take(device.createBuffer({kCopyBytes, gfx::Memory::Readback}));
        }

        for (std::uint32_t epoch = 0; epoch < kEpochs; ++epoch) {
            std::array<std::string, kWorkers> workerErrors{};
            std::vector<std::thread> threads;
            threads.reserve(kWorkers);
            for (std::uint32_t w = 0; w < kWorkers; ++w) {
                threads.emplace_back([&, w, epoch] {
                    try {
                        take(pools[w]->bindRecordingThread());
                        auto& cmd = *lists[w];
                        take(cmd.begin());
                        // Parallel buffer copy from shared read-only upload buffer to disjoint device/readback buffers
                        take(cmd.transition(devBuffers[w], gfx::ResourceState::CopyDestination));
                        take(cmd.copyBufferRegion(sharedUpload, w * kCopyBytes, devBuffers[w], 0, kCopyBytes));
                        take(cmd.transition(devBuffers[w], gfx::ResourceState::CopySource));
                        take(cmd.copyBufferRegion(devBuffers[w], 0, bufReadbacks[w], 0, kCopyBytes));

                        // Parallel render pass sharing read-only pipeline & vertex buffer
                        take(cmd.transition(targets[w], gfx::ResourceState::RenderTarget));
                        take(cmd.beginRenderPass(targets[w], {0.0f, 0.0f, 0.0f, 1.0f}));
                        take(cmd.setPipeline(pipeline));
                        take(cmd.bindVertexBuffer(0, sharedVb, sizeof(Vertex)));
                        const std::uint32_t colorIndex = (w + epoch) % kWorkers;
                        for (std::uint32_t ty = 0; ty < 4; ++ty) {
                            for (std::uint32_t tx = 0; tx < 4; ++tx) {
                                take(cmd.setViewport({static_cast<float>(tx * 8u), static_cast<float>(ty * 8u), 8.0f, 8.0f, 0.0f, 1.0f}));
                                take(cmd.setScissor(tx * 8u, ty * 8u, 8u, 8u));
                                take(cmd.draw(3, 1, colorIndex * 3u));
                            }
                        }
                        take(cmd.endRenderPass());
                        take(cmd.transition(targets[w], gfx::ResourceState::CopySource));
                        take(cmd.copyTextureToBuffer(targets[w], texReadbacks[w]));
                        take(cmd.end());
                    } catch (const std::exception& e) {
                        workerErrors[w] = e.what();
                    }
                });
            }
            for (auto& t : threads) t.join();
            for (std::uint32_t w = 0; w < kWorkers; ++w) {
                if (!workerErrors[w].empty()) {
                    throw std::runtime_error("Worker " + std::to_string(w) + " failed: " + workerErrors[w]);
                }
            }

            std::array<gfx::CommandList*, kWorkers> batch{};
            for (std::uint32_t w = 0; w < kWorkers; ++w) batch[w] = lists[w].get();
            const auto fence = take(device.queue().submit(batch));
            ++batches;
            take(device.queue().wait(fence, std::chrono::seconds{10}));

            // Verify every byte of every buffer and every pixel of every target
            for (std::uint32_t w = 0; w < kWorkers; ++w) {
                const auto actualBuf = take(bufReadbacks[w]->read(0, kCopyBytes));
                const std::size_t base = static_cast<std::size_t>(w * kCopyBytes);
                check(std::memcmp(actualBuf.data(), sharedPayload.data() + base, static_cast<std::size_t>(kCopyBytes)) == 0,
                      "Parallel worker buffer copy bytes must match exactly");

                const auto fp = targets[w]->footprint();
                const auto actualPixels = take(texReadbacks[w]->read(0, fp.size));
                const std::uint32_t colorIndex = (w + epoch) % kWorkers;
                const auto& c = workerColors[colorIndex];
                const int er = static_cast<int>(std::lround(c[0] * 255.0f));
                const int eg = static_cast<int>(std::lround(c[1] * 255.0f));
                const int eb = static_cast<int>(std::lround(c[2] * 255.0f));
                for (std::uint32_t y = 0; y < kTexSize; ++y) {
                    for (std::uint32_t x = 0; x < kTexSize; ++x) {
                        const auto* px = reinterpret_cast<const std::uint8_t*>(
                            actualPixels.data() + static_cast<std::size_t>(y) * fp.rowPitch + static_cast<std::size_t>(x) * 4u);
                        if (std::abs(static_cast<int>(px[0]) - er) > 1 ||
                            std::abs(static_cast<int>(px[1]) - eg) > 1 ||
                            std::abs(static_cast<int>(px[2]) - eb) > 1 ||
                            px[3] != 255u) {
                            throw std::runtime_error("Parallel rendered pixel mismatch");
                        }
                    }
                }
                ++assertions;
                ++images;
                take(pools[w]->reset());
            }
        }
    }

    // 2. Thread-Affinity & Batch Conflict Rejection Contracts
    {
        auto poolA = take(device.createCommandPool());
        auto poolB = take(device.createCommandPool());
        auto listA = take(poolA->createCommandList());
        auto listB = take(poolB->createCommandList());
        auto otherList = take(otherDevice.createCommandList());
        auto sharedBuf = take(device.createBuffer({256, gfx::Memory::Device}));
        auto rbBuf = take(device.createBuffer({256, gfx::Memory::Readback}));

        // Empty / null / duplicate / foreign batch submit rejections
        expectError(device.queue().submit(std::span<gfx::CommandList* const>{}),
                    gfx::ErrorCode::InvalidArgument, "Empty batch submit rejected");
        std::array<gfx::CommandList*, 1> nullBatch{nullptr};
        expectError(device.queue().submit(nullBatch),
                    gfx::ErrorCode::InvalidArgument, "Null command list in batch rejected");

        // Bind poolA to worker thread and verify main thread is rejected while bound there
        std::thread bindWorker([&] {
            take(poolA->bindRecordingThread());
        });
        bindWorker.join();
        expectError(listA->begin(), gfx::ErrorCode::InvalidState,
                    "Main thread rejected while poolA is bound to another thread");

        // Bind poolA back to main thread and verify bindRecordingThread while open is rejected
        take(poolA->bindRecordingThread());
        take(listA->begin());
        {
            gfx::Result<void> offBind = std::unexpected(gfx::Error{gfx::ErrorCode::BackendFailure, "not run"});
            std::thread t([&] { offBind = poolA->bindRecordingThread(); });
            t.join();
            expectError(offBind, gfx::ErrorCode::InvalidState,
                        "bindRecordingThread while pool list is recording rejected");
        }
        take(listA->transition(sharedBuf, gfx::ResourceState::CopyDestination));
        take(listA->copyBufferRegion(sharedUpload, 0, sharedBuf, 0, 256));
        take(listA->end());

        // Duplicate list in batch rejected
        std::array<gfx::CommandList*, 2> dupBatch{listA.get(), listA.get()};
        expectError(device.queue().submit(dupBatch),
                    gfx::ErrorCode::InvalidArgument, "Duplicate command list in batch rejected");

        // Foreign list in batch rejected atomically (listA stays submittable)
        take(otherList->begin());
        take(otherList->end());
        std::array<gfx::CommandList*, 2> foreignBatch{listA.get(), otherList.get()};
        expectError(device.queue().submit(foreignBatch),
                    gfx::ErrorCode::WrongDevice, "Foreign command list in batch rejected");

        // Conflicting mutation across listA and listB on the same resource rejected atomically
        take(listB->begin());
        take(listB->transition(sharedBuf, gfx::ResourceState::CopyDestination));
        take(listB->copyBufferRegion(sharedUpload, 256, sharedBuf, 0, 256));
        take(listB->end());
        std::array<gfx::CommandList*, 2> conflictBatch{listA.get(), listB.get()};
        expectError(device.queue().submit(conflictBatch),
                    gfx::ErrorCode::InvalidState, "Conflicting writes to same resource in parallel batch rejected");

        // Atomic preservation: listA still submits and executes cleanly on its own!
        std::array<gfx::CommandList*, 1> singleBatch{listA.get()};
        take(device.queue().wait(take(device.queue().submit(singleBatch)), std::chrono::seconds{10}));
        ++batches;

        // Read back sharedBuf to verify listA's bytes (offset 0, not listB's offset 256)
        take(poolA->reset());
        take(poolB->reset());
        take(listA->begin());
        take(listA->transition(sharedBuf, gfx::ResourceState::CopySource));
        take(listA->copyBufferRegion(sharedBuf, 0, rbBuf, 0, 256));
        take(listA->end());
        take(device.queue().wait(take(device.queue().submit(*listA)), std::chrono::seconds{10}));
        ++batches;
        const auto got = take(rbBuf->read(0, 256));
        check(std::memcmp(got.data(), sharedPayload.data(), 256) == 0,
              "Atomic batch rejection must preserve listA execution and exclude listB");
        take(poolA->reset());
    }

    sharedVb.reset();
    sharedUpload.reset();
    pipeline.reset();
    verifyEmptyStats(device);
    if (validation) {
        const auto errs = take(device.validationErrorCount());
        check(errs == 0u, "Validation layer must report zero parallel recording errors");
    }
}

void verifyWarpLossRecovery() {
    auto adapters = take(gfx::enumerateAdapters(gfx::Backend::D3D12));
    auto it = std::find_if(adapters.begin(), adapters.end(), [](const gfx::AdapterInfo& info) {
        return info.softwareAdapter;
    });
    check(it != adapters.end(), "WARP adapter required for parallel loss/recovery test");

    gfx::DeviceDesc desc{gfx::Backend::D3D12, false, true, true, it->id};
    auto device = take(gfx::createDevice(desc));
    {
        auto other = take(gfx::createDevice({gfx::Backend::D3D12, false, true, false, it->id}));
        verifyAdapter(*device, *other, gfx::Backend::D3D12, false);
    }

    auto lostPool = take(device->createCommandPool());
    auto lostList = take(lostPool->createCommandList());
    take(device->removeForTesting());
    {
        gfx::Result<void> workerBind = std::unexpected(gfx::Error{gfx::ErrorCode::BackendFailure, "not run"});
        std::thread t([&] { workerBind = lostPool->bindRecordingThread(); });
        t.join();
        expectError(workerBind, gfx::ErrorCode::DeviceLost, "Lost device rejects worker bindRecordingThread");
    }
    lostList.reset();
    lostPool.reset();

    auto recovered = take(device->recover());
    auto recoveredOther = take(gfx::createDevice({gfx::Backend::D3D12, false, true, false, it->id}));
    verifyAdapter(*recovered, *recoveredOther, gfx::Backend::D3D12, false);
}

struct PersistentThreadPool {
    std::vector<std::thread> workers;
    std::mutex mutex;
    std::condition_variable workCv;
    std::condition_variable doneCv;
    std::uint64_t generation{};
    std::uint32_t remaining{};
    bool stop{};
    std::function<void(std::uint32_t)> task;
    std::vector<std::string> errors;

    explicit PersistentThreadPool(std::uint32_t count) : errors(count) {
        workers.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            workers.emplace_back([this, i] {
                std::uint64_t seen = 0;
                for (;;) {
                    std::unique_lock lock(mutex);
                    workCv.wait(lock, [&] { return stop || generation != seen; });
                    if (stop) return;
                    seen = generation;
                    auto fn = task;
                    lock.unlock();
                    std::string err;
                    try {
                        fn(i);
                    } catch (const std::exception& e) {
                        err = e.what();
                    }
                    lock.lock();
                    if (!err.empty()) errors[i] = std::move(err);
                    if (--remaining == 0) doneCv.notify_one();
                }
            });
        }
    }

    void run(std::function<void(std::uint32_t)> fn) {
        {
            std::scoped_lock lock(mutex);
            for (auto& e : errors) e.clear();
            task = std::move(fn);
            remaining = static_cast<std::uint32_t>(workers.size());
            ++generation;
        }
        workCv.notify_all();
        std::unique_lock lock(mutex);
        doneCv.wait(lock, [&] { return remaining == 0; });
        for (const auto& e : errors) {
            if (!e.empty()) throw std::runtime_error(e);
        }
    }

    ~PersistentThreadPool() {
        {
            std::scoped_lock lock(mutex);
            stop = true;
            ++generation;
        }
        workCv.notify_all();
        for (auto& w : workers) w.join();
    }
};

double medianMs(std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    const std::size_t n = samples.size();
    return (n % 2 == 1) ? samples[n / 2] : 0.5 * (samples[n / 2 - 1] + samples[n / 2]);
}

void runBenchmark(gfx::Backend backend) {
    auto adapters = take(gfx::enumerateAdapters(backend));
    auto it = std::find_if(adapters.begin(), adapters.end(), [](const gfx::AdapterInfo& info) {
        return !info.softwareAdapter;
    });
    if (it == adapters.end()) {
        std::cout << "SKIP benchmark no hardware adapter\n";
        return;
    }

    auto device = take(gfx::createDevice({backend, false, false, false, it->id}));
    auto pipeline = makeSharedPipeline(*device, backend);

    constexpr std::uint32_t kLists = 16;
    constexpr std::uint32_t kDrawsPerList = 1024;
    constexpr std::uint32_t kTexSize = 16;
    constexpr std::uint32_t kWarmupFrames = 2;
    constexpr std::uint32_t kMeasuredFrames = 10;

    std::vector<Vertex> verts{
        {-1.0f, -1.0f, 0.5f, 0.25f, 0.75f, 0.5f, 1.0f},
        { 3.0f, -1.0f, 0.5f, 0.25f, 0.75f, 0.5f, 1.0f},
        {-1.0f,  3.0f, 0.5f, 0.25f, 0.75f, 0.5f, 1.0f},
    };
    auto vb = take(device->createBuffer({verts.size() * sizeof(Vertex), gfx::Memory::Upload}));
    take(vb->write(0, std::as_bytes(std::span{verts})));
    {
        auto initCmd = take(device->createCommandList());
        take(initCmd->begin());
        take(initCmd->transition(vb, gfx::ResourceState::VertexBuffer));
        take(initCmd->end());
        take(device->queue().wait(take(device->queue().submit(*initCmd)), std::chrono::seconds{10}));
    }

    std::vector<std::unique_ptr<gfx::CommandPool>> pools(kLists);
    std::vector<std::unique_ptr<gfx::CommandList>> lists(kLists);
    std::vector<gfx::TexturePtr> targets(kLists);
    std::vector<gfx::BufferPtr> readbacks(kLists);
    for (std::uint32_t i = 0; i < kLists; ++i) {
        pools[i] = take(device->createCommandPool());
        lists[i] = take(pools[i]->createCommandList());
        targets[i] = take(device->createTexture({kTexSize, kTexSize, gfx::Format::RGBA8UNorm, true}));
        readbacks[i] = take(device->createBuffer({targets[i]->footprint().size, gfx::Memory::Readback}));
    }

    auto recordSlice = [&](std::uint32_t idx, bool copyReadback) {
        auto& cmd = *lists[idx];
        take(cmd.begin());
        take(cmd.transition(targets[idx], gfx::ResourceState::RenderTarget));
        take(cmd.beginRenderPass(targets[idx], {0.0f, 0.0f, 0.0f, 1.0f}));
        take(cmd.setPipeline(pipeline));
        take(cmd.bindVertexBuffer(0, vb, sizeof(Vertex)));
        for (std::uint32_t d = 0; d < kDrawsPerList; ++d) {
            const float vx = static_cast<float>((d & 3u) * 4u);
            const float vy = static_cast<float>(((d >> 2u) & 3u) * 4u);
            take(cmd.setViewport({vx, vy, 4.0f, 4.0f, 0.0f, 1.0f}));
            take(cmd.setScissor(static_cast<std::uint32_t>(vx), static_cast<std::uint32_t>(vy), 4u, 4u));
            take(cmd.draw(3));
        }
        take(cmd.endRenderPass());
        if (copyReadback) {
            take(cmd.transition(targets[idx], gfx::ResourceState::CopySource));
            take(cmd.copyTextureToBuffer(targets[idx], readbacks[idx]));
        }
        take(cmd.end());
    };

    const unsigned hwThreads = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::uint32_t> configs{1u};
    if (hwThreads >= 2u) configs.push_back(2u);
    if (hwThreads >= 4u) configs.push_back(4u);

    std::cout << "BENCHMARK backend=" << (backend == gfx::Backend::D3D12 ? "D3D12" : "Vulkan")
              << " adapter=\"" << device->caps().adapterName << "\""
              << " lists=" << kLists
              << " draws_per_frame=" << (kLists * kDrawsPerList)
              << " frames=" << kMeasuredFrames
              << " hw_threads=" << hwThreads << "\n";

    double baselineRecordP50 = 0.0;
    for (const std::uint32_t numThreads : configs) {
        std::vector<double> recordSamples;
        std::vector<double> submitWaitSamples;
        recordSamples.reserve(kMeasuredFrames);
        submitWaitSamples.reserve(kMeasuredFrames);

        if (numThreads == 1u) {
            for (std::uint32_t i = 0; i < kLists; ++i) take(pools[i]->bindRecordingThread());
            for (std::uint32_t f = 0; f < kWarmupFrames + kMeasuredFrames; ++f) {
                const bool lastFrame = (f + 1 == kWarmupFrames + kMeasuredFrames);
                const auto t0 = std::chrono::steady_clock::now();
                for (std::uint32_t i = 0; i < kLists; ++i) recordSlice(i, lastFrame);
                const auto t1 = std::chrono::steady_clock::now();

                std::vector<gfx::CommandList*> batch(kLists);
                for (std::uint32_t i = 0; i < kLists; ++i) batch[i] = lists[i].get();
                const auto fence = take(device->queue().submit(batch));
                take(device->queue().wait(fence, std::chrono::seconds{30}));
                const auto t2 = std::chrono::steady_clock::now();

                for (std::uint32_t i = 0; i < kLists; ++i) take(pools[i]->reset());
                if (f >= kWarmupFrames) {
                    recordSamples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
                    submitWaitSamples.push_back(std::chrono::duration<double, std::milli>(t2 - t1).count());
                }
            }
        } else {
            PersistentThreadPool threadPool(numThreads);
            const std::uint32_t perWorker = kLists / numThreads;
            // Bind each worker's pools once before timed frames
            threadPool.run([&](std::uint32_t workerId) {
                for (std::uint32_t i = workerId * perWorker; i < (workerId + 1u) * perWorker; ++i) {
                    take(pools[i]->bindRecordingThread());
                }
            });
            for (std::uint32_t f = 0; f < kWarmupFrames + kMeasuredFrames; ++f) {
                const bool lastFrame = (f + 1 == kWarmupFrames + kMeasuredFrames);
                const auto t0 = std::chrono::steady_clock::now();
                threadPool.run([&](std::uint32_t workerId) {
                    for (std::uint32_t i = workerId * perWorker; i < (workerId + 1u) * perWorker; ++i) {
                        recordSlice(i, lastFrame);
                    }
                });
                const auto t1 = std::chrono::steady_clock::now();

                std::vector<gfx::CommandList*> batch(kLists);
                for (std::uint32_t i = 0; i < kLists; ++i) batch[i] = lists[i].get();
                const auto fence = take(device->queue().submit(batch));
                take(device->queue().wait(fence, std::chrono::seconds{30}));
                const auto t2 = std::chrono::steady_clock::now();

                for (std::uint32_t i = 0; i < kLists; ++i) take(pools[i]->reset());
                if (f >= kWarmupFrames) {
                    recordSamples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
                    submitWaitSamples.push_back(std::chrono::duration<double, std::milli>(t2 - t1).count());
                }
            }
        }

        // Verify ALL 16 targets' pixels after every thread-count configuration
        for (std::uint32_t i = 0; i < kLists; ++i) {
            const auto fp = targets[i]->footprint();
            const auto pixels = take(readbacks[i]->read(0, fp.size));
            for (std::uint32_t y = 0; y < kTexSize; ++y) {
                for (std::uint32_t x = 0; x < kTexSize; ++x) {
                    const auto* px = reinterpret_cast<const std::uint8_t*>(
                        pixels.data() + static_cast<std::size_t>(y) * fp.rowPitch + static_cast<std::size_t>(x) * 4u);
                    if (std::abs(static_cast<int>(px[0]) - 64) > 1 ||
                        std::abs(static_cast<int>(px[1]) - 191) > 1 ||
                        std::abs(static_cast<int>(px[2]) - 128) > 1 ||
                        px[3] != 255u) {
                        throw std::runtime_error("Benchmark pixel verification mismatch");
                    }
                }
            }
        }

        const double recP50 = medianMs(recordSamples);
        const double recMin = *std::min_element(recordSamples.begin(), recordSamples.end());
        const double subP50 = medianMs(submitWaitSamples);
        if (numThreads == 1u) baselineRecordP50 = recP50;
        const double speedup = (recP50 > 0.0) ? (baselineRecordP50 / recP50) : 0.0;
        std::cout << std::fixed << std::setprecision(3)
                  << "  threads=" << numThreads
                  << " cpu_record_p50_ms=" << recP50
                  << " cpu_record_min_ms=" << recMin
                  << " speedup=" << speedup << "x"
                  << " cpu_submit_wait_p50_ms=" << subP50
                  << " verified_targets=" << kLists << "\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        bool useD3D12 = false;
        bool useVulkan = false;
        bool validation = false;
        bool warp = false;
        bool benchmark = false;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg(argv[i]);
            if (arg == "--d3d12") useD3D12 = true;
            else if (arg == "--vulkan") useVulkan = true;
            else if (arg == "--validation") validation = true;
            else if (arg == "--warp") warp = true;
            else if (arg == "--benchmark") benchmark = true;
            else throw std::runtime_error("Unknown argument: " + std::string(arg));
        }
        if (useD3D12 == useVulkan) {
            throw std::runtime_error("Specify exactly one of --d3d12 or --vulkan");
        }
        const gfx::Backend backend = useD3D12 ? gfx::Backend::D3D12 : gfx::Backend::Vulkan;
        if (benchmark) {
            runBenchmark(backend);
            return 0;
        }
        if (warp) {
            if (!useD3D12) throw std::runtime_error("--warp is only valid with --d3d12");
            verifyWarpLossRecovery();
            std::cout << "PASS parallel_recording adapters=1 assertions=" << assertions
                      << " batches=" << batches
                      << " images=" << images
                      << " rejections=" << rejections
                      << " errors=0\n";
            return 0;
        }

        auto adapters = take(gfx::enumerateAdapters(backend));
        unsigned tested = 0;
        for (const auto& info : adapters) {
            if (info.softwareAdapter) continue;
            auto device = take(gfx::createDevice({backend, validation, false, false, info.id}));
            auto other = take(gfx::createDevice({backend, false, false, false, info.id}));
            verifyAdapter(*device, *other, backend, validation);
            ++tested;
        }
        if (!tested) {
            std::cout << "SKIP parallel_recording no hardware adapters\n";
            return 77;
        }
        std::cout << "PASS parallel_recording adapters=" << tested
                  << " assertions=" << assertions
                  << " batches=" << batches
                  << " images=" << images
                  << " rejections=" << rejections
                  << " errors=0\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL parallel_recording: " << e.what() << "\n";
        return 1;
    }
}
