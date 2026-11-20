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
unsigned regionUploads{};
unsigned regionDownloads{};
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

std::uint32_t texelBytes(gfx::Format format) {
    return format == gfx::Format::RGBA16Float ? 8u : 4u;
}

void fillTexel(std::byte* destination, gfx::Format format, std::uint32_t x, std::uint32_t y, unsigned seed) {
    if (format == gfx::Format::D32Float) {
        const float depth = static_cast<float>((x * 13u + y * 29u + seed * 7u) % 997u) / 1000.0f;
        std::memcpy(destination, &depth, sizeof(depth));
        return;
    }
    const std::uint32_t count = texelBytes(format);
    for (std::uint32_t c = 0; c < count; ++c) {
        destination[c] = static_cast<std::byte>((x * 31u + y * 53u + seed * 19u + c * 11u + 5u) & 0xffu);
    }
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

void runColorPartialUploadAndDownload(gfx::Device& device, gfx::Format format, unsigned seed) {
    constexpr std::uint32_t texWidth = 67;
    constexpr std::uint32_t texHeight = 43;
    const std::uint32_t bpp = texelBytes(format);

    auto texture = take(device.createTexture({texWidth, texHeight, format}));
    const auto fullFootprint = texture->footprint();

    // CPU reference image storing tightly packed rows [y * texWidth * bpp + x * bpp].
    std::vector<std::byte> reference(static_cast<std::size_t>(texWidth) * texHeight * bpp);
    std::vector<std::byte> baseUploadBytes(static_cast<std::size_t>(fullFootprint.size), std::byte{0xee});
    for (std::uint32_t y = 0; y < texHeight; ++y) {
        for (std::uint32_t x = 0; x < texWidth; ++x) {
            fillTexel(reference.data() + (static_cast<std::size_t>(y) * texWidth + x) * bpp, format, x, y, seed);
            std::memcpy(
                baseUploadBytes.data() + static_cast<std::size_t>(y) * fullFootprint.rowPitch + static_cast<std::size_t>(x) * bpp,
                reference.data() + (static_cast<std::size_t>(y) * texWidth + x) * bpp,
                bpp);
        }
    }

    // Three disjoint sub-region patches with non-zero 512-byte-aligned bufferOffset and default/custom rowPitch.
    struct PatchSpec {
        std::uint32_t x, y, width, height;
        std::uint32_t customPitch;
        unsigned patchSeed;
    };
    const std::array<PatchSpec, 3> patches{{
        {0,  0,  1,  1,  0,    seed + 10},
        {7,  5,  19, 13, 0,    seed + 20},
        {50, 31, 17, 12, 512,  seed + 30},
    }};

    // Pack all three patches into one Upload buffer at 512-byte aligned offsets with sentinel gaps.
    std::vector<gfx::TextureCopyRegion> regions;
    std::uint64_t nextOffset = 512;
    for (const auto& spec : patches) {
        const auto fp = take(gfx::computeTextureRegionFootprint(format, spec.width, spec.height, spec.customPitch));
        regions.push_back({nextOffset, spec.customPitch, spec.x, spec.y, spec.width, spec.height});
        nextOffset = ((nextOffset + fp.size + 511u) & ~std::uint64_t{511}) + 512u;
    }
    const std::uint64_t patchBufferSize = nextOffset;
    std::vector<std::byte> patchBufferBytes(static_cast<std::size_t>(patchBufferSize), std::byte{0x7a});

    for (std::size_t idx = 0; idx < patches.size(); ++idx) {
        const auto& spec = patches[idx];
        const auto& region = regions[idx];
        const auto fp = take(gfx::computeTextureRegionFootprint(format, spec.width, spec.height, spec.customPitch));
        for (std::uint32_t py = 0; py < spec.height; ++py) {
            for (std::uint32_t px = 0; px < spec.width; ++px) {
                std::byte texel[8]{};
                fillTexel(texel, format, spec.x + px, spec.y + py, spec.patchSeed);
                std::memcpy(
                    patchBufferBytes.data() + region.bufferOffset + static_cast<std::size_t>(py) * fp.rowPitch + static_cast<std::size_t>(px) * bpp,
                    texel,
                    bpp);
                std::memcpy(
                    reference.data() + (static_cast<std::size_t>(spec.y + py) * texWidth + (spec.x + px)) * bpp,
                    texel,
                    bpp);
            }
        }
    }

    auto baseUpload = take(device.createBuffer({fullFootprint.size, gfx::Memory::Upload}));
    auto patchUpload = take(device.createBuffer({patchBufferSize, gfx::Memory::Upload}));
    auto fullReadback = take(device.createBuffer({fullFootprint.size, gfx::Memory::Readback}));
    take(baseUpload->write(0, baseUploadBytes));
    take(patchUpload->write(0, patchBufferBytes));

    // Prepare a single Readback buffer initialized with sentinel 0x6d to verify partial downloads preserve all guard bytes.
    const gfx::TextureCopyRegion downloadA{512, 0, 5, 3, 23, 17};
    const auto downloadFpA = take(gfx::computeTextureRegionFootprint(format, downloadA.width, downloadA.height, downloadA.bufferRowPitch));
    const std::uint64_t downloadOffsetB = ((512 + downloadFpA.size + 511u) & ~std::uint64_t{511}) + 512u;
    const gfx::TextureCopyRegion downloadB{downloadOffsetB, 512, 48, 29, 19, 14};
    const auto downloadFpB = take(gfx::computeTextureRegionFootprint(format, downloadB.width, downloadB.height, downloadB.bufferRowPitch));
    const std::uint64_t partialReadbackSize = downloadOffsetB + downloadFpB.size + 512u;

    const std::vector<std::byte> readbackSentinel(static_cast<std::size_t>(partialReadbackSize), std::byte{0x6d});
    auto readbackInit = take(device.createBuffer({partialReadbackSize, gfx::Memory::Upload}));
    auto partialReadback = take(device.createBuffer({partialReadbackSize, gfx::Memory::Readback}));
    take(readbackInit->write(0, readbackSentinel));

    auto commands = take(device.createCommandList());
    take(commands->begin());
    take(commands->copyBuffer(readbackInit, partialReadback, partialReadbackSize));
    take(commands->transition(texture, gfx::ResourceState::CopyDestination));
    take(commands->copyBufferToTexture(baseUpload, texture));
    for (const auto& region : regions) {
        take(commands->copyBufferToTextureRegion(patchUpload, texture, region));
    }
    take(commands->transition(texture, gfx::ResourceState::CopySource));
    take(commands->copyTextureToBuffer(texture, fullReadback));
    take(commands->copyTextureToBufferRegion(texture, partialReadback, downloadA));
    take(commands->copyTextureToBufferRegion(texture, partialReadback, downloadB));
    take(commands->end());

    const auto fence = take(device.queue().submit(*commands));
    std::weak_ptr<gfx::Texture> weakTexture = texture;
    std::weak_ptr<gfx::Buffer> weakPatchUpload = patchUpload;
    baseUpload.reset();
    patchUpload.reset();
    readbackInit.reset();
    texture.reset();
    commands.reset();
    check(weakTexture.expired() && weakPatchUpload.expired(),
          "Submitted partial texture copies must retain native allocations, not public wrappers");

    take(device.queue().wait(fence));

    // 1. Verify EVERY texel of the full texture (both patched and untouched surrounding texels).
    const auto actualFull = take(fullReadback->read(0, fullFootprint.size));
    for (std::uint32_t y = 0; y < texHeight; ++y) {
        const auto* expectedRow = reference.data() + static_cast<std::size_t>(y) * texWidth * bpp;
        const auto* actualRow = actualFull.data() + static_cast<std::size_t>(y) * fullFootprint.rowPitch;
        check(std::memcmp(actualRow, expectedRow, static_cast<std::size_t>(texWidth) * bpp) == 0,
              "Full texture after partial uploads must match patched and untouched texels");
    }
    verifiedBytes += static_cast<std::uint64_t>(texWidth) * texHeight * bpp;
    regionUploads += static_cast<unsigned>(regions.size());

    // 2. Verify both partial downloads AND every untouched guard byte / row padding byte.
    const auto actualPartial = take(partialReadback->read(0, partialReadbackSize));
    std::vector<std::byte> expectedPartial = readbackSentinel;
    auto paintExpectedDownload = [&](const gfx::TextureCopyRegion& region, const gfx::TextureFootprint& fp) {
        for (std::uint32_t ry = 0; ry < region.height; ++ry) {
            std::memcpy(
                expectedPartial.data() + region.bufferOffset + static_cast<std::size_t>(ry) * fp.rowPitch,
                reference.data() + (static_cast<std::size_t>(region.y + ry) * texWidth + region.x) * bpp,
                fp.rowBytes);
        }
    };
    paintExpectedDownload(downloadA, downloadFpA);
    paintExpectedDownload(downloadB, downloadFpB);

    check(actualPartial == expectedPartial,
          "Partial texture downloads and every surrounding/padding sentinel byte must match");
    verifiedBytes += partialReadbackSize;
    regionDownloads += 2;

    fullReadback.reset();
    partialReadback.reset();
    verifyReclaimed(device);
}

void runDepthFullExtentAndPartialRejection(gfx::Device& device) {
    constexpr std::uint32_t width = 19;
    constexpr std::uint32_t height = 11;
    auto depthTexture = take(device.createTexture({width, height, gfx::Format::D32Float, false, true}));
    const auto fp = take(gfx::computeTextureRegionFootprint(gfx::Format::D32Float, width, height, 512));

    constexpr std::uint64_t uploadOffset = 512;
    constexpr std::uint64_t readbackOffset = 1024;
    const std::uint64_t uploadSize = uploadOffset + fp.size + 512;
    const std::uint64_t readbackSize = readbackOffset + fp.size + 512;

    std::vector<std::byte> uploadBytes(static_cast<std::size_t>(uploadSize), std::byte{0x44});
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            fillTexel(uploadBytes.data() + uploadOffset + static_cast<std::size_t>(y) * fp.rowPitch + static_cast<std::size_t>(x) * 4u,
                      gfx::Format::D32Float, x, y, 91);
        }
    }
    const std::vector<std::byte> readbackSentinel(static_cast<std::size_t>(readbackSize), std::byte{0x9e});

    auto upload = take(device.createBuffer({uploadSize, gfx::Memory::Upload}));
    auto readbackInit = take(device.createBuffer({readbackSize, gfx::Memory::Upload}));
    auto readback = take(device.createBuffer({readbackSize, gfx::Memory::Readback}));
    take(upload->write(0, uploadBytes));
    take(readbackInit->write(0, readbackSentinel));

    auto commands = take(device.createCommandList());
    take(commands->begin());
    take(commands->copyBuffer(readbackInit, readback, readbackSize));
    take(commands->transition(depthTexture, gfx::ResourceState::CopyDestination));
    expectError(commands->copyBufferToTextureRegion(upload, depthTexture, {uploadOffset, 512, 1, 0, width - 1, height}),
                gfx::ErrorCode::Unsupported,
                "Partial depth upload must return Unsupported");
    expectError(commands->copyBufferToTextureRegion(upload, depthTexture, {uploadOffset, 512, 0, 0, width, height - 1}),
                gfx::ErrorCode::Unsupported,
                "Partial depth upload height must return Unsupported");
    take(commands->copyBufferToTextureRegion(upload, depthTexture, {uploadOffset, 512, 0, 0, width, height}));

    take(commands->transition(depthTexture, gfx::ResourceState::CopySource));
    expectError(commands->copyTextureToBufferRegion(depthTexture, readback, {readbackOffset, 512, 0, 1, width, height - 1}),
                gfx::ErrorCode::Unsupported,
                "Partial depth download must return Unsupported");
    take(commands->copyTextureToBufferRegion(depthTexture, readback, {readbackOffset, 512, 0, 0, width, height}));
    take(commands->end());

    const auto fence = take(device.queue().submit(*commands));
    upload.reset();
    readbackInit.reset();
    depthTexture.reset();
    commands.reset();
    take(device.queue().wait(fence));

    std::vector<std::byte> expected = readbackSentinel;
    for (std::uint32_t y = 0; y < height; ++y) {
        std::memcpy(
            expected.data() + readbackOffset + static_cast<std::size_t>(y) * fp.rowPitch,
            uploadBytes.data() + uploadOffset + static_cast<std::size_t>(y) * fp.rowPitch,
            fp.rowBytes);
    }
    check(take(readback->read(0, readbackSize)) == expected,
          "Full-extent D32Float copy with custom offset/rowPitch and sentinel guards must match");
    ++regionUploads;
    ++regionDownloads;
    verifiedBytes += readbackSize;

    readback.reset();
    verifyReclaimed(device);
}

