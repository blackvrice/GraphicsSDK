#include "BufferPoolRange.hpp"
#include <graphics/Backend.hpp>

#include <iostream>
#include <stdexcept>
#include <string_view>

int main(int argc, char** argv) try {
    using gfx::vulkan::BufferSlots;
    unsigned checks{};
    const auto check = [&](bool condition, const char* message) {
        ++checks;
        if (!condition) throw std::runtime_error(message);
    };
    check(argc == 2 && gfx::buildFingerprint() == std::string_view{argv[1]}, "Wrong installed SDK fingerprint");
    check(BufferSlots::stride(65, 64) == 128, "Size class rounds up");
    check(BufferSlots::stride(1, 256) == 256, "Alignment can increase stride");
    check(BufferSlots::stride(4096, 4096) == 4096, "Maximum eligible size and alignment");
    check(BufferSlots::stride(4097, 64) == 0 && BufferSlots::stride(64, 8192) == 0,
          "Oversize or over-aligned requirements stay dedicated");
    check(BufferSlots::stride(64, 64, true) == 0, "Dedicated requirements bypass the pool");

    for (unsigned stride = 64; stride <= 4096; stride *= 2) {
        check(BufferSlots::nextCapacity(stride, 0) == 1, "Empty class starts with one slot");
        check(BufferSlots::nextCapacity(stride, 1) == 2 && BufferSlots::nextCapacity(stride, 2) == 4 &&
              BufferSlots::nextCapacity(stride, 4) == 4, "Live blocks grow up to four slots");
        check(BufferSlots::nextCapacity(stride, 8) == 0, "Reject stale capacity above the cap");
        for (const unsigned capacity : {0u, 3u, 8u, 16u}) {
            BufferSlots rejected(stride, capacity);
            check(rejected.capacity() == 0 && rejected.bytes() == 0 && !rejected.take() && !rejected.release(0),
                  "Constructor rejects invalid or oversize capacity");
        }

        BufferSlots block(stride, 4);
        check(block.bytes() == stride * 4 && block.bytes() <= BufferSlots::blockBytes, "Native block byte cap");
        for (unsigned slot = 0; slot < 4; ++slot) {
            check(block.take() == slot, "Allocate the first free slot");
        }
        check(!block.take() && !block.release(4), "Reject a fifth slot");
        check(block.release(1) && !block.release(1) && block.release(3), "Reject duplicate release");
        check(block.take() == 1 && block.take() == 3 && !block.take(), "Reuse holes in slot order");
        for (unsigned slot = 0; slot < 4; ++slot) {
            check(block.release(slot), "Release every live slot");
        }
        check(block.take() == 0, "Released block begins at slot zero");
    }
    std::cout << "PASS assertions=" << checks << " SDK_SHA256=" << gfx::buildFingerprint() << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
