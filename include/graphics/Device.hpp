#pragma once
#include "CommandQueue.hpp"
#include "SwapChain.hpp"

namespace gfx {
class Device {
public:
    virtual ~Device() = default;
    [[nodiscard]] virtual const DeviceCaps& caps() const = 0;
    [[nodiscard]] virtual Result<BufferPtr> createBuffer(BufferDesc) = 0;
    [[nodiscard]] virtual Result<TexturePtr> createTexture(TextureDesc) = 0;
    [[nodiscard]] virtual Result<PipelinePtr> createGraphicsPipeline(const GraphicsPipelineDesc&) = 0;
    [[nodiscard]] virtual Result<std::unique_ptr<CommandList>> createCommandList() = 0;
    [[nodiscard]] virtual Result<std::unique_ptr<SwapChain>> createSwapChain(Surface, SwapChainDesc) = 0;
    [[nodiscard]] virtual CommandQueue& queue() = 0;
    [[nodiscard]] virtual Result<std::uint64_t> validationErrorCount() = 0;
};
[[nodiscard]] Result<std::unique_ptr<Device>> createDevice(DeviceDesc desc = {});
}
