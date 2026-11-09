#include <graphics/Graphics.hpp>
#include "DeviceGeneration.hpp"
#include <algorithm>
#include <list>

namespace gfx {
struct Device::GraphicsCache {
    struct Entry {
        GraphicsPipelineDesc desc;
        PipelinePtr pipeline;
    };
    std::list<Entry> entries;
};

namespace {
bool cacheableKey(const GraphicsPipelineDesc& desc) {
    // Bound copies of bytecode and semantic strings. Backend validation bounds layout counts.
    std::size_t available = 1024 * 1024;
    const auto charge = [&available](std::size_t bytes) {
        if (bytes > available) return false;
        available -= bytes;
        return true;
    };
    return charge(desc.vertex.bytes.size()) && charge(desc.fragment.bytes.size()) &&
           std::ranges::all_of(desc.vertexLayout.attributes, [&](const auto& attribute) {
               return charge(attribute.semantic.size());
           });
}
}

Device::Device() noexcept = default;
Device::~Device() = default;

Result<CachedGraphicsPipeline> Device::createCachedGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    if (auto status = checkStatus(); !status) return std::unexpected(status.error());
    if (graphicsCache) {
        auto& entries = graphicsCache->entries;
        const auto found = std::ranges::find(entries, desc, &GraphicsCache::Entry::desc);
        if (found != entries.end()) {
            entries.splice(entries.end(), entries, found);
            return CachedGraphicsPipeline{found->pipeline, true};
        }
    }
    auto pipeline = createGraphicsPipeline(desc);
    if (!pipeline) return std::unexpected(pipeline.error());
    if (cacheableKey(desc)) {
        if (!graphicsCache) graphicsCache = std::make_unique<GraphicsCache>();
        auto& entries = graphicsCache->entries;
        entries.push_back({desc, *pipeline});
        if (entries.size() > 8) entries.pop_front();
    }
    return CachedGraphicsPipeline{std::move(*pipeline), false};
}

Result<void> Device::clearGraphicsPipelineCache() {
    if (auto status = checkStatus(); !status) return status;
    graphicsCache.reset();
    return {};
}

std::uint64_t detail::allocateDeviceGeneration() noexcept {
    static GenerationCounter counter;
    return counter.next();
}
std::string_view buildFingerprint() noexcept { return GFX_SOURCE_SHA256; }
#ifdef GFX_HAS_METAL
Result<std::unique_ptr<Device>> createMetalDevice(DeviceDesc);
Result<std::vector<AdapterInfo>> enumerateMetalAdapters();
#endif
#ifdef GFX_HAS_D3D12
Result<std::unique_ptr<Device>> createD3D12Device(DeviceDesc);
Result<std::vector<AdapterInfo>> enumerateD3D12Adapters();
#endif
#ifdef GFX_HAS_VULKAN
Result<std::unique_ptr<Device>> createVulkanDevice(DeviceDesc);
Result<std::vector<AdapterInfo>> enumerateVulkanAdapters();
#endif
bool backendCompiled(Backend backend) noexcept {
#ifdef GFX_HAS_METAL
    if (backend == Backend::Metal) return true;
#endif
#ifdef GFX_HAS_D3D12
    if (backend == Backend::D3D12) return true;
#endif
#ifdef GFX_HAS_VULKAN
    if (backend == Backend::Vulkan) return true;
#endif
    (void)backend; return false;
}
Result<std::unique_ptr<Device>> createDevice(DeviceDesc desc) {
    if (desc.adapter && desc.adapter->backend!=desc.backend)
        return std::unexpected(Error{ErrorCode::InvalidArgument,"Adapter identity belongs to a different backend"});
#ifdef GFX_HAS_METAL
    if (desc.backend == Backend::Metal) return createMetalDevice(desc);
#endif
#ifdef GFX_HAS_D3D12
    if (desc.backend == Backend::D3D12) return createD3D12Device(desc);
#endif
#ifdef GFX_HAS_VULKAN
    if (desc.backend == Backend::Vulkan) return createVulkanDevice(desc);
#endif
    return std::unexpected(Error{ErrorCode::Unsupported, "Requested graphics backend is not compiled"});
}
Result<std::vector<AdapterInfo>> enumerateAdapters(Backend backend) {
#ifdef GFX_HAS_METAL
    if (backend == Backend::Metal) return enumerateMetalAdapters();
#endif
#ifdef GFX_HAS_D3D12
    if (backend==Backend::D3D12) return enumerateD3D12Adapters();
#else
    (void)backend;
#endif
#ifdef GFX_HAS_VULKAN
    if (backend == Backend::Vulkan) return enumerateVulkanAdapters();
#endif
    return std::unexpected(Error{ErrorCode::Unsupported,"Requested graphics backend is not compiled"});
}
}
