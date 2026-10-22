#pragma once
#include "Types.hpp"
#include <string_view>
#include <vector>

namespace gfx {
[[nodiscard]] bool backendCompiled(Backend) noexcept;
[[nodiscard]] std::string_view buildFingerprint() noexcept;
// Fresh candidate metadata; device creation separately checks required feature support.
// D3D12: hardware in high-performance preference order, then WARP when available.
[[nodiscard]] Result<std::vector<AdapterInfo>> enumerateAdapters(Backend backend = Backend::D3D12);
inline constexpr std::string_view version = "0.0.1";
}
