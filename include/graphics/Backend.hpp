#pragma once
#include "Types.hpp"
#include <string_view>

namespace gfx {
[[nodiscard]] bool backendCompiled(Backend) noexcept;
[[nodiscard]] std::string_view buildFingerprint() noexcept;
inline constexpr std::string_view version = "0.0.1";
}
