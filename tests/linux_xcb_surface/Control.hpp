#pragma once
#include <cstdint>
namespace surfaceprobe {
struct Snapshot {
    std::uint64_t instances{}, destroyedInstances{}, xcbEnabled{}, surfaces{}, destroyedSurfaces{}, surfaceQueries{}, visualQueries{}, nativeSurfaceTrue{}, nativeVisualTrue{};
    std::uint32_t family{};
};
using Query=int(*)(Snapshot*,unsigned);
}
