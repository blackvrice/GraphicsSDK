#include <graphics/Graphics.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
unsigned assertions{};
unsigned regionCopies{};
unsigned rejections{};
std::uint64_t verifiedBytes{};

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

std::vector<std::byte> makePattern(std::size_t size, unsigned seed) {
    std::vector<std::byte> bytes(size);
    for (std::size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<std::byte>((i * 41u + seed * 17u + (i / 251u) * 7u + 3u) & 0xffu);
    }
    return bytes;
}

void verifyReclaimed(gfx::Device& device) {
    take(device.queue().waitIdle());
    const auto stats = take(device.resourceStatistics());
    check(stats.trackedObjects == 0 &&
          stats.uniqueResources == 0 &&
          stats.resourceAllocationBytes == 0 &&
          stats.pendingSubmissions == 0 &&
          stats.pendingObjectReferences == 0,
          "All native resources and pending submissions must be reclaimed");
}

struct SubrangeCase {
    std::uint64_t sourceSize;
    std::uint64_t destinationSize;
    std::uint64_t sourceOffset;
    std::uint64_t destinationOffset;
    std::uint64_t copyBytes;
    unsigned seed;
};

void runSubrangeCase(gfx::Device& device, const SubrangeCase& testCase) {
    const auto sourceData = makePattern(static_cast<std::size_t>(testCase.sourceSize), testCase.seed);
    std::vector<std::byte> deviceSentinel(static_cast<std::size_t>(testCase.destinationSize), std::byte{0xa5});
    std::vector<std::byte> readbackSentinel(static_cast<std::size_t>(testCase.destinationSize), std::byte{0x5c});

    auto uploadSource = take(device.createBuffer({testCase.sourceSize, gfx::Memory::Upload}));
    auto uploadInit = take(device.createBuffer({testCase.destinationSize, gfx::Memory::Upload}));
    auto uploadReadbackInit = take(device.createBuffer({testCase.destinationSize, gfx::Memory::Upload}));
    auto deviceBuffer = take(device.createBuffer({testCase.destinationSize, gfx::Memory::Device}));
    auto readbackBuffer = take(device.createBuffer({testCase.destinationSize, gfx::Memory::Readback}));

    take(uploadSource->write(0, sourceData));
    take(uploadInit->write(0, deviceSentinel));
    take(uploadReadbackInit->write(0, readbackSentinel));

    auto commands = take(device.createCommandList());
    take(commands->begin());
    take(commands->transition(deviceBuffer, gfx::ResourceState::CopyDestination));
    // Fill destination and readback with known sentinels so any out-of-range write is detected.
    take(commands->copyBuffer(uploadInit, deviceBuffer, testCase.destinationSize));
    take(commands->copyBuffer(uploadReadbackInit, readbackBuffer, testCase.destinationSize));
    take(commands->copyBufferRegion(
        uploadSource, testCase.sourceOffset,
        deviceBuffer, testCase.destinationOffset,
        testCase.copyBytes));
    take(commands->transition(deviceBuffer, gfx::ResourceState::CopySource));
    take(commands->copyBuffer(deviceBuffer, readbackBuffer, testCase.destinationSize));
    take(commands->end());

    const auto fence = take(device.queue().submit(*commands));
    std::weak_ptr<gfx::Buffer> weakUpload = uploadSource;
    std::weak_ptr<gfx::Buffer> weakDevice = deviceBuffer;
    uploadSource.reset();
    uploadInit.reset();
    uploadReadbackInit.reset();
    deviceBuffer.reset();
    commands.reset();
    check(weakUpload.expired() && weakDevice.expired(),
          "Submitted copyBufferRegion must retain native allocations, not public wrappers");

    take(device.queue().wait(fence));
    const auto actual = take(readbackBuffer->read(0, testCase.destinationSize));

    std::vector<std::byte> expected = deviceSentinel;
    std::copy_n(
        sourceData.begin() + static_cast<std::ptrdiff_t>(testCase.sourceOffset),
        static_cast<std::ptrdiff_t>(testCase.copyBytes),
        expected.begin() + static_cast<std::ptrdiff_t>(testCase.destinationOffset));

    check(actual == expected, "Sub-range bytes and all surrounding sentinel guard bytes must match");
    ++regionCopies;
    verifiedBytes += testCase.destinationSize;

    readbackBuffer.reset();
    verifyReclaimed(device);
}

