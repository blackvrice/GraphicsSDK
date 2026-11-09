#include "RingRanges.hpp"
#include <array>
#include <iostream>
#include <limits>
#include <vector>

namespace {
using namespace gfx::upload_ring_experiment;
constexpr std::uint64_t device = 42;
unsigned checks{};

void check(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}

Range reserve(RingRanges& ring, std::uint64_t bytes) {
    const auto result = ring.reserve(bytes);
    check(result.has_value(), "Expected a staging reservation");
    return *result;
}

void reject(RingRanges& ring, std::uint64_t bytes, ReserveError error) {
    const auto result = ring.reserve(bytes);
    check(!result && result.error() == error, "Wrong reservation rejection");
}

void bounds() {
    for (const auto& [capacity, alignment, owner] : std::array{
             std::array{0u, 8u, 1u}, std::array{64u, 0u, 1u}, std::array{64u, 3u, 1u},
             std::array{63u, 8u, 1u}, std::array{64u, 8u, 0u}}) {
        bool rejected{};
        try { RingRanges invalid(capacity, alignment, owner); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "Invalid configuration accepted");
    }
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    RingRanges ring(max-7, 8, device);
    reject(ring, 0, ReserveError::invalidSize);
    reject(ring, max, ReserveError::exceedsCapacity);
    const auto all = reserve(ring, max-10);
    check(all.bytes == max-10 && all.reservedBytes == max-7 && all.offset == 0, "Rounding overflowed");
    reject(ring, 1, ReserveError::full);
    check(ring.cancel(all.ticket) && ring.empty(), "Full reservation did not release");
    const auto next = reserve(ring, 1);
    check(next.offset == 0 && next.reservedBytes == 8 && next.ticket != all.ticket, "Empty ring reset or serial");
    check(!ring.cancel(all.ticket) && ring.canWrite(next.ticket), "Old ticket affected a reused range");
}

void wrapAndOrder() {
    RingRanges ring(64, 8, device);
    const auto a = reserve(ring, 23);
    const auto b = reserve(ring, 16);
    const auto c = reserve(ring, 16);
    check(a.offset == 0 && b.offset == 24 && c.offset == 40, "Alignment or placement differs");
    check(ring.seal(b.ticket) && ring.seal(c.ticket), "Seal recordings");
    check(ring.submit(std::array{c.ticket}, {1, device}) && ring.complete({1, device}), "Later range completion");
    reject(ring, 16, ReserveError::full);
    check(ring.seal(a.ticket) && ring.submit(std::array{a.ticket}, {2, device}) &&
          ring.complete({2, device}), "Complete the earlier range");
    const auto wrapped = reserve(ring, 16);
    const auto boundary = reserve(ring, 8);
    check(wrapped.offset == 0 && boundary.offset == 16, "Contiguous wrap or exact fit differs");
    reject(ring, 8, ReserveError::full);
    check(ring.cancel(b.ticket) && !ring.canWrite(c.ticket), "Recorded head release or completed ticket");
    const auto after = reserve(ring, 40);
    check(after.offset == 24, "Reclaim the completed prefix after cancelling its head");
    check(ring.cancel(after.ticket), "Cancel tail");
    reject(ring, 8, ReserveError::full);
    check(ring.cancel(wrapped.ticket) && ring.cancel(boundary.ticket) && ring.empty(), "Cancelled prefix cleanup");
}

void submission() {
    RingRanges ring(32, 8, device);
    const auto a = reserve(ring, 16);
    const auto b = reserve(ring, 16);
    check(ring.canWrite(a.ticket) && ring.seal(a.ticket) && !ring.canWrite(a.ticket), "Seal closes writes");
    check(!ring.seal(a.ticket), "Duplicate seal accepted");
    check(!ring.submit(std::array{a.ticket, b.ticket}, {1, device}), "Unsealed batch accepted");
    check(ring.canWrite(b.ticket) && ring.seal(b.ticket), "Rejected batch changed another entry");
    check(!ring.submit(std::array{a.ticket, a.ticket}, {1, device}), "Duplicate ticket accepted");
    check(!ring.submit(std::array{a.ticket}, {1, device+1}), "Foreign fence accepted");
    check(!ring.submit(std::array{Ticket{device+1, a.ticket.serial}}, {1, device}), "Foreign ticket accepted");
    check(!ring.submit({}, {1, device}), "Empty submission accepted");
    const std::array both{a.ticket, b.ticket};
    check(ring.canSubmit(both, {2, device}), "Submission preflight rejected a sealed batch");
    // No commit on native failure. Completion of unrelated work cannot free the recording.
    check(ring.complete({1, device}) && ring.canSubmit(both, {2, device}), "Native failure lost retry ownership");
    reject(ring, 8, ReserveError::full);
    check(ring.submit(both, {2, device}), "Retry was not accepted");
    check(!ring.cancel(a.ticket) && !ring.submit(both, {3, device}), "Submitted range changed owner");
    check(!ring.complete({2, device+1}) && !ring.complete({0, device}), "Foreign or stale completion accepted");
    reject(ring, 8, ReserveError::full);
    check(ring.complete({2, device}) && ring.empty(), "Fence completion did not release batch");

    const auto live = reserve(ring, 32);
    check(ring.seal(live.ticket) && !ring.submit(std::array{live.ticket}, {2, device}), "Fence value reused");
    check(ring.submit(std::array{live.ticket}, {3, device}), "Pending retirement preparation");
    ring.retire();
    check(ring.empty() && !ring.cancel(live.ticket) && !ring.complete({3, device}), "Retired generation accepted work");
    reject(ring, 8, ReserveError::retired);
    RingRanges replacement(32, 8, device+1);
    const auto fresh = reserve(replacement, 32);
    check(fresh.offset == 0 && !replacement.seal(live.ticket) && replacement.canWrite(fresh.ticket),
          "Old ticket affected the new generation");
}

void stress(unsigned capacity, unsigned alignment, std::uint32_t seed) {
    struct Lease {
        Range range;
        std::uint64_t absolute;
        bool sealed{}, cancelled{};
        std::uint64_t fence{};
    };
    RingRanges ring(capacity, alignment, device);
    std::vector<Lease> leases;
    std::uint64_t cursor{}, submitted{}, completed{};
    const auto random = [&] {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return seed;
    };
    for (unsigned step = 0; step < 20000; ++step) {
        const unsigned op = random()%5;
        if (op == 0 || leases.empty()) {
            const std::uint64_t bytes = random()%(capacity+1)+1;
            const auto rounded = (bytes+alignment-1)/alignment*alignment;
            auto absolute = cursor;
            const auto physical = absolute%capacity;
            if (physical+rounded > capacity) absolute += capacity-physical;
            const bool fits = bytes <= capacity &&
                (leases.empty() || absolute+rounded-leases.front().absolute <= capacity);
            const auto result = ring.reserve(bytes);
            check(bool(result) == fits, "Absolute-position oracle disagrees on fit");
            if (result) {
                check(result->offset == absolute%capacity && result->bytes == bytes &&
                      result->reservedBytes == rounded, "Absolute-position oracle disagrees on placement");
                for (const auto& lease : leases)
                    check(lease.range.offset+lease.range.reservedBytes <= result->offset ||
                          result->offset+result->reservedBytes <= lease.range.offset, "Live reservations overlap");
                leases.push_back({*result, absolute});
                cursor = absolute+rounded;
            } else {
                check(result.error() == (bytes > capacity ? ReserveError::exceedsCapacity : ReserveError::full),
                      "Stress reservation error differs");
            }
        } else if (op == 4) {
            completed += random()%(submitted-completed+1);
            check(ring.complete({completed, device}), "Stress completion rejected");
        } else {
            auto& lease = leases[random()%leases.size()];
            const auto ticket = lease.range.ticket;
            if (op == 1) {
                const bool canSeal = !lease.sealed && !lease.cancelled;
                check(ring.seal(ticket) == canSeal, "Stress seal differs");
                lease.sealed |= canSeal;
            } else if (op == 2) {
                const bool canSubmit = lease.sealed && !lease.cancelled && !lease.fence;
                check(ring.submit(std::array{ticket}, {submitted+1, device}) == canSubmit, "Stress submit differs");
                if (canSubmit) lease.fence = ++submitted;
            } else {
                const bool canCancel = !lease.cancelled && !lease.fence;
                check(ring.cancel(ticket) == canCancel, "Stress cancellation differs");
                lease.cancelled |= canCancel;
            }
        }
        const auto firstLive = std::ranges::find_if(leases, [&](const auto& lease) {
            return !lease.cancelled && (!lease.fence || lease.fence > completed);
        });
        leases.erase(leases.begin(), firstLive);
        if (leases.empty()) cursor = 0;
        check(ring.empty() == leases.empty(), "Stress FIFO reclamation differs");
        for (const auto& lease : leases)
            check(ring.canWrite(lease.range.ticket) == (!lease.sealed && !lease.cancelled), "Stress write ownership differs");
    }
}
}

int main() try {
    bounds();
    wrapAndOrder();
    submission();
    for (unsigned capacity : {64u, 96u, 128u})
        for (unsigned alignment : {8u, 16u})
            stress(capacity, alignment, 0x42665u+capacity+alignment);
    std::cout << "PASS upload_ring_range_assertions=" << checks << " stress_steps=120000 GPU_calls=0\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
