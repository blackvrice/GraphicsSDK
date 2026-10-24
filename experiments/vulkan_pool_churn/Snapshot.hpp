#pragma once
#include <cstdint>
namespace churn {
struct Entry {
    std::uint64_t buffer{},memory{},offset{},required{},alignment{},allocation{};
    unsigned type{},typeBits{},dedicated{};
    bool operator==(const Entry&)const=default;
};
inline constexpr unsigned limit=2048;
struct Snapshot {std::uint64_t memories{},bytes{},buffers{},images{};unsigned count{};Entry entries[limit]{};};
}
