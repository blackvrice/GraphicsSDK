#pragma once
// 비침투 외부 AI 연산(CUDA / DirectML / ONNX Runtime) Interop 브리지 예제.
// GraphicsSDK 공개 헤더(<graphics/Graphics.hpp>)만 사용하며 외부 AI 런타임에 대한
// 컴파일/링크 의존성을 SDK 코어에 추가하지 않는다.

#include <graphics/Graphics.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ai_interop {

// 외부 AI 런타임과 공유하는 텐서 형상 (FP32 기준, GPU 버퍼 크기는 256바이트 정렬).
struct TensorShape {
    std::uint32_t batch{1};
    std::uint32_t channels{1};
    std::uint32_t height{1};
    std::uint32_t width{1};

    [[nodiscard]] std::uint64_t elementCount() const noexcept {
        return static_cast<std::uint64_t>(batch) * channels * height * width;
    }
    [[nodiscard]] std::uint64_t rawByteSize() const noexcept {
        return elementCount() * sizeof(float);
    }
    [[nodiscard]] std::uint64_t alignedByteSize() const noexcept {
        const std::uint64_t raw = rawByteSize();
        return (raw + 255u) & ~std::uint64_t{255u};
    }
};

enum class InteropMode {
    ExternalHandleZeroCopy, // Win32 NT / OpaqueFd 외부 핸들 Zero-Copy (CUDA / Cross-API Vulkan/D3D12)
    InProcessNativeHandle,  // 동일 프로세스 비소유 네이티브 핸들 (DirectML / ONNX Runtime D3D12 EP)
    HostStagingFallback     // Zero-Copy 불가 또는 CPU EP 환경의 안전 스테이징 복사 폴백
};

// DirectML / ONNX Runtime D3D12 EP 등 동일 프로세스 외부 AI 런타임에 전달하는 비소유 핸들 묶음.
// 호출자는 외부 AI 실행이 완료될 때까지 원본 BufferPtr 및 TimelineFencePtr 수명을 유지해야 한다.
struct InProcessNativeBinding {
    gfx::NativeHandle device{};
    gfx::NativeHandle tensorBuffer{};
    gfx::NativeHandle timelineFence{};
    std::uint64_t byteSize{};
};

// 외부 프로세스/다른 API(CUDA/Vulkan) AI 런타임에 소유권을 이전하는 외부 핸들 묶음.
struct ExportedInteropHandles {
    gfx::ExternalHandle tensorBuffer;
    gfx::ExternalHandle timelineFence;
    gfx::BufferDesc bufferDesc{};
};

// GraphicsSDK 측에서 소유하는 AI 공유 텐서 및 단조 증가 타임라인 동기화 상태.
struct SharedTensor {
    TensorShape shape{};
    InteropMode mode{InteropMode::HostStagingFallback};
    gfx::BufferPtr buffer;
    gfx::TimelineFencePtr fence;
    std::uint64_t nextValue{1};

    [[nodiscard]] gfx::Result<InProcessNativeBinding> queryInProcessHandles(gfx::Device& device) const {
        auto devH = device.nativeDeviceHandle();
        if (!devH) return std::unexpected(devH.error());
        auto bufH = device.nativeBufferHandle(buffer);
        if (!bufH) return std::unexpected(bufH.error());
        auto fenH = device.nativeTimelineFenceHandle(fence);
        if (!fenH) return std::unexpected(fenH.error());
        return InProcessNativeBinding{*devH, *bufH, *fenH, shape.alignedByteSize()};
    }

    [[nodiscard]] gfx::Result<ExportedInteropHandles> exportHandles(gfx::Device& device) const {
        if (mode != InteropMode::ExternalHandleZeroCopy) {
            return std::unexpected(gfx::Error{
                gfx::ErrorCode::Unsupported,
                "SharedTensor was not created in ExternalHandleZeroCopy mode"});
        }
        auto bufH = device.exportBufferHandle(buffer);
        if (!bufH) return std::unexpected(bufH.error());
        auto fenH = device.exportTimelineFenceHandle(fence);
        if (!fenH) return std::unexpected(fenH.error());
        ExportedInteropHandles out{};
        out.tensorBuffer = std::move(*bufH);
        out.timelineFence = std::move(*fenH);
        out.bufferDesc = buffer->description();
        return out;
    }
};

// 텐서 원소별 Affine(+선택적 ReLU) 연산 파라미터 (256B UBO 선두 16B).
struct AffineParams {
    float scale{1.0f};
    float bias{0.0f};
    float applyRelu{0.0f};
    float elementCount{0.0f};
};

// 환경 능력에 따라 Zero-Copy 공유 텐서 또는 안전 폴백 텐서를 생성한다.
[[nodiscard]] inline gfx::Result<SharedTensor> createSharedTensor(
    gfx::Device& device,
    TensorShape shape,
    bool preferZeroCopy) {
    if (shape.elementCount() == 0) {
        return std::unexpected(gfx::Error{gfx::ErrorCode::InvalidArgument, "Tensor shape must be non-empty"});
    }
    const bool canZeroCopy = preferZeroCopy &&
        device.caps().externalMemoryInterop &&
        device.caps().externalFenceInterop;

    gfx::BufferDesc bufDesc{};
    bufDesc.size = shape.alignedByteSize();
    bufDesc.memory = gfx::Memory::Device;
    bufDesc.storage = true;
    bufDesc.shared = canZeroCopy;

    auto buf = device.createBuffer(bufDesc);
    if (!buf) return std::unexpected(buf.error());

    auto fence = canZeroCopy
        ? device.createSharedTimelineFence(0)
        : device.createTimelineFence(0);
    if (!fence) return std::unexpected(fence.error());

    SharedTensor out{};
    out.shape = shape;
    out.mode = canZeroCopy ? InteropMode::ExternalHandleZeroCopy : InteropMode::HostStagingFallback;
    out.buffer = std::move(*buf);
    out.fence = std::move(*fence);
    out.nextValue = 1;
    return out;
}

