#pragma once
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace gfx {
enum class ErrorCode { InvalidArgument, InvalidState, WrongDevice, Unsupported, OutOfMemory, DeviceLost, Timeout, BackendFailure };
struct Error { ErrorCode code; std::string message; std::int64_t nativeCode{}; };
template<class T> using Result = std::expected<T, Error>;
enum class Backend { D3D12, Vulkan, Metal };
enum class QueueType { Graphics, Compute, Copy };
// Backend-scoped opaque identity. D3D12 uses the OS LUID, not an enumeration index.
// Refresh after topology changes/reboot; it is not a persistent hardware serial number.
struct AdapterId { Backend backend{Backend::D3D12}; std::uint64_t value{}; bool operator==(const AdapterId&) const = default; };
struct AdapterInfo {
    AdapterId id;
    std::string name;
    bool softwareAdapter{};
    std::uint32_t vendorId{}, deviceId{};
    std::uint64_t dedicatedVideoMemory{}, sharedSystemMemory{};
};
enum class Format { RGBA8UNorm, RGBA16Float, D32Float, BGRA8UNorm };
enum class Memory { Device, Upload, Readback };
enum class MemorySegment { Local, NonLocal };
// OS process/adapter snapshot, not SDK allocation accounting or total/free VRAM.
// All amounts are bytes; usage may exceed budget. Success can contain zero values.
struct MemoryBudget {
    AdapterId adapterId;
    MemorySegment segment{MemorySegment::Local};
    std::uint64_t budgetBytes{}, usageBytes{}, availableForReservationBytes{}, reservationBytes{};
};
enum class ResourceState { Common, CopySource, CopyDestination, ShaderRead, RenderTarget, Present, VertexBuffer, IndexBuffer, VertexAndIndexBuffer, ConstantBuffer, DepthWrite, DepthRead, UnorderedAccess, ResolveSource, ResolveDestination };
enum class IndexFormat { UInt16, UInt32 };
struct Color { float r{}, g{}, b{}, a{1}; };
// Active render-pass viewport in top-left pixel coordinates with depth bounds in [0, 1].
struct Viewport {
    float x{}, y{}, width{}, height{};
    float minDepth{0.0f}, maxDepth{1.0f};
    bool operator==(const Viewport&) const = default;
};
struct BufferDesc { std::uint64_t size{}; Memory memory{Memory::Device}; bool storage{}; };
struct TextureDesc { std::uint32_t width{}, height{}; Format format{Format::RGBA8UNorm}; bool renderTarget{}, depthStencil{}, storage{}; std::uint32_t sampleCount{1}; };
enum class TextureViewUsage { ShaderRead, RenderTarget, DepthStencil, Storage };
// Subresource view descriptor over a Texture. When format is unset, inherits the parent texture format.
struct TextureViewDesc {
    std::optional<Format> format;
    TextureViewUsage usage{TextureViewUsage::ShaderRead};
    std::uint32_t baseMipLevel{};
    std::uint32_t mipLevelCount{1};
    std::uint32_t baseArrayLayer{};
    std::uint32_t arrayLayerCount{1};
    bool operator==(const TextureViewDesc&) const = default;
};
struct TextureFootprint { std::uint64_t size{}; std::uint32_t rowPitch{}, rowBytes{}, height{}; };
// Portable 2D buffer<->texture copy region. bufferOffset must be a multiple of 512 bytes.
// bufferRowPitch=0 selects the 256-byte aligned pitch for width; non-zero must be a
// multiple of 256 bytes and >= width * bytesPerTexel.
struct TextureCopyRegion {
    std::uint64_t bufferOffset{};
    std::uint32_t bufferRowPitch{};
    std::uint32_t x{}, y{}, width{}, height{};
};
struct Fence { std::uint64_t value{}, device{}; };
struct DeviceDesc {
    Backend backend{Backend::D3D12}; bool validation{}; bool softwareAdapter{}; bool enableDeviceLossTesting{};
    // Exact selection, including recovery. softwareAdapter must match the selected entry.
    std::optional<AdapterId> adapter;
};
// Diagnostic snapshot after pruning completed submissions. Allocation bytes describe
// unique SDK-held resource allocations, not residency, driver caches or total VRAM.
struct ResourceStatistics {
    std::uint64_t trackedObjects{}, uniqueResources{}, resourceAllocationBytes{};
    std::uint64_t pendingSubmissions{}, pendingObjectReferences{};
};
// Effective SDK and adapter format capability snapshot for one Format.
struct FormatSupport {
    Format format{Format::RGBA8UNorm};
    bool texture2D{};
    bool renderTarget{};
    bool depthStencil{};
    bool sampled{};
    bool linearFilter{};
    bool blendable{};
    bool copySource{};
    bool copyDestination{};
    bool partialRegionCopy{};
    bool storage{};
    // Bitmask of supported sample counts among 1, 2, 4, 8 for attachment creation with this format.
    std::uint32_t sampleCounts{1};
    bool operator==(const FormatSupport&) const = default;
};
struct DeviceCaps {
    Backend backend{};
    std::string adapterName;
    bool softwareAdapter{}, timestampQueries{}, compute{}, swapChain{}, resourceBinding{};
    bool vertexInput{}, indexedDrawing{};
    bool bindingLayouts{};
    bool depthAttachment{}, multipleRenderTargets{};
    bool deviceRecovery{}, deviceLossInjection{};
    AdapterId adapterId;
    // Effective adapter and SDK limits for the selected device.
    std::uint32_t maxTextureDimension2D{};
    std::uint32_t maxColorAttachments{};
    std::uint32_t constantBufferAlignment{};
    std::uint32_t textureCopyRowAlignment{};
    std::uint32_t textureCopyOffsetAlignment{};
    std::uint32_t maxVertexStreams{};
    std::uint32_t maxConstantBufferSize{};
    bool dedicatedComputeQueue{};
    bool dedicatedCopyQueue{};
    bool timelineFences{};
};
}
