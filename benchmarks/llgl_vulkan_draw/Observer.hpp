#pragma once
#include <cstdint>
namespace drawobserver {
struct Snapshot {
    std::uint64_t calls{},instances{},vertices{},forwarded{},badArguments{};
    std::uint32_t minInstances{},maxInstances{},instanceApi{},createdInstances{},createdDevices{},liveInstances{},liveDevices{};
};
using Read=int(*)(Snapshot*,unsigned);
using Reset=void(*)();
}