// Compute Shader로 텐서 버퍼에 Affine(+ReLU) 연산을 적용하고 선택적 Timeline Wait/Signal을 수행한다.
[[nodiscard]] inline gfx::Result<gfx::Fence> dispatchTensorOp(
    gfx::Device& device,
    const gfx::PipelinePtr& pipeline,
    const gfx::BufferPtr& tensorBuffer,
    const AffineParams& params,
    const gfx::TimelineFencePtr& waitFence,
    std::uint64_t waitValue,
    const gfx::TimelineFencePtr& signalFence,
    std::uint64_t signalValue,
    const gfx::BufferPtr& optionalUploadSource = nullptr,
    const gfx::BufferPtr& optionalReadbackDest = nullptr,
    std::uint64_t copyBytes = 0,
    bool releaseToCommonAfter = false) {
    auto cbUp = device.createBuffer({256, gfx::Memory::Upload});
    if (!cbUp) return std::unexpected(cbUp.error());
    auto cbDev = device.createBuffer({256, gfx::Memory::Device});
    if (!cbDev) return std::unexpected(cbDev.error());
    std::array<std::byte, 256> cbBytes{};
    std::memcpy(cbBytes.data(), &params, sizeof(params));
    if (auto ok = (*cbUp)->write(0, cbBytes); !ok) return std::unexpected(ok.error());

    auto cmd = device.createCommandList();
    if (!cmd) return std::unexpected(cmd.error());
    if (auto ok = (*cmd)->begin(); !ok) return std::unexpected(ok.error());

    if (auto ok = (*cmd)->transition(*cbDev, gfx::ResourceState::CopyDestination); !ok) return std::unexpected(ok.error());
    if (auto ok = (*cmd)->copyBuffer(*cbUp, *cbDev, 256); !ok) return std::unexpected(ok.error());
    if (auto ok = (*cmd)->transition(*cbDev, gfx::ResourceState::ConstantBuffer); !ok) return std::unexpected(ok.error());

    if (optionalUploadSource && copyBytes > 0) {
        if (auto ok = (*cmd)->transition(tensorBuffer, gfx::ResourceState::CopyDestination); !ok) return std::unexpected(ok.error());
        if (auto ok = (*cmd)->copyBuffer(optionalUploadSource, tensorBuffer, copyBytes); !ok) return std::unexpected(ok.error());
    }

    if (auto ok = (*cmd)->transition(tensorBuffer, gfx::ResourceState::UnorderedAccess); !ok) return std::unexpected(ok.error());
    if (auto ok = (*cmd)->setComputePipeline(pipeline); !ok) return std::unexpected(ok.error());

    gfx::ResourceBindings bindings{};
    bindings.storageBuffers = {{0, 0, tensorBuffer, 0, 0}};
    bindings.constants = {{1, 0, *cbDev, 0, 256}};
    if (auto ok = (*cmd)->bindComputeResources(bindings); !ok) return std::unexpected(ok.error());

    const std::uint32_t count = static_cast<std::uint32_t>(params.elementCount);
    const std::uint32_t groupsX = (count + 63u) / 64u;
    if (auto ok = (*cmd)->dispatch(groupsX, 1u, 1u); !ok) return std::unexpected(ok.error());
    if (auto ok = (*cmd)->uavBarrier(tensorBuffer); !ok) return std::unexpected(ok.error());

    if (optionalReadbackDest && copyBytes > 0) {
        if (auto ok = (*cmd)->transition(tensorBuffer, gfx::ResourceState::CopySource); !ok) return std::unexpected(ok.error());
        if (auto ok = (*cmd)->copyBuffer(tensorBuffer, optionalReadbackDest, copyBytes); !ok) return std::unexpected(ok.error());
    }
    if (releaseToCommonAfter) {
        if (auto ok = (*cmd)->transition(tensorBuffer, gfx::ResourceState::Common); !ok) return std::unexpected(ok.error());
    }
    if (auto ok = (*cmd)->end(); !ok) return std::unexpected(ok.error());

    gfx::SubmitDesc sd{};
    gfx::CommandList* listPtr = cmd->get();
    sd.commandLists = std::span<gfx::CommandList* const>(&listPtr, 1);
    gfx::TimelineWait tw{waitFence, waitValue};
    gfx::TimelineSignal ts{signalFence, signalValue};
    if (waitFence) sd.waits = std::span<const gfx::TimelineWait>(&tw, 1);
    if (signalFence) sd.signals = std::span<const gfx::TimelineSignal>(&ts, 1);
    return device.queue().submit(sd);
}

// CPU 기준 참조 계산 (전처리 -> Affine+ReLU 추론 -> 후처리).
inline std::vector<float> computeCpuReference(
    std::span<const float> input,
    const AffineParams& pre,
    const AffineParams& infer,
    const AffineParams& post) {
    std::vector<float> out(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        float v = input[i] * pre.scale + pre.bias;
        if (pre.applyRelu > 0.5f && v < 0.0f) v = 0.0f;
        v = v * infer.scale + infer.bias;
        if (infer.applyRelu > 0.5f && v < 0.0f) v = 0.0f;
        v = v * post.scale + post.bias;
        if (post.applyRelu > 0.5f && v < 0.0f) v = 0.0f;
        out[i] = v;
    }
    return out;
}

} // namespace ai_interop
