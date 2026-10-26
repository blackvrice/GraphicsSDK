#pragma once
#include <cstdint>
namespace markerprobe {
struct Label { std::uint64_t command{}; char text[256]{}; };
struct Snapshot {
    std::uint32_t calls{}, forwarded{}, errors{}, lookups{}, liveInstances{}, liveDevices{}, debugInstances{}, duplicateExtensions{};
    Label labels[32]{};
};
using Query = int (*)(Snapshot*, unsigned);
using Configure = void (*)(unsigned);
}
