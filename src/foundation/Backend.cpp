#include <graphics/Graphics.hpp>

namespace gfx {
std::string_view buildFingerprint() noexcept { return GFX_SOURCE_SHA256; }
#ifdef GFX_HAS_D3D12
Result<std::unique_ptr<Device>> createD3D12Device(DeviceDesc);
Result<std::vector<AdapterInfo>> enumerateD3D12Adapters();
#endif
#ifdef GFX_HAS_VULKAN
Result<std::unique_ptr<Device>> createVulkanDevice(DeviceDesc);
Result<std::vector<AdapterInfo>> enumerateVulkanAdapters();
#endif
bool backendCompiled(Backend backend) noexcept {
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
#ifdef GFX_HAS_D3D12
    if (desc.backend == Backend::D3D12) return createD3D12Device(desc);
#endif
#ifdef GFX_HAS_VULKAN
    if (desc.backend == Backend::Vulkan) return createVulkanDevice(desc);
#endif
    return std::unexpected(Error{ErrorCode::Unsupported, "Requested graphics backend is not compiled"});
}
Result<std::vector<AdapterInfo>> enumerateAdapters(Backend backend) {
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
