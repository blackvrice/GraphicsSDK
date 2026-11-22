#include <graphics/Graphics.hpp>
#include "../examples/ai_compute_interop/AiTensorInteropBridge.hpp"
#include "fixtures/ComputeFixtures.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Totals {
    std::uint64_t adapters{};
    std::uint64_t assertions{};
    std::uint64_t rejections{};
    std::uint64_t inferencesVerified{};
};

void check(bool condition, const std::string& message, Totals& totals) {
    if (!condition) throw std::runtime_error(message);
    ++totals.assertions;
}

template <class T>
T take(gfx::Result<T> result, const std::string& what, Totals& totals) {
    if (!result) {
        throw std::runtime_error(what + ": " + result.error().message +
                                 " (code=" + std::to_string(static_cast<int>(result.error().code)) +
                                 ", native=" + std::to_string(result.error().nativeCode) + ")");
    }
    ++totals.assertions;
    return std::move(result.value());
}

void take(gfx::Result<void> result, const std::string& what, Totals& totals) {
    if (!result) {
        throw std::runtime_error(what + ": " + result.error().message +
                                 " (code=" + std::to_string(static_cast<int>(result.error().code)) +
                                 ", native=" + std::to_string(result.error().nativeCode) + ")");
    }
    ++totals.assertions;
}

template <class T>
void expectError(const gfx::Result<T>& result, gfx::ErrorCode expected, const std::string& what, Totals& totals) {
    if (result) throw std::runtime_error(what + ": expected error but succeeded");
    if (result.error().code != expected) {
        throw std::runtime_error(what + ": expected error code " +
                                 std::to_string(static_cast<int>(expected)) + " got " +
                                 std::to_string(static_cast<int>(result.error().code)) +
                                 " (" + result.error().message + ")");
    }
    ++totals.rejections;
}

template <std::size_t N>
gfx::ShaderBinary spirvBinary(gfx::ShaderStage stage, const std::array<std::uint32_t, N>& words) {
    gfx::ShaderBinary bin{};
    bin.format = gfx::ShaderFormat::SPIRV;
    bin.stage = stage;
    bin.bytes.resize(N * sizeof(std::uint32_t));
    std::memcpy(bin.bytes.data(), words.data(), bin.bytes.size());
    return bin;
}

gfx::PipelinePtr makeAffineReluPipeline(gfx::Device& device, gfx::Backend backend, Totals& totals) {
    gfx::ShaderBinary cs{};
    if (backend == gfx::Backend::D3D12) {
        constexpr std::string_view hlsl = R"(
RWByteAddressBuffer tensorBuf : register(u0);
cbuffer AiParams : register(b0) {
    float4 params;
};
[numthreads(64, 1, 1)]
void CS(uint3 gid : SV_DispatchThreadID) {
    uint idx = gid.x;
    uint count = (uint)params.w;
    if (idx >= count) return;
    float v = asfloat(tensorBuf.Load(idx * 4));
    v = v * params.x + params.y;
    if (params.z > 0.5) {
        v = v > 0.0 ? v : 0.0;
    }
    tensorBuf.Store(idx * 4, asuint(v));
}
)";
        cs = take(gfx::compileHlsl(hlsl, "CS", gfx::ShaderStage::Compute), "compile HLSL Affine+ReLU CS", totals);
    } else {
        cs = spirvBinary(gfx::ShaderStage::Compute, computeFixtures::ai_affine_relu_fp32);
    }
    gfx::ComputePipelineDesc desc{};
    desc.compute = cs;
    desc.bindingLayout.entries = {
        {gfx::BindingType::StorageBuffer, 0, 1, 0, gfx::ShaderVisibility::Compute},
        {gfx::BindingType::ConstantBuffer, 0, 1, 0, gfx::ShaderVisibility::Compute},
    };
    return take(device.createComputePipeline(desc), "create Affine+ReLU compute pipeline", totals);
}

std::vector<float> makeInputTensor(std::size_t count, std::uint32_t frameIdx) {
    std::vector<float> values(count);
    for (std::size_t i = 0; i < count; ++i) {
        // Deterministic signed distribution so ReLU exercises both clamped (<=0) and active (>0) branches.
        const float t = static_cast<float>((i * 37u + frameIdx * 101u) % 200u) / 100.0f - 1.0f;
        values[i] = t * 2.5f;
    }
    return values;
}

