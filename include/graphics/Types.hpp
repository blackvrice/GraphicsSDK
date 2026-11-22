#pragma once
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

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
enum class Format {
    RGBA8UNorm,
    RGBA16Float,
    D32Float,
    BGRA8UNorm,
    BC1UNorm,
    BC2UNorm,
    BC3UNorm,
    BC4UNorm,
    BC5UNorm,
    BC6HUFloat,
    BC7UNorm,
    ETC2RGB8UNorm,
    ETC2RGBA8UNorm,
    EACR11UNorm,
    EACRG11UNorm,
    ASTC4x4UNorm,
    ASTC6x6UNorm,
    ASTC8x8UNorm,
    RGBA8SRGB,
    BGRA8SRGB,
    RGB10A2UNorm,
    R8UNorm,
    RG8UNorm,
    R16UNorm,
    RG16UNorm,
    NV12,
    P010
};
struct FormatBlockInfo {
    std::uint32_t blockWidth{1};
    std::uint32_t blockHeight{1};
    std::uint32_t bytesPerBlock{};
    bool compressed{};
    bool operator==(const FormatBlockInfo&) const = default;
};
enum class Memory { Device, Upload, Readback };
enum class MemorySegment { Local, NonLocal };
// OS process/adapter snapshot, not SDK allocation accounting or total/free VRAM.
// All amounts are bytes; usage may exceed budget. Success can contain zero values.
struct MemoryBudget {
    AdapterId adapterId;
    MemorySegment segment{MemorySegment::Local};
    std::uint64_t budgetBytes{}, usageBytes{}, availableForReservationBytes{}, reservationBytes{};
};
enum class ResourceState { Common, CopySource, CopyDestination, ShaderRead, RenderTarget, Present, VertexBuffer, IndexBuffer, VertexAndIndexBuffer, ConstantBuffer, DepthWrite, DepthRead, UnorderedAccess, ResolveSource, ResolveDestination, IndirectArgument };

// Standard GPU indirect command layouts shared across D3D12, Vulkan, and Metal.
struct DrawIndirectCommand {
    std::uint32_t vertexCount{};
    std::uint32_t instanceCount{};
    std::uint32_t firstVertex{};
    std::uint32_t firstInstance{};
};
struct DrawIndexedIndirectCommand {
    std::uint32_t indexCount{};
    std::uint32_t instanceCount{};
    std::uint32_t firstIndex{};
    std::int32_t baseVertex{};
    std::uint32_t firstInstance{};
};
struct DispatchIndirectCommand {
    std::uint32_t groupCountX{};
    std::uint32_t groupCountY{};
    std::uint32_t groupCountZ{};
};
static_assert(sizeof(DrawIndirectCommand) == 16);
static_assert(sizeof(DrawIndexedIndirectCommand) == 20);
static_assert(sizeof(DispatchIndirectCommand) == 12);
enum class IndexFormat { UInt16, UInt32 };
struct Color { float r{}, g{}, b{}, a{1}; };
// Active render-pass viewport in top-left pixel coordinates with depth bounds in [0, 1].
struct Viewport {
    float x{}, y{}, width{}, height{};
    float minDepth{0.0f}, maxDepth{1.0f};
    bool operator==(const Viewport&) const = default;
};
struct BufferDesc { std::uint64_t size{}; Memory memory{Memory::Device}; bool storage{}; bool shared{}; };
enum class TextureDimension { Texture2D = 0, Texture1D, Texture3D, Texture2DArray, TextureCube };
struct TextureDesc {
    std::uint32_t width{}, height{};
    Format format{Format::RGBA8UNorm};
    bool renderTarget{}, depthStencil{}, storage{};
    std::uint32_t sampleCount{1};
    TextureDimension dimension{TextureDimension::Texture2D};
    std::uint32_t depth{1};
    std::uint32_t mipLevels{1};
    std::uint32_t arrayLayers{1};
    bool shared{};
};
enum class TextureViewUsage { ShaderRead, RenderTarget, DepthStencil, Storage };
// Subresource view descriptor over a Texture. When format or dimension is unset,
// inherits the parent texture format/dimension (or Texture2D for single-slice RT/DS views).
struct TextureViewDesc {
    std::optional<Format> format;
    TextureViewUsage usage{TextureViewUsage::ShaderRead};
    std::uint32_t baseMipLevel{};
    std::uint32_t mipLevelCount{1};
    std::uint32_t baseArrayLayer{};
    std::uint32_t arrayLayerCount{1};
    std::optional<TextureDimension> dimension;
    std::uint32_t planeSlice{};
    bool operator==(const TextureViewDesc&) const = default;
};
struct TextureFootprint { std::uint64_t size{}; std::uint32_t rowPitch{}, rowBytes{}, height{}; };
// Portable buffer<->texture subresource copy region. bufferOffset must be a multiple of 512 bytes.
// bufferRowPitch=0 selects the 256-byte aligned pitch for width; non-zero must be a
// multiple of 256 bytes and >= width * bytesPerTexel. For 3D regions, depth slices are
// packed contiguously at slice stride rowPitch * height bytes.
struct TextureCopyRegion {
    std::uint64_t bufferOffset{};
    std::uint32_t bufferRowPitch{};
    std::uint32_t x{}, y{}, width{}, height{};
    std::uint32_t mipLevel{};
    std::uint32_t arrayLayer{};
    std::uint32_t z{};
    std::uint32_t depth{1};
    std::uint32_t planeSlice{};
};
struct Fence { std::uint64_t value{}, device{}; };
struct DeviceDesc {
    Backend backend{Backend::D3D12}; bool validation{}; bool softwareAdapter{}; bool enableDeviceLossTesting{};
    // Exact selection, including recovery. softwareAdapter must match the selected entry.
    std::optional<AdapterId> adapter;
    // Opt-in GPU-assisted validation (Vulkan GPU-AV / D3D12 GBV); requires validation=true.
    bool gpuAssistedValidation{};
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
    bool resourceAliasing{};
    bool residencyControl{};
    bool sparseBinding{};
    bool sparseResidencyTexture2D{};
    bool indirectDraw{};
    bool multiDrawIndirect{};
    bool descriptorIndexing{};
    bool meshShaders{};
    std::uint32_t maxDrawIndirectCount{};
    std::uint32_t maxBindlessDescriptors{};
    bool externalMemoryInterop{};
    bool externalFenceInterop{};
    bool gpuAssistedValidation{};
};

