#pragma once
#include <atomic>
#include <cstdint>

namespace gfx::detail {
// One namespace for every backend in a linked SDK image. Never wrap/reuse a fence owner.
class GenerationCounter {
    std::atomic<std::uint64_t> next_;
public:
    explicit GenerationCounter(std::uint64_t initial = 1) noexcept : next_(initial) {}
    [[nodiscard]] std::uint64_t next() noexcept {
        auto value = next_.load(std::memory_order_relaxed);
        do {
            if (!value || value == UINT64_MAX) return 0;
        } while (!next_.compare_exchange_weak(value, value + 1, std::memory_order_relaxed));
        return value;
    }
};
[[nodiscard]] std::uint64_t allocateDeviceGeneration() noexcept;
}