void verifyFloatTensor(
    std::span<const std::byte> actualBytes,
    std::span<const float> expected,
    const std::string& label,
    Totals& totals) {
    check(actualBytes.size() >= expected.size() * sizeof(float), label + " byte size check", totals);
    for (std::size_t i = 0; i < expected.size(); ++i) {
        float actual = 0.0f;
        std::memcpy(&actual, actualBytes.data() + i * sizeof(float), sizeof(float));
        if (std::isnan(actual) || std::fabs(actual - expected[i]) > 1e-4f) {
            throw std::runtime_error(label + " mismatch at idx=" + std::to_string(i) +
                                     ": expected=" + std::to_string(expected[i]) +
                                     " actual=" + std::to_string(actual));
        }
    }
    ++totals.assertions;
    ++totals.inferencesVerified;
}

void runHostFallbackVerification(
    gfx::Device& device,
    const gfx::PipelinePtr& pipeline,
    const std::string& tag,
    Totals& totals) {
    const ai_interop::TensorShape shape{1, 1, 64, 64}; // 4,096 FP32 = 16 KiB
    auto tensor = take(ai_interop::createSharedTensor(device, shape, /*preferZeroCopy=*/false), "create fallback tensor", totals);
    check(tensor.mode == ai_interop::InteropMode::HostStagingFallback, "mode must be HostStagingFallback", totals);

    const std::size_t elemCount = static_cast<std::size_t>(shape.elementCount());
    const std::uint64_t copyBytes = shape.alignedByteSize();
    const auto input = makeInputTensor(elemCount, 0);

    const ai_interop::AffineParams pre{0.5f, -0.25f, 0.0f, static_cast<float>(elemCount)};
    const ai_interop::AffineParams infer{1.75f, 0.10f, 1.0f, static_cast<float>(elemCount)};
    const ai_interop::AffineParams post{2.0f, 0.05f, 0.0f, static_cast<float>(elemCount)};

    auto upBuf = take(device.createBuffer({copyBytes, gfx::Memory::Upload}), "fallback upload", totals);
    auto rbBuf = take(device.createBuffer({copyBytes, gfx::Memory::Readback}), "fallback readback", totals);
    take(upBuf->write(0, std::as_bytes(std::span<const float>(input))), "write fallback input", totals);

    // 1. GraphicsSDK preprocess + readback for host/CPU AI EP.
    auto f1 = take(ai_interop::dispatchTensorOp(
        device, pipeline, tensor.buffer, pre, nullptr, 0, tensor.fence, 1, upBuf, rbBuf, copyBytes),
        "fallback preprocess", totals);
    take(device.queue().wait(f1, std::chrono::milliseconds(5000)), "wait fallback pre", totals);
    const auto midBytes = take(rbBuf->read(0, copyBytes), "readback pre tensor", totals);

    // 2. Host/CPU external AI runtime inference (e.g., ONNX Runtime CPU EP).
    std::vector<float> mid(elemCount);
    std::memcpy(mid.data(), midBytes.data(), elemCount * sizeof(float));
    for (float& v : mid) {
        v = v * infer.scale + infer.bias;
        if (v < 0.0f) v = 0.0f;
    }
    take(upBuf->write(0, std::as_bytes(std::span<const float>(mid))), "upload AI output", totals);

    // 3. GraphicsSDK postprocess + readback.
    auto f2 = take(ai_interop::dispatchTensorOp(
        device, pipeline, tensor.buffer, post, tensor.fence, 1, tensor.fence, 2, upBuf, rbBuf, copyBytes),
        "fallback postprocess", totals);
    take(device.queue().wait(f2, std::chrono::milliseconds(5000)), "wait fallback post", totals);
    const auto outBytes = take(rbBuf->read(0, copyBytes), "readback post tensor", totals);

    const auto expected = ai_interop::computeCpuReference(input, pre, infer, post);
    verifyFloatTensor(outBytes, expected, tag + "/SafeFallback", totals);
    std::cout << "  [" << tag << "] Safe-Copy Host-Staging Fallback (4096 FP32): PASS\n";
}

