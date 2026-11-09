#pragma once
#include <cstdint>

namespace submitBaseline {
struct Counts {
    std::uint64_t pools{}, destroyedPools{}, commandBuffers{}, fences{}, destroyedFences{}, submissions{}, copies{};
    std::uint64_t ready{}, notReady{}, waits{}, idle{}, livePools{}, liveFences{}, errors{};
};
using Query = int (*)(Counts*, unsigned);
}
