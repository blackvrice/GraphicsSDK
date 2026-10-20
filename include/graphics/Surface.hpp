#pragma once
#include <cstdint>

namespace gfx {
enum class SurfaceKind { Win32 };
// Win32 window is an HWND cast to void*. No platform header enters the public API.
struct Surface { SurfaceKind kind{SurfaceKind::Win32}; void* window{}; };
struct SwapChainDesc { std::uint32_t width{}, height{}, buffers{2}; bool vsync{}; };
}