void runZeroCopyPingPong(
    gfx::Device& gfxDev,
    gfx::Backend gfxBackend,
    gfx::Device& aiDev,
    gfx::Backend aiBackend,
    const std::string& tag,
    Totals& totals) {
    const ai_interop::TensorShape shape{1, 1, 64, 64}; // 4,096 FP32 = 16 KiB
    const std::size_t elemCount = static_cast<std::size_t>(shape.elementCount());
    const std::uint64_t copyBytes = shape.alignedByteSize();

    auto gfxPipe = makeAffineReluPipeline(gfxDev, gfxBackend, totals);
    auto aiPipe = makeAffineReluPipeline(aiDev, aiBackend, totals);

    auto tensor = take(ai_interop::createSharedTensor(gfxDev, shape, /*preferZeroCopy=*/true), "create zero-copy tensor", totals);
    check(tensor.mode == ai_interop::InteropMode::ExternalHandleZeroCopy, "mode must be ExternalHandleZeroCopy", totals);

    // Verify In-Process NativeHandle contract (DirectML / ONNX Runtime D3D12 EP boundary).
    const auto nativeBinding = take(tensor.queryInProcessHandles(gfxDev), "queryInProcessHandles", totals);
    check(nativeBinding.device.value != 0, "native device handle non-zero", totals);
    check(nativeBinding.tensorBuffer.value != 0, "native tensor buffer handle non-zero", totals);
    check(nativeBinding.timelineFence.value != 0, "native timeline fence handle non-zero", totals);
    // Cross-device native handle lookup must be rejected with WrongDevice.
    expectError(aiDev.nativeBufferHandle(tensor.buffer), gfx::ErrorCode::WrongDevice, "cross-device nativeBufferHandle", totals);
    expectError(aiDev.nativeTimelineFenceHandle(tensor.fence), gfx::ErrorCode::WrongDevice, "cross-device nativeTimelineFenceHandle", totals);

    // Export external handles -> import into external AI runtime device.
    auto exported = take(tensor.exportHandles(gfxDev), "export AI tensor/fence handles", totals);
    auto aiTensorBuf = take(aiDev.importBufferHandle(exported.bufferDesc, exported.tensorBuffer), "import AI tensor buffer", totals);
    auto aiFence = take(aiDev.importTimelineFenceHandle(exported.timelineFence), "import AI timeline fence", totals);

    auto upBuf = take(gfxDev.createBuffer({copyBytes, gfx::Memory::Upload}), "ping-pong upload", totals);
    auto rbBuf = take(gfxDev.createBuffer({copyBytes, gfx::Memory::Readback}), "ping-pong readback", totals);

    // 3 consecutive GPU-to-GPU frames: GraphicsSDK preprocess -> AI Runtime Affine+ReLU -> GraphicsSDK postprocess.
    for (std::uint32_t frame = 0; frame < 3u; ++frame) {
        const auto input = makeInputTensor(elemCount, frame + 1u);
        const ai_interop::AffineParams pre{0.5f + 0.1f * static_cast<float>(frame), -0.25f, 0.0f, static_cast<float>(elemCount)};
        const ai_interop::AffineParams infer{1.75f, 0.10f * static_cast<float>(frame + 1u), 1.0f, static_cast<float>(elemCount)};
        const ai_interop::AffineParams post{2.0f, 0.05f, 0.0f, static_cast<float>(elemCount)};

        take(upBuf->write(0, std::as_bytes(std::span<const float>(input))), "write frame input", totals);

        const std::uint64_t sigPre = tensor.nextValue++;
        const std::uint64_t sigAi = tensor.nextValue++;
        const std::uint64_t sigPost = tensor.nextValue++;

        // Step 1 (GraphicsSDK): upload + preprocess + signal(sigPre).
        auto fPre = take(ai_interop::dispatchTensorOp(
            gfxDev, gfxPipe, tensor.buffer, pre,
            frame == 0 ? nullptr : tensor.fence, frame == 0 ? 0 : (sigPre - 1),
            tensor.fence, sigPre,
            upBuf, nullptr, copyBytes, /*releaseToCommonAfter=*/true),
            "submit preprocess", totals);

        // Step 2 (External AI Runtime): GPU wait(sigPre) -> Affine+ReLU -> signal(sigAi).
        auto fAi = take(ai_interop::dispatchTensorOp(
            aiDev, aiPipe, aiTensorBuf, infer,
            aiFence, sigPre,
            aiFence, sigAi,
            nullptr, nullptr, 0, /*releaseToCommonAfter=*/true),
            "submit AI inference", totals);

        // Step 3 (GraphicsSDK): GPU wait(sigAi) -> postprocess + readback + signal(sigPost).
        auto fPost = take(ai_interop::dispatchTensorOp(
            gfxDev, gfxPipe, tensor.buffer, post,
            tensor.fence, sigAi,
            tensor.fence, sigPost,
            nullptr, rbBuf, copyBytes, /*releaseToCommonAfter=*/false),
            "submit postprocess", totals);

        take(gfxDev.queue().wait(fPost, std::chrono::milliseconds(5000)), "wait postprocess", totals);
        take(aiDev.queue().wait(fAi, std::chrono::milliseconds(5000)), "wait AI queue", totals);
        take(gfxDev.queue().wait(fPre, std::chrono::milliseconds(5000)), "wait preprocess", totals);

        const auto outBytes = take(rbBuf->read(0, copyBytes), "readback frame output", totals);
        const auto expected = ai_interop::computeCpuReference(input, pre, infer, post);
        verifyFloatTensor(outBytes, expected, tag + "/Frame" + std::to_string(frame), totals);
    }
    std::cout << "  [" << tag << "] Zero-Copy 3-Frame GPU-to-GPU Ping-Pong (12,288 FP32): PASS\n";
}