enum class MemoryHeapType { Buffers, Textures, RenderTargets };
struct MemoryHeapDesc {
    std::uint64_t size{};
    MemoryHeapType type{MemoryHeapType::Buffers};
    bool operator==(const MemoryHeapDesc&) const = default;
};
struct ResourceMemoryRequirements {
    std::uint64_t size{};
    std::uint64_t alignment{};
    MemoryHeapType heapType{MemoryHeapType::Buffers};
    bool operator==(const ResourceMemoryRequirements&) const = default;
};
struct HeapFragmentationStats {
    std::uint64_t totalBytes{};
    std::uint64_t allocatedBytes{};
    std::uint64_t virtualRequestedBytes{};
    std::uint64_t aliasedSavedBytes{};
    std::uint64_t freeBytes{};
    std::uint64_t largestFreeBlockBytes{};
    std::uint32_t liveAllocations{};
    std::uint32_t freeRegionCount{};
    double externalFragmentationRatio{};
    bool resident{true};
};
struct SparseTextureProperties {
    bool supported{};
    std::uint32_t tileWidth{};
    std::uint32_t tileHeight{};
    std::uint32_t tileDepth{};
    std::uint32_t standardTileBytes{};
    std::uint32_t totalTiles{};
    std::uint32_t packedMipStart{};
    std::uint32_t packedMipCount{};
    std::uint32_t packedMipTiles{};
};

inline constexpr std::uint32_t kMaxTimestampScopesPerCommandList = 32;

struct TimestampScopeResult {
    std::string name;
    QueueType queue{QueueType::Graphics};
    std::uint32_t depth{};
    std::uint64_t startTicks{};
    std::uint64_t endTicks{};
    // Real GPU hardware elapsed time in milliseconds; std::nullopt when unmeasured or unsupported.
    // Never synthesized from CPU wall time.
    std::optional<double> gpuMilliseconds;
};

struct ProfilingCounters {
    std::uint64_t drawCalls{};
    std::uint64_t indexedDrawCalls{};
    std::uint64_t instancedDrawCalls{};
    std::uint64_t dispatchCalls{};
    std::uint64_t renderPasses{};
    std::uint64_t copyCommands{};
    std::uint64_t barriers{};
    std::uint64_t uavBarriers{};
    std::uint64_t submissions{};
    std::uint64_t pipelineCacheHits{};
    std::uint64_t pipelineCacheMisses{};
    std::uint64_t queueWaits{};
    std::uint64_t timelineWaits{};
};

struct ProfilingSnapshot {
    bool enabled{};
    ProfilingCounters counters;
    // Cumulative CPU time spent blocked in CommandQueue::wait / TimelineFence::wait.
    // std::nullopt when profiling is disabled.
    std::optional<double> cpuStallMilliseconds;
    // OS adapter local VRAM snapshot when supported; std::nullopt when disabled or unsupported.
    std::optional<std::uint64_t> vramBudgetBytes;
    std::optional<std::uint64_t> vramUsageBytes;
    // Resolved GPU hardware timestamp scopes recorded while profiling is enabled.
    std::vector<TimestampScopeResult> gpuTimestampScopes;
};

