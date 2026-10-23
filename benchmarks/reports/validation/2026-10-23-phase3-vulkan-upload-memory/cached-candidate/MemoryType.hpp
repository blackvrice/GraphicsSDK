#pragma once
#include <graphics/Types.hpp>
#include <cstdint>
#include <optional>
#include <span>

namespace gfx::vulkan {
struct BufferMemoryType { bool deviceLocal{}, hostVisible{}, coherent{}, cached{}; };
inline std::optional<std::uint32_t> selectBufferMemoryType(std::span<const BufferMemoryType> types,
    std::uint32_t compatible, Memory memory) {
    if (memory != Memory::Device && memory != Memory::Upload && memory != Memory::Readback) return {};
    std::optional<std::uint32_t> selected;
    unsigned best{};
    for (std::uint32_t i = 0; i < types.size() && i < 32; ++i) {
        const auto& t = types[i];
        if (!(compatible & (1u << i)) || (memory == Memory::Device ? !t.deviceLocal : !t.hostVisible)) continue;
        // Preserve Device and Readback choices. Upload prefers cached host writes
        // within each coherency tier, with a noncoherent fallback still available.
        const unsigned rank = memory == Memory::Device ? 0u :
            memory == Memory::Readback ? unsigned(t.coherent) : 2u * unsigned(t.coherent) + unsigned(t.cached);
        if (!selected || rank > best) { selected = i; best = rank; }
    }
    return selected;
}
}