void runPackedMultiSliceCase(gfx::Device& device) {
    struct Slice {
        std::uint64_t uploadOffset;
        std::uint64_t deviceOffset;
        std::uint64_t readbackOffset;
        std::uint64_t bytes;
    };

    // Simulate an upload ring packing aligned and unaligned slices into one Upload buffer,
    // scattering them into a Device buffer, and gathering them into distinct Readback offsets.
    constexpr std::array<Slice, 8> slices{{
        {0,     17,    3,     1},
        {5,     64,    19,    7},
        {16,    128,   64,    16},
        {40,    259,   131,   63},
        {128,   512,   256,   256},
        {401,   1027,  600,   513},
        {1024,  2048,  1536,  1024},
        {2103,  4099,  3077,  4099},
    }};
    constexpr std::uint64_t uploadCapacity = 8192;
    constexpr std::uint64_t deviceCapacity = 10240;
    constexpr std::uint64_t readbackCapacity = 10240;

    const auto uploadBytes = makePattern(uploadCapacity, 73);
    const std::vector<std::byte> deviceGuard(deviceCapacity, std::byte{0xd1});
    const std::vector<std::byte> readbackGuard(readbackCapacity, std::byte{0x6b});

    auto upload = take(device.createBuffer({uploadCapacity, gfx::Memory::Upload}));
    auto initDevice = take(device.createBuffer({deviceCapacity, gfx::Memory::Upload}));
    auto initReadback = take(device.createBuffer({readbackCapacity, gfx::Memory::Upload}));
    auto gpuBuffer = take(device.createBuffer({deviceCapacity, gfx::Memory::Device}));
    auto readback = take(device.createBuffer({readbackCapacity, gfx::Memory::Readback}));

    take(upload->write(0, uploadBytes));
    take(initDevice->write(0, deviceGuard));
    take(initReadback->write(0, readbackGuard));

    auto commands = take(device.createCommandList());
    take(commands->begin());
    take(commands->transition(gpuBuffer, gfx::ResourceState::CopyDestination));
    take(commands->copyBuffer(initDevice, gpuBuffer, deviceCapacity));
    take(commands->copyBuffer(initReadback, readback, readbackCapacity));
    for (const auto& slice : slices) {
        take(commands->copyBufferRegion(
            upload, slice.uploadOffset,
            gpuBuffer, slice.deviceOffset,
            slice.bytes));
    }
    take(commands->transition(gpuBuffer, gfx::ResourceState::CopySource));
    for (const auto& slice : slices) {
        take(commands->copyBufferRegion(
            gpuBuffer, slice.deviceOffset,
            readback, slice.readbackOffset,
            slice.bytes));
    }
    take(commands->end());

    const auto fence = take(device.queue().submit(*commands));
    upload.reset();
    initDevice.reset();
    initReadback.reset();
    gpuBuffer.reset();
    commands.reset();
    take(device.queue().wait(fence));

    std::vector<std::byte> expectedReadback = readbackGuard;
    for (const auto& slice : slices) {
        std::copy_n(
            uploadBytes.begin() + static_cast<std::ptrdiff_t>(slice.uploadOffset),
            static_cast<std::ptrdiff_t>(slice.bytes),
            expectedReadback.begin() + static_cast<std::ptrdiff_t>(slice.readbackOffset));
    }

    const auto actualReadback = take(readback->read(0, readbackCapacity));
    check(actualReadback == expectedReadback,
          "Packed multi-slice upload/device/readback copies and guard gaps must match");
    regionCopies += static_cast<unsigned>(slices.size() * 2);
    verifiedBytes += readbackCapacity;

    readback.reset();
    verifyReclaimed(device);
}

