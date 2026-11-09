#pragma once

#include <graphics/Types.hpp>
#include <algorithm>
#include <bit>
#include <cstdint>
#include <deque>
#include <expected>
#include <span>
#include <stdexcept>

namespace gfx::upload_ring_experiment {

struct Ticket {
    std::uint64_t device{}, serial{};
    bool operator==(const Ticket&) const = default;
};

struct Range {
    Ticket ticket;
    std::uint64_t offset{}, bytes{}, reservedBytes{};
};

enum class ReserveError { invalidSize, exceedsCapacity, full, exhausted, retired };

// CPU policy for one staging ring per device generation. The caller owns the
// backing bytes, command recording, and confirmation of native fence progress.
class RingRanges {
public:
    RingRanges(std::uint64_t capacity, std::uint64_t alignment, std::uint64_t device)
        : capacity_(capacity), alignment_(alignment), device_(device) {
        if (!device || !capacity || !std::has_single_bit(alignment) || capacity % alignment)
            throw std::invalid_argument("Invalid staging ring configuration");
    }

    RingRanges(const RingRanges&) = delete;
    RingRanges& operator=(const RingRanges&) = delete;

    [[nodiscard]] std::expected<Range, ReserveError> reserve(std::uint64_t bytes) {
        if (retired_) return std::unexpected(ReserveError::retired);
        if (!bytes) return std::unexpected(ReserveError::invalidSize);
        if (bytes > capacity_) return std::unexpected(ReserveError::exceedsCapacity);
        if (!nextSerial_) return std::unexpected(ReserveError::exhausted);

        // Capacity is a multiple of alignment, so the rounded size stays in bounds.
        const auto reserved = ((bytes-1)/alignment_+1)*alignment_;
        auto offset = write_;
        if (!entries_.empty()) {
            const auto first = entries_.front().range.offset;
            if (write_ == first) return std::unexpected(ReserveError::full);
            if (write_ < first) {
                if (reserved > first-write_) return std::unexpected(ReserveError::full);
            } else if (reserved > capacity_-write_) {
                if (reserved > first) return std::unexpected(ReserveError::full);
                offset = 0;
            }
        }
        const Range range{{device_, nextSerial_}, offset, bytes, reserved};
        entries_.push_back({range, Status::writable, 0});
        write_ = offset+reserved;
        ++nextSerial_;
        return range;
    }

    [[nodiscard]] bool canWrite(Ticket ticket) const {
        const auto it = std::ranges::find(entries_, ticket, &Entry::ticket);
        return it != entries_.end() && it->status == Status::writable;
    }

    [[nodiscard]] bool seal(Ticket ticket) {
        const auto it = std::ranges::find(entries_, ticket, &Entry::ticket);
        if (it == entries_.end() || it->status != Status::writable) return false;
        it->status = Status::recorded;
        return true;
    }

    [[nodiscard]] bool cancel(Ticket ticket) {
        const auto it = std::ranges::find(entries_, ticket, &Entry::ticket);
        if (it == entries_.end() || (it->status != Status::writable && it->status != Status::recorded))
            return false;
        it->status = Status::cancelled;
        reclaim();
        return true;
    }

    // Check the whole batch before native submission. A failed native submission
    // leaves these sealed tickets with the recording for retry or cancellation.
    [[nodiscard]] bool canSubmit(std::span<const Ticket> tickets, Fence fence) const {
        if (retired_ || fence.device != device_ || fence.value <= lastSubmission_ ||
            fence.value <= completed_ || tickets.empty()) return false;
        for (std::size_t i = 0; i < tickets.size(); ++i) {
            const auto prior = tickets.first(i);
            if (std::ranges::find(prior, tickets[i]) != prior.end()) return false;
            const auto it = std::ranges::find(entries_, tickets[i], &Entry::ticket);
            if (it == entries_.end() || it->status != Status::recorded) return false;
        }
        return true;
    }

    [[nodiscard]] bool submit(std::span<const Ticket> tickets, Fence fence) {
        if (!canSubmit(tickets, fence)) return false;
        for (auto ticket : tickets) {
            auto it = std::ranges::find(entries_, ticket, &Entry::ticket);
            it->status = Status::submitted;
            it->fence = fence.value;
        }
        lastSubmission_ = fence.value;
        return true;
    }

    [[nodiscard]] bool complete(Fence fence) {
        if (retired_ || fence.device != device_ || fence.value < completed_) return false;
        completed_ = fence.value;
        reclaim();
        return true;
    }

    // Call only once the caller has retired the native backing and its users.
    void retire() {
        retired_ = true;
        entries_.clear();
        write_ = 0;
    }

    [[nodiscard]] bool empty() const { return entries_.empty(); }

private:
    enum class Status { writable, recorded, submitted, cancelled };
    struct Entry {
        Range range;
        Status status;
        std::uint64_t fence;
        [[nodiscard]] Ticket ticket() const { return range.ticket; }
    };

    void reclaim() {
        while (!entries_.empty()) {
            const auto& first = entries_.front();
            if (first.status != Status::cancelled &&
                !(first.status == Status::submitted && first.fence <= completed_)) break;
            entries_.pop_front();
        }
        if (entries_.empty()) write_ = 0;
    }

    std::uint64_t capacity_, alignment_, device_;
    std::uint64_t write_{}, nextSerial_{1}, lastSubmission_{}, completed_{};
    std::deque<Entry> entries_;
    bool retired_{};
};

} // namespace gfx::upload_ring_experiment
