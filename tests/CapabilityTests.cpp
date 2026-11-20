#include <graphics/Graphics.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
unsigned assertions{};
unsigned formatQueries{};
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

void verifyEmptyStats(gfx::Device& device) {
    const auto stats = take(device.resourceStatistics());
    check(stats.trackedObjects == 0 &&
          stats.uniqueResources == 0 &&
          stats.resourceAllocationBytes == 0 &&
          stats.pendingSubmissions == 0 &&
          stats.pendingObjectReferences == 0,
          "Capability queries must not allocate or retain SDK resources");
}

void verifyCapsAndFormats(gfx::Device& device, gfx::Backend backend) {
    const auto& caps = device.caps();
    check(caps.backend == backend, "Reported backend matches requested backend");
    check(caps.maxTextureDimension2D >= 4096u && caps.maxTextureDimension2D <= 16384u,
          "maxTextureDimension2D must be within valid 2D range");
    check(caps.maxColorAttachments >= 4u && caps.maxColorAttachments <= 8u,
          "maxColorAttachments must be 4..8");
    check(caps.multipleRenderTargets == (caps.maxColorAttachments >= 2u),
          "multipleRenderTargets must agree with maxColorAttachments");
    check(caps.constantBufferAlignment >= 256u &&
          (caps.constantBufferAlignment & (caps.constantBufferAlignment - 1u)) == 0u,
          "constantBufferAlignment must be a power of two >= 256");
    check(caps.textureCopyRowAlignment == 256u, "textureCopyRowAlignment must be 256");
    check(caps.textureCopyOffsetAlignment == 512u, "textureCopyOffsetAlignment must be 512");
    check(caps.maxVertexStreams >= 15u && caps.maxVertexStreams <= 32u,
          "maxVertexStreams must be 15..32");
    check(caps.maxConstantBufferSize >= 16384u && caps.maxConstantBufferSize <= 65536u,
          "maxConstantBufferSize must be 16KiB..64KiB");
    if (backend == gfx::Backend::D3D12) {
        check(caps.maxTextureDimension2D == 16384u &&
              caps.maxColorAttachments == 8u &&
              caps.constantBufferAlignment == 256u &&
              caps.maxVertexStreams == 32u &&
              caps.maxConstantBufferSize == 65536u,
              "D3D12 limits must match architectural/SDK caps");
    }

    verifyEmptyStats(device);

    constexpr std::array<gfx::Format, 3> colorFormats{
        gfx::Format::RGBA8UNorm,
        gfx::Format::BGRA8UNorm,
        gfx::Format::RGBA16Float,
    };
    for (const auto format : colorFormats) {
        const auto support = take(device.queryFormatSupport(format));
        ++formatQueries;
        check(support.format == format, "Returned FormatSupport format matches request");
        check(support.texture2D && support.renderTarget && support.sampled &&
              support.linearFilter && support.blendable &&
              support.copySource && support.copyDestination &&
              support.partialRegionCopy && !support.depthStencil,
              "Color format support flags must match effective capabilities");
        // Cross-check reported support by creating an actual render-target texture.
        auto tex = take(device.createTexture({16, 16, format, support.renderTarget, false}));
        check(tex->description().format == format, "Created color texture matches queried format");
        tex.reset();
    }

    const auto depthSupport = take(device.queryFormatSupport(gfx::Format::D32Float));
    ++formatQueries;
    check(depthSupport.format == gfx::Format::D32Float, "Returned D32Float format matches request");
    check(depthSupport.depthStencil == caps.depthAttachment &&
          depthSupport.texture2D == caps.depthAttachment &&
          !depthSupport.renderTarget && !depthSupport.sampled &&
          !depthSupport.linearFilter && !depthSupport.blendable &&
          !depthSupport.partialRegionCopy,
          "D32Float support flags must match effective SDK and adapter depth contract");
    if (depthSupport.depthStencil) {
        auto depthTex = take(device.createTexture({16, 16, gfx::Format::D32Float, false, true}));
        check(depthTex->description().depthStencil, "Created depth texture matches queried format");
        depthTex.reset();
    }

    verifyEmptyStats(device);

    // Unknown format enum must fail with InvalidArgument.
    expectError(device.queryFormatSupport(static_cast<gfx::Format>(99)),
                gfx::ErrorCode::InvalidArgument,
                "Unknown format enum must return InvalidArgument");

    // Creating-thread contract: worker thread query must return InvalidState.
    bool workerRejected = false;
    std::thread worker([&]() {
        const auto r = device.queryFormatSupport(gfx::Format::RGBA8UNorm);
        workerRejected = !r && r.error().code == gfx::ErrorCode::InvalidState;
    });
    worker.join();
    check(workerRejected, "queryFormatSupport on non-creating thread must return InvalidState");
    ++rejections;

    verifyEmptyStats(device);
}

void verifyD3D12LossAndRecovery(const gfx::AdapterInfo& adapter, bool validation) {
    gfx::DeviceDesc desc;
    desc.backend = gfx::Backend::D3D12;
    desc.validation = validation;
    desc.softwareAdapter = adapter.softwareAdapter;
    desc.adapter = adapter.id;
    desc.enableDeviceLossTesting = true;

    auto device = take(gfx::createDevice(desc));
    if (!device->caps().deviceLossInjection) {
        return;
    }
    const auto beforeLoss = take(device->queryFormatSupport(gfx::Format::RGBA8UNorm));
    ++formatQueries;
    take(device->removeForTesting());
    expectError(device->queryFormatSupport(gfx::Format::RGBA8UNorm),
                gfx::ErrorCode::DeviceLost,
                "queryFormatSupport on lost device must return DeviceLost");

    auto recovered = take(device->recover());
    expectError(device->queryFormatSupport(gfx::Format::RGBA8UNorm),
                gfx::ErrorCode::DeviceLost,
                "queryFormatSupport on retired device must remain DeviceLost");
    const auto afterRecovery = take(recovered->queryFormatSupport(gfx::Format::RGBA8UNorm));
    ++formatQueries;
    check(afterRecovery == beforeLoss, "Recovered same-adapter device must report identical format support");
    if (validation) {
        check(take(recovered->validationErrorCount()) == 0,
              "Recovered device validation error count must remain zero");
    }
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
            throw std::runtime_error("Usage: gfx_capability_tests [--d3d12] [--validation] [--warp]");
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
        verifyCapsAndFormats(*device, backend);
        if (validation) {
            check(take(device->validationErrorCount()) == 0,
                  "Validation error count must remain zero");
        }
        device.reset();

        if (backend == gfx::Backend::D3D12 && warp) {
            verifyD3D12LossAndRecovery(adapter, validation);
        }
        ++testedAdapters;
    }

    check(testedAdapters > 0, "At least one real adapter must execute");
    std::cout << "{\"event\":\"capabilities\""
              << ",\"backend\":" << std::quoted(backend == gfx::Backend::D3D12 ? "d3d12" : "vulkan")
              << ",\"source\":" << std::quoted(gfx::buildFingerprint())
              << ",\"adapters\":" << testedAdapters
              << ",\"validation\":" << (validation ? "true" : "false")
              << ",\"assertions\":" << assertions
              << ",\"format_queries\":" << formatQueries
              << ",\"rejections\":" << rejections
              << ",\"errors\":0}\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
