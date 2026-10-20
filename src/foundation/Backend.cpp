#include <graphics/Graphics.hpp>

namespace gfx {
std::string_view buildFingerprint() noexcept { return GFX_SOURCE_SHA256; }
#ifdef GFX_HAS_D3D12
Result<std::unique_ptr<Device>> createD3D12Device(DeviceDesc);
#endif
bool backendCompiled(Backend backend) noexcept {
#ifdef GFX_HAS_D3D12
    return backend == Backend::D3D12;
#else
    (void)backend; return false;
#endif
}
Result<std::unique_ptr<Device>> createDevice(DeviceDesc desc) {
#ifdef GFX_HAS_D3D12
    if (desc.backend == Backend::D3D12) return createD3D12Device(desc);
#endif
    return std::unexpected(Error{ErrorCode::Unsupported, "Requested graphics backend is not compiled"});
}
}
