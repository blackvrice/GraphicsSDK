#pragma once
#include <graphics/Types.hpp>
#include <cmath>
#include <limits>

namespace gfx::vulkan {
inline bool timestampSupported(unsigned bits, double period) {
    return bits >= 36 && bits <= 64 && std::isfinite(period) && period > 0;
}
// hostNanoseconds bounds GPU execution since just before queue submission.
// Reject an ambiguous full counter cycle; cached results remain valid afterward.
inline Result<double> timestampMilliseconds(std::uint64_t start, std::uint64_t end, unsigned bits,
    double period, double hostNanoseconds, bool startWrittenFirst) {
    if (!timestampSupported(bits, period)) return std::unexpected(Error{ErrorCode::Unsupported, "Queue timestamp metadata unsupported"});
    if (!std::isfinite(hostNanoseconds) || hostNanoseconds < 0) return std::unexpected(Error{ErrorCode::InvalidArgument, "Invalid timestamp host bound"});
    if (!startWrittenFirst || hostNanoseconds >= std::ldexp(period, bits))
        return std::unexpected(Error{ErrorCode::InvalidState, "Timestamp order or counter-cycle bound is ambiguous"});
    const auto mask = bits == 64 ? std::numeric_limits<std::uint64_t>::max() : (std::uint64_t{1} << bits) - 1;
    const auto delta = ((end & mask) - (start & mask)) & mask;
    const auto milliseconds = static_cast<double>(delta) * (period / 1000000.0);
    if (!std::isfinite(milliseconds)) return std::unexpected(Error{ErrorCode::InvalidState, "Timestamp conversion overflow"});
    return milliseconds;
}
}