void runContractRejections(gfx::Device& device, const gfx::DeviceDesc& desc) {
    auto otherDevice = take(gfx::createDevice(desc));
    auto upload = take(device.createBuffer({256, gfx::Memory::Upload}));
    auto gpuBuffer = take(device.createBuffer({256, gfx::Memory::Device}));
    auto readback = take(device.createBuffer({256, gfx::Memory::Readback}));
    auto foreignReadback = take(otherDevice->createBuffer({256, gfx::Memory::Readback}));
    auto renderTarget = take(device.createTexture({16, 16, gfx::Format::RGBA8UNorm, true}));

    const auto payload = makePattern(256, 11);
    take(upload->write(0, payload));

    auto commands = take(device.createCommandList());
    expectError(commands->copyBufferRegion(upload, 0, readback, 0, 16),
                gfx::ErrorCode::InvalidState,
                "copyBufferRegion outside recording must fail");

    take(commands->begin());
    const auto nullCode = desc.backend == gfx::Backend::D3D12
                              ? gfx::ErrorCode::WrongDevice
                              : gfx::ErrorCode::InvalidArgument;
    expectError(commands->copyBufferRegion({}, 0, readback, 0, 16), nullCode, "Reject null source");
    expectError(commands->copyBufferRegion(upload, 0, {}, 0, 16), nullCode, "Reject null destination");
    expectError(commands->copyBufferRegion(upload, 0, foreignReadback, 0, 16),
                gfx::ErrorCode::WrongDevice,
                "Reject foreign destination buffer");

    expectError(commands->copyBufferRegion(upload, 0, readback, 0, 0),
                gfx::ErrorCode::InvalidArgument,
                "Reject zero-byte region copy");
    expectError(commands->copyBufferRegion(upload, 256, readback, 0, 1),
                gfx::ErrorCode::InvalidArgument,
                "Reject sourceOffset at buffer end");
    expectError(commands->copyBufferRegion(upload, 257, readback, 0, 1),
                gfx::ErrorCode::InvalidArgument,
                "Reject sourceOffset past buffer end");
    expectError(commands->copyBufferRegion(upload, 200, readback, 0, 57),
                gfx::ErrorCode::InvalidArgument,
                "Reject source range exceeding buffer size");
    expectError(commands->copyBufferRegion(upload, 0, readback, 256, 1),
                gfx::ErrorCode::InvalidArgument,
                "Reject destinationOffset at buffer end");
    expectError(commands->copyBufferRegion(upload, 0, readback, 200, 57),
                gfx::ErrorCode::InvalidArgument,
                "Reject destination range exceeding buffer size");
    expectError(commands->copyBufferRegion(upload, std::numeric_limits<std::uint64_t>::max() - 3, readback, 0, 8),
                gfx::ErrorCode::InvalidArgument,
                "Reject overflowing sourceOffset + bytes");
    expectError(commands->copyBufferRegion(upload, 0, readback, std::numeric_limits<std::uint64_t>::max() - 3, 8),
                gfx::ErrorCode::InvalidArgument,
                "Reject overflowing destinationOffset + bytes");
    expectError(commands->copyBufferRegion(upload, 0, upload, 64, 16),
                gfx::ErrorCode::InvalidArgument,
                "Reject same-buffer alias copy even for disjoint offsets");
    expectError(commands->copyBufferRegion(readback, 0, gpuBuffer, 0, 16),
                gfx::ErrorCode::InvalidArgument,
                "Reject Readback buffer as copy source");
    expectError(commands->copyBufferRegion(gpuBuffer, 0, upload, 0, 16),
                gfx::ErrorCode::InvalidArgument,
                "Reject Upload buffer as copy destination");

    // Verify rejection atomicity: a rejected copy must not retain the temporary buffer or observe Upload version.
    const auto statsBefore = take(device.resourceStatistics());
    {
        auto temporary = take(device.createBuffer({256, gfx::Memory::Device}));
        std::weak_ptr<gfx::Buffer> weakTemporary = temporary;
        expectError(commands->copyBufferRegion(upload, 16, temporary, 32, 64),
                    gfx::ErrorCode::InvalidState,
                    "Reject destination not in CopyDestination state");
        expectError(commands->copyBufferRegion(temporary, 16, readback, 32, 64),
                    gfx::ErrorCode::InvalidState,
                    "Reject source not in CopySource state");
        temporary.reset();
        check(weakTemporary.expired(), "Rejected temporary buffer must not be retained by wrapper");
        const auto statsAfter = take(device.resourceStatistics());
        check(statsAfter.trackedObjects == statsBefore.trackedObjects &&
              statsAfter.uniqueResources == statsBefore.uniqueResources &&
              statsAfter.resourceAllocationBytes == statsBefore.resourceAllocationBytes,
              "Rejected copyBufferRegion must not retain native allocations");
    }

    // Mutating Upload after only rejected copies must not invalidate the still-empty recording.
    take(upload->write(0, payload));

    // Reject copyBufferRegion inside an active render pass.
    take(commands->transition(renderTarget, gfx::ResourceState::RenderTarget));
    take(commands->beginRenderPass(renderTarget, {0.0f, 0.0f, 0.0f, 1.0f}));
    expectError(commands->copyBufferRegion(upload, 0, readback, 0, 16),
                gfx::ErrorCode::InvalidState,
                "Reject copyBufferRegion inside an active render pass");
    take(commands->endRenderPass());

    // Now record a valid sub-range copy and confirm a post-recording Upload write invalidates submit.
    take(commands->copyBufferRegion(upload, 32, readback, 64, 48));
    take(commands->end());
    take(upload->write(0, payload));
    expectError(device.queue().submit(*commands),
                gfx::ErrorCode::InvalidState,
                "Upload write after accepted copyBufferRegion must invalidate submission");

    commands.reset();
    renderTarget.reset();
    foreignReadback.reset();
    otherDevice.reset();
    upload.reset();
    gpuBuffer.reset();
    readback.reset();
    verifyReclaimed(device);
}
} // namespace

