#include "Timestamp.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

int main() try {
    unsigned assertions{}; auto check = [&](bool value) { ++assertions; if (!value) throw std::runtime_error("Vulkan timestamp conversion contract"); };
    using namespace gfx;
    for (auto bits : {0u, 1u, 35u, 65u, 255u}) check(!vulkan::timestampSupported(bits, 1));
    for (auto p : {0., -1., std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) check(!vulkan::timestampSupported(64, p));
    for (auto bits : {36u, 40u, 48u, 64u}) {
        check(vulkan::timestampSupported(bits, .5));
        const auto mask = UINT64_MAX >> (64 - bits);
        for (auto period : {.5, 1., 10.}) {
            for (auto start : {std::uint64_t{0}, std::uint64_t{1234567}, mask - 4}) for (auto delta : {0ull, 1ull, 10ull, 999999ull}) {
                auto r = vulkan::timestampMilliseconds(start, (start + delta) & mask, bits, period, 100000000, true);
                check(r && std::abs(*r - double(delta) * period / 1000000.) < 1e-12);
            }
            std::uint64_t seed = 0x123456789abcdef0ull;
            for (unsigned i = 0; i < 300; ++i) {
                seed = seed * 6364136223846793005ull + 1; auto start = seed & mask;
                auto delta = (seed >> 17) % 1000000;
                auto r = vulkan::timestampMilliseconds(start, (start + delta) & mask, bits, period, 100000000, true);
                check(r && std::abs(*r - double(delta) * period / 1000000.) < 1e-12);
            }
        }
        auto r = vulkan::timestampMilliseconds(mask - 4, 5, bits, 1, 100, true); check(r && std::abs(*r - .00001) < 1e-15);
        r = vulkan::timestampMilliseconds(0, 1, bits, 1, std::ldexp(1., bits), true); check(!r && r.error().code == ErrorCode::InvalidState);
        r = vulkan::timestampMilliseconds(10, 11, bits, 1, 100, false); check(!r && r.error().code == ErrorCode::InvalidState);
    }
    auto r = vulkan::timestampMilliseconds(0, 1, 0, 1, 100, true); check(!r && r.error().code == ErrorCode::Unsupported);
    for (auto host : {-1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        r = vulkan::timestampMilliseconds(0, 1, 64, 1, host, true); check(!r && r.error().code == ErrorCode::InvalidArgument);
    }
    r = vulkan::timestampMilliseconds((1ull << 60) | ((1ull << 36) - 5), 5, 36, 1, 100, true); check(r && std::abs(*r - .00001) < 1e-15);
    r = vulkan::timestampMilliseconds(0, UINT64_MAX, 64, std::numeric_limits<double>::max(), 100, true); check(!r && r.error().code == ErrorCode::InvalidState);
    std::cout << "PASS timestamp_math_assertions=" << assertions << " GPU_calls=0\n"; return 0;
} catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