inline constexpr std::uint32_t kMaxCrashBreadcrumbs = 128;

struct BreadcrumbEntry {
    std::uint64_t sequence{};
    QueueType queue{QueueType::Graphics};
    std::string label;
    std::uint64_t submittedFence{};
    bool completed{};
};

// Device-level diagnostic snapshot combining crash breadcrumbs and automatic resource leak detection.
// Safe to query even after DeviceLost to inspect the last uncompleted GPU checkpoints.
struct DiagnosticReport {
    std::uint64_t liveTrackedObjects{};
    std::uint64_t liveUniqueResources{};
    std::uint64_t liveAllocationBytes{};
    std::uint64_t pendingSubmissions{};
    bool hasLeaks{};
    bool deviceLost{};
    std::vector<BreadcrumbEntry> breadcrumbs;
    std::string summary;
};

enum class ExternalHandleType { OpaqueWin32Nt, D3D12Resource, D3D12Fence, OpaqueFd, MetalSharedEvent };

enum class NativeObjectType {
    D3D12Device,
    D3D12CommandQueue,
    D3D12Resource,
    D3D12Fence,
    VulkanInstance,
    VulkanPhysicalDevice,
    VulkanDevice,
    VulkanQueue,
    VulkanBuffer,
    VulkanImage,
    VulkanDeviceMemory,
    VulkanSemaphore,
    MetalDevice,
    MetalCommandQueue,
    MetalBuffer,
    MetalTexture,
    MetalSharedEvent
};

// Non-owning opaque handle to an underlying native GPU object.
// Valid only while the owning SDK object remains alive.
struct NativeHandle {
    Backend backend{Backend::D3D12};
    NativeObjectType type{NativeObjectType::D3D12Device};
    std::uintptr_t value{};
    explicit operator bool() const noexcept { return value != 0; }
    bool operator==(const NativeHandle&) const = default;
};

// Owning RAII wrapper for an OS-level exported external memory or timeline fence handle
// (Win32 NT HANDLE on Windows, POSIX fd on Linux). Closes the OS handle on destruction
// unless ownership is transferred via release(). Keeps OS headers out of public headers.
class ExternalHandle {
public:
    ExternalHandle() noexcept = default;
    ExternalHandle(ExternalHandleType type, std::uintptr_t rawHandle) noexcept
        : type_(type), handle_(rawHandle) {}
    ~ExternalHandle() noexcept { reset(); }
    ExternalHandle(const ExternalHandle&) = delete;
    ExternalHandle& operator=(const ExternalHandle&) = delete;
    ExternalHandle(ExternalHandle&& other) noexcept
        : type_(other.type_), handle_(other.handle_) { other.handle_ = 0; }
    ExternalHandle& operator=(ExternalHandle&& other) noexcept {
        if (this != &other) {
            reset();
            type_ = other.type_;
            handle_ = other.handle_;
            other.handle_ = 0;
        }
        return *this;
    }
    [[nodiscard]] ExternalHandleType type() const noexcept { return type_; }
    [[nodiscard]] std::uintptr_t raw() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept { return handle_ != 0; }
    [[nodiscard]] std::uintptr_t release() noexcept {
        const std::uintptr_t out = handle_;
        handle_ = 0;
        return out;
    }
    void reset() noexcept;
private:
    ExternalHandleType type_{ExternalHandleType::OpaqueWin32Nt};
    std::uintptr_t handle_{};
};

// Persistent shader/pipeline cache identity used to invalidate cached blobs across
// SDK version, backend, GPU adapter, or driver updates.
struct PipelineCacheFingerprint {
    std::string sdkVersion;
    Backend backend{Backend::D3D12};
    std::uint32_t vendorId{}, deviceId{};
    std::string adapterName;
    std::uint64_t driverFingerprint{};
    bool operator==(const PipelineCacheFingerprint&) const = default;
};

struct PipelineCacheLoadResult {
    bool loaded{};
    bool invalidated{};
    bool recoveredFromCorruption{};
    std::uint64_t bytesLoaded{};
};

struct ShaderDefine {
    std::string name;
    std::string value;
    bool operator==(const ShaderDefine&) const = default;
};

struct ShaderVariantKey {
    std::string name;
    std::vector<ShaderDefine> defines;
    std::uint64_t sourceHash{};
    bool operator==(const ShaderVariantKey&) const = default;
};

struct PipelinePrewarmStats {
    std::uint32_t requested{};
    std::uint32_t compiled{};
    std::uint32_t cacheHits{};
    std::uint32_t failed{};
    double elapsedMilliseconds{};
};
}