int main(int argc, char** argv) try {
    auto backend = gfx::Backend::Vulkan;
    bool validation = false;
    bool warp = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--d3d12") == 0) {
            backend = gfx::Backend::D3D12;
        } else if (std::strcmp(argv[i], "--validation") == 0) {
            validation = true;
        } else if (std::strcmp(argv[i], "--warp") == 0) {
            warp = true;
        } else {
            throw std::runtime_error("Usage: gfx_buffer_region_tests [--d3d12] [--validation] [--warp]");
        }
    }

    check(gfx::backendCompiled(backend), "Requested backend must be compiled");
    const auto adapters = take(gfx::enumerateAdapters(backend));
    if (backend == gfx::Backend::D3D12 && !warp &&
        !std::any_of(adapters.begin(), adapters.end(), [](const auto& info) { return !info.softwareAdapter; })) {
        std::cout << "SKIP no hardware D3D12 adapter\n";
        return 77;
    }

    constexpr std::array<SubrangeCase, 8> subrangeCases{{
        {64,    64,    3,    5,    1,     1},
        {64,    64,    1,    7,    3,     2},
        {1024,  1024,  256,  512,  256,   3},
        {1024,  1024,  13,   256,  129,   4},
        {1024,  1024,  256,  19,   131,   5},
        {512,   1024,  255,  767,  257,   6},
        {81920, 81920, 137,  4099, 65539, 7},
        {257,   257,   0,    0,    257,   8},
    }};

    unsigned testedAdapters = 0;
    for (const auto& adapter : adapters) {
        if (backend == gfx::Backend::D3D12 && adapter.softwareAdapter != warp) {
            continue;
        }
        gfx::DeviceDesc desc;
        desc.backend = backend;
        desc.validation = validation;
        desc.softwareAdapter = adapter.softwareAdapter;
        desc.adapter = adapter.id;

        auto device = take(gfx::createDevice(desc));
        for (const auto& testCase : subrangeCases) {
            runSubrangeCase(*device, testCase);
        }
        runPackedMultiSliceCase(*device);
        runContractRejections(*device, desc);

        if (validation) {
            check(take(device->validationErrorCount()) == 0,
                  "Validation error count must remain zero");
        }
        ++testedAdapters;
    }

    check(testedAdapters > 0, "At least one real adapter must execute");
    std::cout << "{\"event\":\"buffer_region\""
              << ",\"backend\":" << std::quoted(backend == gfx::Backend::D3D12 ? "d3d12" : "vulkan")
              << ",\"source\":" << std::quoted(gfx::buildFingerprint())
              << ",\"adapters\":" << testedAdapters
              << ",\"validation\":" << (validation ? "true" : "false")
              << ",\"assertions\":" << assertions
              << ",\"region_copies\":" << regionCopies
              << ",\"rejections\":" << rejections
              << ",\"verified_bytes\":" << verifiedBytes
              << ",\"errors\":0}\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
