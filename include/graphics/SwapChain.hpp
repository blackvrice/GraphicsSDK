#pragma once
#include "Resources.hpp"
#include "Surface.hpp"

namespace gfx {
class SwapChain {
public:
    virtual ~SwapChain() = default;
    // A retained buffer is usable only while this swapchain exists and that
    // buffer is current. Commands recorded before its next present become stale.
    [[nodiscard]] virtual Result<TexturePtr> acquire() = 0;
    [[nodiscard]] virtual Result<void> present() = 0;
    // Release acquired references first; resize waits for queued GPU work.
    [[nodiscard]] virtual Result<void> resize(std::uint32_t width, std::uint32_t height) = 0;
};
}
