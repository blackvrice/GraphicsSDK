#include <graphics/Graphics.hpp>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace {
using namespace std::chrono_literals;
unsigned assertions{};
void check(bool value, const char* message) { ++assertions; if (!value) throw std::runtime_error(message); }
template<class T> T take(gfx::Result<T> result) {
    if (!result) throw std::runtime_error(result.error().message + " native=" + std::to_string(result.error().nativeCode));
    return std::move(*result);
}
void take(gfx::Result<void> result) { if (!result) throw std::runtime_error(result.error().message); }
template<class T> void reject(const gfx::Result<T>& result, gfx::ErrorCode code, const char* message) {
    check(!result && result.error().code == code, message);
}
std::string hex(std::uint64_t value) { std::ostringstream out; out << std::hex << std::setfill('0') << std::setw(16) << value; return out.str(); }
gfx::DeviceDesc description(std::optional<gfx::AdapterId> id = {}) { gfx::DeviceDesc d; d.backend = gfx::Backend::Metal; d.adapter = id; return d; }
}
int main(int argc, char** argv) try {
    check(argc == 1 || argc == 2, "Optional source fingerprint only");
    if (argc == 2) check(gfx::buildFingerprint() == argv[1], "Installed/source fingerprint mismatch");
    check(gfx::backendCompiled(gfx::Backend::Metal), "Metal backend required, no skip");
    const auto adapters = take(gfx::enumerateAdapters(gfx::Backend::Metal));
    check(!adapters.empty(), "Native Metal device required, no skip");
    std::uint64_t previous{};
    for (const auto& adapter : adapters) {
        check(adapter.id.backend == gfx::Backend::Metal && adapter.id.value > previous && !adapter.name.empty(), "Ordered unique native identities");
        previous = adapter.id.value;
        auto exact = take(gfx::createDevice(description(adapter.id)));
        check(exact->caps().adapterId == adapter.id && exact->caps().adapterName == adapter.name, "Exact registry selection without fallback");
    }
    reject(gfx::createDevice(description(gfx::AdapterId{gfx::Backend::Metal, 0})), gfx::ErrorCode::Unsupported, "Absent registry ID must not fallback");
    reject(gfx::createDevice(description(gfx::AdapterId{gfx::Backend::Vulkan, 1})), gfx::ErrorCode::InvalidArgument, "Foreign backend adapter");
    for (int policy = 0; policy < 3; ++policy) {
        auto d = description();
        if (policy == 0) d.validation = true;
        if (policy == 1) d.softwareAdapter = true;
        if (policy == 2) d.enableDeviceLossTesting = true;
        reject(gfx::createDevice(d), gfx::ErrorCode::Unsupported, "Unavailable policy must not silently downgrade");
    }
    auto device = take(gfx::createDevice(description()));
    const auto caps = device->caps();
    check(caps.backend == gfx::Backend::Metal && !caps.softwareAdapter, "Metal capabilities and selection policy");
    check(std::any_of(adapters.begin(), adapters.end(), [&](const auto& a) { return a.id == caps.adapterId && a.name == caps.adapterName; }), "Default adapter belongs to inventory");
    check(!caps.compute && !caps.timestampQueries && !caps.swapChain && !caps.resourceBinding && !caps.vertexInput && !caps.indexedDrawing &&
        !caps.bindingLayouts && !caps.depthAttachment && !caps.multipleRenderTargets && !caps.deviceRecovery && !caps.deviceLossInjection, "No unimplemented capability advertised");
    auto foreign = take(gfx::createDevice(description(caps.adapterId)));
    auto& queue = device->queue();
    check(take(queue.completedValue()) == 0, "No completed fence before submission");
    take(queue.waitIdle());
    auto commands = take(device->createCommandList());
    reject(commands->end(), gfx::ErrorCode::InvalidState, "End before begin");
    reject(commands->debugMarker("before"), gfx::ErrorCode::InvalidState, "Marker before begin");
    reject(queue.submit(*commands), gfx::ErrorCode::InvalidState, "Submit before recording");
    take(commands->begin());
    reject(commands->begin(), gfx::ErrorCode::InvalidState, "Double begin");
    reject(queue.submit(*commands), gfx::ErrorCode::InvalidState, "Submit while open");
    reject(commands->debugMarker(""), gfx::ErrorCode::InvalidArgument, "Empty marker");
    reject(commands->debugMarker(std::string_view("x\0y", 3)), gfx::ErrorCode::InvalidArgument, "NUL marker");
    reject(commands->debugMarker(std::string_view("\xff", 1)), gfx::ErrorCode::InvalidArgument, "Invalid UTF-8 marker");
    take(commands->debugMarker("native Metal first submission"));
    take(commands->end());
    reject(commands->end(), gfx::ErrorCode::InvalidState, "Double end");
    reject(foreign->queue().submit(*commands), gfx::ErrorCode::WrongDevice, "Foreign command list");
    auto fence = take(queue.submit(*commands));
    check(fence.value == 1 && fence.device, "Nonzero first fence");
    reject(queue.submit(*commands), gfx::ErrorCode::InvalidState, "Double submission");
    reject(foreign->queue().wait(fence, 0ms), gfx::ErrorCode::WrongDevice, "Foreign fence");
    reject(queue.wait({0, fence.device}, 0ms), gfx::ErrorCode::InvalidArgument, "Zero fence");
    reject(queue.wait({2, fence.device}, 0ms), gfx::ErrorCode::InvalidArgument, "Future fence");
    auto polled = queue.wait(fence, 0ms);
    check(polled || polled.error().code == gfx::ErrorCode::Timeout, "Zero wait polls native completion");
    const bool zeroPollTimedOut = !polled;
    take(queue.wait(fence, 30s));
    take(queue.wait(fence, 0ms));
    for (auto timeout : {-1ms, std::chrono::milliseconds(4294967295ll), std::chrono::milliseconds::max()})
        reject(queue.wait(fence, timeout), gfx::ErrorCode::InvalidArgument, "Invalid timeout even after completion");
    check(take(queue.completedValue()) == 1, "Actual native command completion");
    take(commands->begin()); take(commands->debugMarker("reused command list")); take(commands->end());
    fence = take(queue.submit(*commands)); commands.reset(); take(queue.wait(fence, 30s));
    for (unsigned i = 0; i < 64; ++i) {
        auto list = take(device->createCommandList());
        take(list->begin()); take(list->debugMarker("retained submission")); take(list->end());
        auto next = take(queue.submit(*list));
        check(next.value == fence.value + 1 && next.device == fence.device, "Monotonic generation fence");
        fence = next; // Drop each public list before waiting; queue retains native command buffers.
    }
    take(queue.wait(fence, 30s)); take(queue.waitIdle()); take(device->checkStatus());
    check(take(queue.completedValue()) == 66, "All 66 native submissions completed");
    const auto statistics = take(device->resourceStatistics());
    check(!statistics.trackedObjects && !statistics.uniqueResources && !statistics.resourceAllocationBytes &&
        !statistics.pendingSubmissions && !statistics.pendingObjectReferences, "Completed submission retention pruned; no resources implemented");
    reject(device->createBuffer({16}), gfx::ErrorCode::Unsupported, "Buffers pending later slice");
    reject(device->createTexture({4, 4}), gfx::ErrorCode::Unsupported, "Textures pending later slice");
    reject(device->createGraphicsPipeline({}), gfx::ErrorCode::Unsupported, "Pipelines pending later slice");
    reject(device->checkSurfaceSupport({}), gfx::ErrorCode::Unsupported, "Surfaces pending later slice");
    reject(device->createSwapChain({}, {}), gfx::ErrorCode::Unsupported, "Presentation pending later slice");
    reject(device->validationErrorCount(), gfx::ErrorCode::Unsupported, "No synthesized validation zero");
    reject(device->queryMemoryBudget(gfx::MemorySegment::Local), gfx::ErrorCode::Unsupported, "No synthesized budget zero");
    reject(device->recover(), gfx::ErrorCode::Unsupported, "Recovery not advertised");
    reject(device->removeForTesting(), gfx::ErrorCode::Unsupported, "Native loss injection not advertised");
    commands = take(device->createCommandList());
    reject(commands->draw(3), gfx::ErrorCode::Unsupported, "Draw explicitly unsupported");
    reject(commands->elapsedGpuMilliseconds(), gfx::ErrorCode::Unsupported, "No synthesized GPU timing");
    bool statusRejected{}, beginRejected{}, submitRejected{}, waitRejected{}, createRejected{}, statsRejected{}, unsupportedRejected{};
    std::thread worker([&] {
        auto wrong = [](const auto& result) { return !result && result.error().code == gfx::ErrorCode::InvalidState; };
        statusRejected = wrong(device->checkStatus()); beginRejected = wrong(commands->begin());
        submitRejected = wrong(queue.submit(*commands)); waitRejected = wrong(queue.wait(fence, 0ms));
        createRejected = wrong(device->createCommandList()); statsRejected = wrong(device->resourceStatistics());
        unsupportedRejected = wrong(device->createBuffer({16}));
    }); worker.join();
    check(statusRejected && beginRejected && submitRejected && waitRejected && createRejected && statsRejected && unsupportedRejected, "Thread guards precede native/state operations");
    // Keep an in-flight native command alive after Device destruction, then reject old public children.
    take(commands->begin()); take(commands->end()); take(queue.submit(*commands));
    device.reset();
    reject(commands->begin(), gfx::ErrorCode::DeviceLost, "Retired child cannot record");
    reject(commands->debugMarker("retired"), gfx::ErrorCode::DeviceLost, "Retired child cannot encode");
    reject(commands->draw(3), gfx::ErrorCode::DeviceLost, "Retired child cannot enter unsupported operations");
    commands.reset(); // Last owner waits for retained native completion without callback/use-after-free.
    std::cout << "{\"schema_version\":1,\"contract\":\"metal_submission_v1\",\"SDK_source_fingerprint\":\"" << gfx::buildFingerprint()
        << "\",\"default_registry_id\":\"" << hex(caps.adapterId.value) << "\",\"registry_ids\":[";
    for (std::size_t i = 0; i < adapters.size(); ++i) { if (i) std::cout << ','; std::cout << '"' << hex(adapters[i].id.value) << '"'; }
    std::cout << "],\"completed_submissions\":66,\"retired_pending_submissions\":1,\"zero_poll_timed_out\":" << (zeroPollTimedOut ? "true" : "false")
        << ",\"assertions\":" << assertions << ",\"SDK_Metal_submission_executed\":true,\"resource_or_draw_executed\":false,\"native_error_injected\":false,"
        << "\"native_validation_accepted\":false,\"physical_GPU_accepted\":false,\"performance_accepted\":false}\n";
    return 0;
} catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
