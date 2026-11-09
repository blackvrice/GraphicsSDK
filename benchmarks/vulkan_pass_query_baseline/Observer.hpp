#pragma once
#include <cstdint>

namespace passQuery {
struct Objects {
    std::uint64_t created{}, destroyed{}, live{};
    bool operator==(const Objects&) const = default;
};
struct Counts {
    Objects queries, passes, framebuffers;
    std::uint64_t resets{}, writes{}, reads{}, errors{};
    bool operator==(const Counts&) const = default;
};
using Query = int (*)(Counts*, unsigned);
}
