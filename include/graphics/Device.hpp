#pragma once
#include "CommandQueue.hpp"
#include "SwapChain.hpp"

namespace gfx {
class Device {
public:
    virtual ~Device() = default;
    [[nodiscard]] virtual const DeviceCaps& caps() const = 0;
    [[nodiscard]] virtual Result<void> checkStatus() = 0;
    // Creating thread only. Diagnostic traversal; does not wait for in-flight work.
    [[nodiscard]] virtual Result<ResourceStatistics> resourceStatistics() = 0;
    // Creating thread only, no GPU wait. D3D12 queries node 0 of the actual device's
    // adapter. OS usage includes other owners on that adapter in this process.
    // Unsupported queries return an error, never a synthesized zero snapshot.
    [[nodiscard]] virtual Result<MemoryBudget> queryMemoryBudget(MemorySegment segment) = 0;
    // On loss, retire this generation and return a fresh device with the same selection policy.
    // Old handles keep CPU descriptions, but all their native operations remain DeviceLost.
    [[nodiscard]] virtual Result<std::unique_ptr<Device>> recover() = 0;
    // Native device removal, only when explicitly enabled in DeviceDesc. Never a driver TDR.
    [[nodiscard]] virtual Result<void> removeForTesting() = 0;
    [[nodiscard]] virtual Result<BufferPtr> createBuffer(BufferDesc) = 0;
    [[nodiscard]] virtual Result<TexturePtr> createTexture(TextureDesc) = 0;
    [[nodiscard]] virtual Result<PipelinePtr> createGraphicsPipeline(const GraphicsPipelineDesc&) = 0;
    [[nodiscard]] virtual Result<std::unique_ptr<CommandList>> createCommandList() = 0;
    // Creating thread only. Validate a live native surface and selected queue;
    // does not create a swapchain or guarantee format/present support.
    [[nodiscard]] virtual Result<void> checkSurfaceSupport(Surface) = 0;
    [[nodiscard]] virtual Result<std::unique_ptr<SwapChain>> createSwapChain(Surface, SwapChainDesc) = 0;
    [[nodiscard]] virtual CommandQueue& queue() = 0;
    // Cumulative ERROR/CORRUPTION count since callback registration, independent of
    // native text storage limits/clearing. D3D12 validation requires InfoQueue1;
    // Vulkan requires Khronos validation/debug utils, synchronization features and layer settings.
    // Vulkan counts delivered ERROR callbacks; external layer settings can filter/limit them.
    [[nodiscard]] virtual Result<std::uint64_t> validationErrorCount() = 0;
};
[[nodiscard]] Result<std::unique_ptr<Device>> createDevice(DeviceDesc desc = {});
}
