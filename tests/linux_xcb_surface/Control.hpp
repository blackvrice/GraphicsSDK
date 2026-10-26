#pragma once
#include <cstdint>
namespace surfaceprobe {
struct Snapshot {
    std::uint64_t instances{}, destroyedInstances{}, xcbEnabled{}, surfaces{}, destroyedSurfaces{}, surfaceQueries{}, visualQueries{}, nativeSurfaceTrue{}, nativeVisualTrue{};
    std::uint32_t family{};
    std::uint64_t devices{}, destroyedDevices{}, chains{}, destroyedChains{}, acquires{}, presents{}, fencedPresents{}, waitedPresents{};
    std::uint32_t format{}, presentMode{}, minImages{}, actualImages{};
};
using Query=int(*)(Snapshot*,unsigned);
}
