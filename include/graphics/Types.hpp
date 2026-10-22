#pragma once
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>

namespace gfx {
enum class ErrorCode { InvalidArgument, InvalidState, WrongDevice, Unsupported, OutOfMemory, DeviceLost, Timeout, BackendFailure };
struct Error { ErrorCode code; std::string message; std::int64_t nativeCode{}; };
template<class T> using Result = std::expected<T, Error>;
enum class Backend { D3D12, Vulkan, Metal };
enum class Format { RGBA8UNorm, RGBA16Float, D32Float };
enum class Memory { Device, Upload, Readback };
enum class ResourceState { Common, CopySource, CopyDestination, ShaderRead, RenderTarget, Present, VertexBuffer, IndexBuffer, VertexAndIndexBuffer, ConstantBuffer, DepthWrite, DepthRead };
enum class IndexFormat { UInt16, UInt32 };
struct Color { float r{}, g{}, b{}, a{1}; };
struct BufferDesc { std::uint64_t size{}; Memory memory{Memory::Device}; };
struct TextureDesc { std::uint32_t width{}, height{}; Format format{Format::RGBA8UNorm}; bool renderTarget{}, depthStencil{}; };
struct TextureFootprint { std::uint64_t size{}; std::uint32_t rowPitch{}, rowBytes{}, height{}; };
struct Fence { std::uint64_t value{}, device{}; };
struct DeviceDesc { Backend backend{Backend::D3D12}; bool validation{}; bool softwareAdapter{}; bool enableDeviceLossTesting{}; };
// Diagnostic snapshot after pruning completed submissions. Allocation bytes describe
// unique SDK-held resource allocations, not residency, driver caches or total VRAM.
struct ResourceStatistics {
    std::uint64_t trackedObjects{}, uniqueResources{}, resourceAllocationBytes{};
    std::uint64_t pendingSubmissions{}, pendingObjectReferences{};
};
struct DeviceCaps {
    Backend backend{};
    std::string adapterName;
    bool softwareAdapter{}, timestampQueries{}, compute{}, swapChain{}, resourceBinding{};
    bool vertexInput{}, indexedDrawing{};
    bool bindingLayouts{};
    bool depthAttachment{}, multipleRenderTargets{};
    bool deviceRecovery{}, deviceLossInjection{};
};
}
