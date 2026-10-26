#pragma once
#include <cstdint>
namespace eventprobe {
enum Operation : unsigned { ClearBoundary=0, AcquireLost=1, PresentLost=2, QueryLost=3, ClosedBoundary=4 };
struct Snapshot {std::uint64_t acquireLost{},presentLost{},queryLost{},queries{},afterClose{};};
using Control=int(*)(unsigned);
using Query=int(*)(Snapshot*,unsigned);
}
