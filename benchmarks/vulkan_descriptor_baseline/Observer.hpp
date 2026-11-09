#pragma once
#include <cstdint>

namespace descriptorBaseline {
struct Counts {
    std::uint64_t create{}, allocate{}, update{}, bind{}, destroy{}, live{}, errors{};
    bool operator==(const Counts&) const = default;
};
using Query = int (*)(Counts*, unsigned);
}
