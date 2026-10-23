#include <graphics/Graphics.hpp>
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
unsigned assertions{}, transfers{};
std::uint64_t verifiedBytes{};
void check(bool value, const char* text) { ++assertions; if (!value) throw std::runtime_error(text); }
template<class T> T take(gfx::Result<T> value) {
    if (!value) throw std::runtime_error(value.error().message + " native=" + std::to_string(value.error().nativeCode));
    return std::move(*value);
}
void take(gfx::Result<void> value) { if (!value) throw std::runtime_error(value.error().message); }
template<class T> void reject(gfx::Result<T> value, gfx::ErrorCode code, const char* text) {
    check(!value && value.error().code == code, text);
}
gfx::DeviceDesc description(const gfx::AdapterInfo& adapter, bool validation) {
    gfx::DeviceDesc d; d.backend = gfx::Backend::Vulkan; d.adapter = adapter.id;
    d.softwareAdapter = adapter.softwareAdapter; d.validation = validation; return d;
}
void transfer(gfx::Device& d, std::uint64_t size, unsigned seed) {
    std::vector<std::byte> expected(static_cast<std::size_t>(size));
    for (std::size_t i = 0; i < expected.size(); ++i) expected[i] = std::byte((i * 37 + seed * 19 + i / 251) & 255);
    auto upload = take(d.createBuffer({size, gfx::Memory::Upload}));
    auto gpu = take(d.createBuffer({size}));
    auto readback = take(d.createBuffer({size, gfx::Memory::Readback}));
    take(upload->write(0, expected));
    auto cmd = take(d.createCommandList()); take(cmd->begin());
    take(cmd->transition(gpu, gfx::ResourceState::CopyDestination));
    take(cmd->copyBuffer(upload, gpu, size));
    take(cmd->transition(gpu, gfx::ResourceState::CopySource));
    take(cmd->copyBuffer(gpu, readback, size)); take(cmd->end());
    auto fence = take(d.queue().submit(*cmd));
    // Only native submission retention may own these allocations after this point.
    std::weak_ptr<gfx::Buffer> uploadWrapper = upload, gpuWrapper = gpu;
    upload.reset(); gpu.reset(); cmd.reset();
    check(uploadWrapper.expired() && gpuWrapper.expired(), "Submission retains native resources, not wrappers");
    auto poll = d.queue().wait(fence, std::chrono::milliseconds(0));
    check(poll || poll.error().code == gfx::ErrorCode::Timeout, "Zero wait polls or times out");
    take(d.queue().wait(fence));
    check(take(readback->read(0, size)) == expected, "Every transferred byte must match");
    if (size > 4) check(take(readback->read(1, 3)) == std::vector<std::byte>(expected.begin() + 1, expected.begin() + 4), "Unaligned host read slice");
    reject(d.queue().wait(fence, std::chrono::milliseconds(-1)), gfx::ErrorCode::InvalidArgument, "Validate negative timeout after completion");
    reject(d.queue().wait(fence, std::chrono::milliseconds(4294967295ll)), gfx::ErrorCode::InvalidArgument, "Reject infinite sentinel after completion");
    take(d.queue().wait(fence, std::chrono::milliseconds(4294967294ll)));
    check(take(d.queue().completedValue()) >= fence.value, "Monotonic completion");
    readback.reset();
    const auto stats = take(d.resourceStatistics());
    check(!stats.trackedObjects && !stats.uniqueResources && !stats.resourceAllocationBytes && !stats.pendingSubmissions && !stats.pendingObjectReferences, "All native resources reclaimed");
    ++transfers; verifiedBytes += size;
}
void contracts(gfx::Device& d, const gfx::AdapterInfo& adapter, bool validation) {
    const auto& caps = d.caps();
    check(caps.backend == gfx::Backend::Vulkan && caps.adapterId == adapter.id && caps.adapterName == adapter.name, "Exact Vulkan selection");
    check(!caps.compute && !caps.timestampQueries && !caps.swapChain && caps.resourceBinding && caps.vertexInput && caps.indexedDrawing &&
        caps.bindingLayouts && caps.depthAttachment && caps.multipleRenderTargets && !caps.deviceRecovery && !caps.deviceLossInjection, "Partial capabilities are honest");
    reject(d.createBuffer({0}), gfx::ErrorCode::InvalidArgument, "Empty buffer");
    reject(d.createBuffer({64, static_cast<gfx::Memory>(99)}), gfx::ErrorCode::InvalidArgument, "Unknown memory kind");
    reject(d.createTexture({1, 1, gfx::Format::D32Float, true, true}), gfx::ErrorCode::InvalidArgument, "Depth cannot also be a color target");
    reject(d.createGraphicsPipeline({}), gfx::ErrorCode::Unsupported, "Pipeline not implemented");
    reject(d.queryMemoryBudget(gfx::MemorySegment::Local), gfx::ErrorCode::Unsupported, "No synthesized OS budget");
    reject(d.recover(), gfx::ErrorCode::Unsupported, "Recovery not implemented");
    reject(d.removeForTesting(), gfx::ErrorCode::Unsupported, "Loss injection not implemented");
    auto other = take(gfx::createDevice(description(adapter, validation)));
    auto up = take(d.createBuffer({257, gfx::Memory::Upload}));
    auto gpu = take(d.createBuffer({257}));
    auto read = take(d.createBuffer({257, gfx::Memory::Readback}));
    auto foreign = take(other->createBuffer({257, gfx::Memory::Readback}));
    std::vector<std::byte> data(257, std::byte{0x53}); take(up->write(0, data));
    const std::array one{std::byte{0x5a}};
    take(up->write(256, one)); data.back() = one[0];
    reject(up->write(UINT64_MAX, one), gfx::ErrorCode::InvalidArgument, "Overflowing host write");
    reject(up->write(257, one), gfx::ErrorCode::InvalidArgument, "Out of range host write");
    reject(gpu->write(0, one), gfx::ErrorCode::InvalidArgument, "Device memory cannot be mapped by write");
    reject(up->read(0, 1), gfx::ErrorCode::InvalidArgument, "Upload memory cannot be read");
    reject(read->read(UINT64_MAX, 1), gfx::ErrorCode::InvalidArgument, "Overflowing read range");
    auto cmd = take(d.createCommandList());
    reject(cmd->end(), gfx::ErrorCode::InvalidState, "End before begin");
    take(cmd->begin());
    reject(cmd->begin(), gfx::ErrorCode::InvalidState, "Nested begin");
    const auto beforeRejections = take(d.resourceStatistics());
    for (unsigned i = 0; i < 32; ++i) {
        auto temporary = take(d.createBuffer({257}));
        std::weak_ptr<gfx::Buffer> weak = temporary;
        reject(cmd->copyBuffer(up, temporary, 257), gfx::ErrorCode::InvalidState, "Rejected copy does not retain state");
        reject(cmd->transition(temporary, gfx::ResourceState::RenderTarget), gfx::ErrorCode::Unsupported, "Unsupported state preserves recording");
        temporary.reset(); check(weak.expired(), "Rejected resources reclaimed");
        const auto afterRejection = take(d.resourceStatistics());
        check(afterRejection.trackedObjects == beforeRejections.trackedObjects &&
            afterRejection.uniqueResources == beforeRejections.uniqueResources &&
            afterRejection.resourceAllocationBytes == beforeRejections.resourceAllocationBytes &&
            afterRejection.pendingSubmissions == beforeRejections.pendingSubmissions,
            "Rejected commands retain no native allocations");
    }
    // A rejected copy must not observe the upload version either.
    take(up->write(0, data));
    reject(cmd->transition(foreign, gfx::ResourceState::CopyDestination), gfx::ErrorCode::WrongDevice, "Foreign resource");
    reject(cmd->transition(up, gfx::ResourceState::CopyDestination), gfx::ErrorCode::InvalidArgument, "Upload state fixed");
    reject(cmd->transition(read, gfx::ResourceState::CopySource), gfx::ErrorCode::InvalidArgument, "Readback state fixed");
    reject(cmd->copyBuffer({}, read, 1), gfx::ErrorCode::InvalidArgument, "Null source");
    reject(cmd->copyBuffer(up, read, 0), gfx::ErrorCode::InvalidArgument, "Empty copy");
    reject(cmd->copyBuffer(up, read, 258), gfx::ErrorCode::InvalidArgument, "Copy bounds");
    reject(cmd->copyBuffer(up, up, 1), gfx::ErrorCode::InvalidArgument, "Copy alias");
    reject(cmd->draw(3), gfx::ErrorCode::InvalidState, "Draw outside pass preserves copy recording");
    take(cmd->transition(gpu, gfx::ResourceState::CopyDestination)); take(cmd->copyBuffer(up, gpu, 257));
    take(cmd->transition(gpu, gfx::ResourceState::CopySource)); take(cmd->copyBuffer(gpu, read, 257)); take(cmd->end());
    reject(other->queue().submit(*cmd), gfx::ErrorCode::WrongDevice, "Wrong queue");
    auto fence = take(d.queue().submit(*cmd));
    reject(other->queue().wait(fence), gfx::ErrorCode::WrongDevice, "Foreign fence");
    take(d.queue().wait(fence));
    check(take(read->read(0, 257)) == data, "Rejections leave accepted work correct");
    reject(d.queue().submit(*cmd), gfx::ErrorCode::InvalidState, "One native recording submits once");
    reject(d.queue().wait({fence.value + 1, fence.device}), gfx::ErrorCode::InvalidArgument, "Unsubmitted fence");
    reject(d.queue().wait({0, fence.device}), gfx::ErrorCode::InvalidArgument, "Zero fence");
    // Rerecording releases the old native recording, and CPU writes invalidate snapshots.
    take(cmd->begin()); take(cmd->copyBuffer(up, read, 257)); take(cmd->end());
    take(up->write(0, data)); reject(d.queue().submit(*cmd), gfx::ErrorCode::InvalidState, "CPU write invalidates recorded version");
    take(cmd->begin()); take(cmd->copyBuffer(up, read, 257)); take(cmd->end());
    auto sibling = take(d.createCommandList()); take(sibling->begin()); take(sibling->copyBuffer(up, read, 257)); take(sibling->end());
    fence = take(d.queue().submit(*sibling));
    reject(d.queue().submit(*cmd), gfx::ErrorCode::InvalidState, "Prior submission invalidates sibling recording");
    sibling.reset(); take(d.queue().wait(fence));
    check(take(read->read(0, 257)) == data, "Sibling transfer bytes");
    bool threadRejected{};
    std::thread worker([&] { auto result = up->write(0, one); threadRejected = !result && result.error().code == gfx::ErrorCode::InvalidState; }); worker.join();
    check(threadRejected, "Creating-thread contract");
    cmd.reset(); up.reset(); gpu.reset(); read.reset(); foreign.reset();
    take(d.queue().waitIdle()); auto stats = take(d.resourceStatistics());
    check(!stats.trackedObjects && !stats.uniqueResources && !stats.pendingSubmissions, "Contract resources reclaimed");
    check(take(d.validationErrorCount()) == 0, "No Vulkan validation errors observed");
    // Accepted recording ownership is separate from public wrapper ownership.
    auto retainedUp = take(d.createBuffer({4096, gfx::Memory::Upload}));
    auto retainedRead = take(d.createBuffer({4096, gfx::Memory::Readback}));
    auto retained = take(d.createCommandList()); take(retained->begin());
    take(retained->copyBuffer(retainedUp, retainedRead, 4096)); take(retained->end());
    retainedUp.reset(); retainedRead.reset();
    stats = take(d.resourceStatistics());
    check(stats.trackedObjects == 1 && stats.uniqueResources == 2 && stats.resourceAllocationBytes >= 8192,
        "Ended recording retains allocations after public wrappers disappear");
    retained.reset(); stats = take(d.resourceStatistics());
    check(!stats.trackedObjects && !stats.uniqueResources && !stats.resourceAllocationBytes,
        "Unsubmitted recording destruction releases allocations");
}
void retirement(const gfx::AdapterInfo& adapter, bool validation) {
    auto d = take(gfx::createDevice(description(adapter, validation)));
    auto up = take(d->createBuffer({4096, gfx::Memory::Upload}));
    auto read = take(d->createBuffer({4096, gfx::Memory::Readback}));
    auto cmd = take(d->createCommandList()); take(cmd->begin()); take(cmd->copyBuffer(up, read, 4096)); take(cmd->end());
    take(d->queue().submit(*cmd)); d.reset();
    check(up->description().size == 4096 && read->description().size == 4096, "Retired children preserve descriptions");
    reject(up->write(0, {}), gfx::ErrorCode::DeviceLost, "Retired upload inert");
    reject(read->read(0, 1), gfx::ErrorCode::DeviceLost, "Retired readback inert");
    reject(cmd->begin(), gfx::ErrorCode::DeviceLost, "Retired command inert");
}
}
int main(int argc, char** argv) try {
    if (argc != 1 && !(argc == 2 && std::string_view(argv[1]) == "--validation"))
        throw std::runtime_error("Usage: gfx_vulkan_tests [--validation]");
    const bool validation = argc == 2 && std::string_view(argv[1]) == "--validation";
    check(gfx::backendCompiled(gfx::Backend::Vulkan), "Vulkan compiled");
    std::cout << "source=" << gfx::buildFingerprint() << " validation=" << validation << std::endl;
    const auto adapters = take(gfx::enumerateAdapters(gfx::Backend::Vulkan));
    check(!adapters.empty(), "At least one real Vulkan adapter required");
    const auto again = take(gfx::enumerateAdapters(gfx::Backend::Vulkan));
    check(again.size() == adapters.size(), "Fresh enumeration count stable");
    for (const auto& adapter : adapters) {
        check(std::any_of(again.begin(), again.end(), [&](auto& p) { return p.id == adapter.id && p.name == adapter.name; }), "UUID identity stable across instances");
        std::cout << "adapter=" << adapter.name << " id=" << adapter.id.value << " vendor=" << adapter.vendorId << " software=" << adapter.softwareAdapter << std::endl;
        auto d = take(gfx::createDevice(description(adapter, validation)));
        contracts(*d, adapter, validation);
        for (auto size : {1ull, 3ull, 4ull, 257ull, 4097ull, 65539ull, 1048579ull, 8388611ull})
            for (unsigned seed = 0; seed < 3; ++seed) transfer(*d, size, seed);
        check(take(d->validationErrorCount()) == 0, "Transfer validation errors");
        retirement(adapter, validation);
        auto mismatch = description(adapter, validation); mismatch.softwareAdapter = !mismatch.softwareAdapter;
        reject(gfx::createDevice(mismatch), gfx::ErrorCode::InvalidArgument, "Exact software-kind mismatch");
    }
    auto missing = description(adapters.front(), validation);
    while (std::any_of(adapters.begin(), adapters.end(), [&](auto& p) { return p.id == missing.adapter; })) ++missing.adapter->value;
    reject(gfx::createDevice(missing), gfx::ErrorCode::Unsupported, "Missing exact adapter never falls back");
    missing.adapter.reset(); missing.enableDeviceLossTesting = true;
    reject(gfx::createDevice(missing), gfx::ErrorCode::Unsupported, "Unsupported loss opt-in");
    std::cout << "PASS assertions=" << assertions << " transfers=" << transfers << " verified_bytes=" << verifiedBytes << " adapters=" << adapters.size() << std::endl;
    return 0;
} catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << std::endl; return 1; }