void runContractRejections(gfx::Device& device, const gfx::DeviceDesc& desc) {
    auto otherDevice = take(gfx::createDevice(desc));
    auto texture = take(device.createTexture({32, 24, gfx::Format::RGBA8UNorm, true}));
    auto foreignTexture = take(otherDevice->createTexture({32, 24, gfx::Format::RGBA8UNorm, true}));
    const auto fp = take(gfx::computeTextureRegionFootprint(gfx::Format::RGBA8UNorm, 32, 24));
    const std::uint64_t bufferSize = 512 + fp.size;

    auto upload = take(device.createBuffer({bufferSize, gfx::Memory::Upload}));
    auto readback = take(device.createBuffer({bufferSize, gfx::Memory::Readback}));
    auto foreignUpload = take(otherDevice->createBuffer({bufferSize, gfx::Memory::Upload}));
    const std::vector<std::byte> data(static_cast<std::size_t>(bufferSize), std::byte{0x22});
    take(upload->write(0, data));

    auto commands = take(device.createCommandList());
    expectError(commands->copyBufferToTextureRegion(upload, texture, {0, 0, 0, 0, 4, 4}),
                gfx::ErrorCode::InvalidState,
                "Region upload outside recording must fail");
    expectError(commands->copyTextureToBufferRegion(texture, readback, {0, 0, 0, 0, 4, 4}),
                gfx::ErrorCode::InvalidState,
                "Region download outside recording must fail");

    take(commands->begin());
    const auto nullCode = desc.backend == gfx::Backend::D3D12
                              ? gfx::ErrorCode::WrongDevice
                              : gfx::ErrorCode::InvalidArgument;
    expectError(commands->copyBufferToTextureRegion({}, texture, {0, 0, 0, 0, 4, 4}), nullCode, "Reject null source buffer");
    expectError(commands->copyBufferToTextureRegion(upload, {}, {0, 0, 0, 0, 4, 4}), nullCode, "Reject null destination texture");
    expectError(commands->copyBufferToTextureRegion(foreignUpload, texture, {0, 0, 0, 0, 4, 4}), gfx::ErrorCode::WrongDevice, "Reject foreign upload buffer");
    expectError(commands->copyBufferToTextureRegion(upload, foreignTexture, {0, 0, 0, 0, 4, 4}), gfx::ErrorCode::WrongDevice, "Reject foreign texture");
    expectError(commands->copyTextureToBufferRegion(foreignTexture, readback, {0, 0, 0, 0, 4, 4}), gfx::ErrorCode::WrongDevice, "Reject foreign download texture");

    // Extent, alignment, bounds, overflow and memory direction rejections.
    expectError(commands->copyBufferToTextureRegion(upload, texture, {0, 0, 0, 0, 0, 4}), gfx::ErrorCode::InvalidArgument, "Zero width");
    expectError(commands->copyBufferToTextureRegion(upload, texture, {0, 0, 0, 0, 4, 0}), gfx::ErrorCode::InvalidArgument, "Zero height");
    expectError(commands->copyBufferToTextureRegion(upload, texture, {0, 0, 30, 0, 3, 4}), gfx::ErrorCode::InvalidArgument, "x + width exceeds texture");
    expectError(commands->copyBufferToTextureRegion(upload, texture, {0, 0, 0, 22, 4, 3}), gfx::ErrorCode::InvalidArgument, "y + height exceeds texture");
    expectError(commands->copyBufferToTextureRegion(upload, texture, {0, 0, std::numeric_limits<std::uint32_t>::max(), 0, 1, 1}),
                gfx::ErrorCode::InvalidArgument, "Overflowing x + width");
    expectError(commands->copyBufferToTextureRegion(upload, texture, {256, 0, 0, 0, 4, 4}), gfx::ErrorCode::InvalidArgument, "bufferOffset not 512-byte aligned");
    expectError(commands->copyBufferToTextureRegion(upload, texture, {0, 128, 0, 0, 4, 4}), gfx::ErrorCode::InvalidArgument, "bufferRowPitch not 256-byte aligned");
    expectError(commands->copyBufferToTextureRegion(upload, texture, {1024, 0, 0, 0, 32, 24}), gfx::ErrorCode::InvalidArgument, "bufferOffset + footprint exceeds buffer");
    expectError(commands->copyBufferToTextureRegion(upload, texture, {std::numeric_limits<std::uint64_t>::max() - 511, 0, 0, 0, 4, 4}),
                gfx::ErrorCode::InvalidArgument, "Overflowing bufferOffset");
    expectError(commands->copyBufferToTextureRegion(readback, texture, {0, 0, 0, 0, 4, 4}), gfx::ErrorCode::InvalidArgument, "Readback as upload source");
    expectError(commands->copyTextureToBufferRegion(texture, upload, {0, 0, 0, 0, 4, 4}), gfx::ErrorCode::InvalidArgument, "Upload as download destination");

    // Atomic rejection: wrong-state region copy must not retain the temporary texture or observe upload version.
    const auto statsBefore = take(device.resourceStatistics());
    {
        auto temporaryTexture = take(device.createTexture({16, 16, gfx::Format::RGBA8UNorm}));
        std::weak_ptr<gfx::Texture> weakTemp = temporaryTexture;
        expectError(commands->copyBufferToTextureRegion(upload, temporaryTexture, {0, 0, 0, 0, 4, 4}),
                    gfx::ErrorCode::InvalidState,
                    "Reject upload when texture is not CopyDestination");
        expectError(commands->copyTextureToBufferRegion(temporaryTexture, readback, {0, 0, 0, 0, 4, 4}),
                    gfx::ErrorCode::InvalidState,
                    "Reject download when texture is not CopySource");
        temporaryTexture.reset();
        check(weakTemp.expired(), "Rejected temporary texture wrapper must expire");
        const auto statsAfter = take(device.resourceStatistics());
        check(statsAfter.trackedObjects == statsBefore.trackedObjects &&
              statsAfter.uniqueResources == statsBefore.uniqueResources &&
              statsAfter.resourceAllocationBytes == statsBefore.resourceAllocationBytes,
              "Rejected region copies must retain zero native allocations");
    }
    take(upload->write(0, data));

    // Reject inside an active render pass.
    take(commands->transition(texture, gfx::ResourceState::RenderTarget));
    take(commands->beginRenderPass(texture, {0.0f, 0.0f, 0.0f, 1.0f}));
    expectError(commands->copyBufferToTextureRegion(upload, texture, {0, 0, 0, 0, 4, 4}),
                gfx::ErrorCode::InvalidState,
                "Reject region upload inside active render pass");
    expectError(commands->copyTextureToBufferRegion(texture, readback, {0, 0, 0, 0, 4, 4}),
                gfx::ErrorCode::InvalidState,
                "Reject region download inside active render pass");
    take(commands->endRenderPass());

    // Accepted region upload followed by CPU Upload write must invalidate submit.
    take(commands->transition(texture, gfx::ResourceState::CopyDestination));
    take(commands->copyBufferToTextureRegion(upload, texture, {512, 0, 2, 2, 8, 8}));
    take(commands->end());
    take(upload->write(0, data));
    expectError(device.queue().submit(*commands),
                gfx::ErrorCode::InvalidState,
                "Upload write after accepted copyBufferToTextureRegion must invalidate submission");

    commands.reset();
    foreignTexture.reset();
    foreignUpload.reset();
    otherDevice.reset();
    texture.reset();
    upload.reset();
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
            throw std::runtime_error("Usage: gfx_texture_region_tests [--d3d12] [--validation] [--warp]");
        }
    }

    check(gfx::backendCompiled(backend), "Requested backend must be compiled");
    const auto adapters = take(gfx::enumerateAdapters(backend));
    if (backend == gfx::Backend::D3D12 && !warp &&
        !std::any_of(adapters.begin(), adapters.end(), [](const auto& info) { return !info.softwareAdapter; })) {
        std::cout << "SKIP no hardware D3D12 adapter\n";
        return 77;
    }

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
        runColorPartialUploadAndDownload(*device, gfx::Format::RGBA8UNorm, 1);
        runColorPartialUploadAndDownload(*device, gfx::Format::BGRA8UNorm, 2);
        runColorPartialUploadAndDownload(*device, gfx::Format::RGBA16Float, 3);
        runDepthFullExtentAndPartialRejection(*device);
        runContractRejections(*device, desc);

        if (validation) {
            check(take(device->validationErrorCount()) == 0,
                  "Validation error count must remain zero");
        }
        ++testedAdapters;
    }

    check(testedAdapters > 0, "At least one real adapter must execute");
    std::cout << "{\"event\":\"texture_region\""
              << ",\"backend\":" << std::quoted(backend == gfx::Backend::D3D12 ? "d3d12" : "vulkan")
              << ",\"source\":" << std::quoted(gfx::buildFingerprint())
              << ",\"adapters\":" << testedAdapters
              << ",\"validation\":" << (validation ? "true" : "false")
              << ",\"assertions\":" << assertions
              << ",\"region_uploads\":" << regionUploads
              << ",\"region_downloads\":" << regionDownloads
              << ",\"rejections\":" << rejections
              << ",\"verified_bytes\":" << verifiedBytes
              << ",\"errors\":0}\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
