#pragma once
#include <cstdint>

namespace gfx {
enum class SurfaceKind { Win32, Xcb, MetalLayer };
// Win32 window is an HWND cast to void*. No platform header enters the public API.
// Xcb uses connection (xcb_connection_t*) and windowId; window must be null.
// MetalLayer uses a CAMetalLayer* bridged to void* in window. Use it on the macOS main thread.
// Caller owns all native handles and keeps them alive through SDK surface use.
struct Surface {
    SurfaceKind kind{SurfaceKind::Win32}; void* window{};
    void* connection{}; std::uint32_t windowId{};
};
struct SwapChainDesc { std::uint32_t width{}, height{}, buffers{2}; bool vsync{}; };
}