void runSameApiSuite(gfx::Backend backend, bool validation, bool warp, Totals& totals) {
    auto adapters = take(gfx::enumerateAdapters(backend), "enumerateAdapters", totals);
    for (const auto& a : adapters) {
        if (warp ? !a.softwareAdapter : a.softwareAdapter) continue;
        const std::string tag = std::string(backend == gfx::Backend::D3D12 ? "D3D12/" : "Vulkan/") + a.name;
        std::cout << "=== AI Compute Interop " << tag << (validation ? " [validation]" : "") << " ===\n";

        gfx::DeviceDesc desc{};
        desc.backend = backend;
        desc.adapter = a.id;
        desc.softwareAdapter = warp;
        desc.validation = validation;

        auto gfxDev = take(gfx::createDevice(desc), "create gfx device", totals);
        auto pipe = makeAffineReluPipeline(*gfxDev, backend, totals);
        runHostFallbackVerification(*gfxDev, pipe, tag, totals);

        if (!warp && gfxDev->caps().externalMemoryInterop && gfxDev->caps().externalFenceInterop) {
            auto aiDev = take(gfx::createDevice(desc), "create external AI device", totals);
            runZeroCopyPingPong(*gfxDev, backend, *aiDev, backend, tag + "/SameAPI", totals);
            if (validation) {
                const auto v2 = take(aiDev->validationErrorCount(), "AI device validationErrorCount", totals);
                check(v2 == 0, "AI device validationErrorCount == 0", totals);
            }
        }
        if (validation) {
            const auto v1 = take(gfxDev->validationErrorCount(), "gfx device validationErrorCount", totals);
            check(v1 == 0, "gfx device validationErrorCount == 0", totals);
            std::cout << "  [" << tag << "] validationErrorCount=0\n";
        }
        ++totals.adapters;
    }
}

void runCrossApiSuite(bool validation, Totals& totals) {
    auto d3dAdapters = take(gfx::enumerateAdapters(gfx::Backend::D3D12), "enumerate D3D12", totals);
    auto vkAdapters = take(gfx::enumerateAdapters(gfx::Backend::Vulkan), "enumerate Vulkan", totals);
    for (const auto& da : d3dAdapters) {
        if (da.softwareAdapter) continue;
        for (const auto& va : vkAdapters) {
            if (va.softwareAdapter) continue;
            if (da.vendorId != va.vendorId || da.deviceId != va.deviceId) continue;

            const std::string tag = "CrossAPI(D3D12->Vulkan)/" + da.name;
            std::cout << "=== AI Compute Interop " << tag << (validation ? " [validation]" : "") << " ===\n";

            gfx::DeviceDesc dDesc{};
            dDesc.backend = gfx::Backend::D3D12;
            dDesc.adapter = da.id;
            dDesc.validation = validation;

            gfx::DeviceDesc vDesc{};
            vDesc.backend = gfx::Backend::Vulkan;
            vDesc.adapter = va.id;
            vDesc.validation = validation;

            auto d3dDev = take(gfx::createDevice(dDesc), "create D3D12 gfx device", totals);
            auto vkDev = take(gfx::createDevice(vDesc), "create Vulkan AI device", totals);

            runZeroCopyPingPong(*d3dDev, gfx::Backend::D3D12, *vkDev, gfx::Backend::Vulkan, tag, totals);
            if (validation) {
                const auto v1 = take(d3dDev->validationErrorCount(), "D3D12 validationErrorCount", totals);
                const auto v2 = take(vkDev->validationErrorCount(), "Vulkan validationErrorCount", totals);
                check(v1 == 0 && v2 == 0, "Cross-API validationErrorCount must be 0", totals);
                std::cout << "  [" << tag << "] cross-API validationErrorCount=0\n";
            }
            ++totals.adapters;
            break;
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    bool useD3D12 = false;
    bool crossApi = false;
    bool validation = false;
    bool warp = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--d3d12") useD3D12 = true;
        else if (arg == "--vulkan") useD3D12 = false;
        else if (arg == "--cross-api") crossApi = true;
        else if (arg == "--validation") validation = true;
        else if (arg == "--warp") { warp = true; useD3D12 = true; }
    }

    try {
        Totals totals{};
        if (crossApi) {
            runCrossApiSuite(validation, totals);
        } else {
            runSameApiSuite(useD3D12 ? gfx::Backend::D3D12 : gfx::Backend::Vulkan, validation, warp, totals);
        }
        check(totals.adapters > 0, "Expected at least one matching adapter", totals);
        std::cout << "PASS gfx_ai_compute_interop_tests: adapters=" << totals.adapters
                  << " inferences=" << totals.inferencesVerified
                  << " assertions=" << totals.assertions
                  << " rejections=" << totals.rejections << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
