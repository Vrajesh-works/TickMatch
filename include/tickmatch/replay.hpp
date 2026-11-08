#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "tickmatch/latency_histogram.hpp"
#include "tickmatch/types.hpp"

namespace tickmatch {

enum class ReplayMode : std::uint8_t {
    Inline,    // parse everything first, then match on one thread
    Pipeline,  // ingest thread parses into an SPSC queue, matching thread drains it
};

struct ReplayOptions {
    ReplayMode mode{ReplayMode::Pipeline};
    std::size_t book_capacity{std::size_t{1} << 20};
    std::size_t queue_capacity{1 << 16};
    std::size_t check_every{0};  // run full invariant checks every N requests (0: only at end)
    // Pipeline only: pace the ingest thread to this many messages per second
    // (0: as fast as possible, so e2e latency then includes queue backlog).
    std::size_t rate{0};
};

struct ReplayStats {
    // Input
    std::uint64_t lines{0};
    std::uint64_t malformed{0};
    std::uint64_t skipped{0};  // valid messages with no visible book effect (types 5, 6, 7)
    // Requests by type
    std::uint64_t requests{0};
    std::uint64_t limits{0};
    std::uint64_t markets{0};
    std::uint64_t cancels{0};
    std::uint64_t reduces{0};
    // Outcomes
    std::uint64_t fills{0};
    std::uint64_t volume{0};
    std::uint64_t reject_unknown_id{0};
    std::uint64_t reject_duplicate_id{0};
    std::uint64_t reject_invalid{0};
    // LOBSTER executions whose replayed fills hit exactly the named resting order
    // for exactly the reported size.
    std::uint64_t executions{0};
    std::uint64_t executions_matched{0};
    // Final state
    std::uint64_t resting_orders{0};
    std::optional<Price> best_bid;
    std::optional<Price> best_ask;
    bool invariants_ok{true};
    std::string invariant_error;
    // Timing
    double wall_seconds{0.0};
    std::uint64_t timer_overhead_ns{0};  // median cost of one now_ns() pair
    LatencyHistogram book_latency;       // time inside OrderBook::submit
    LatencyHistogram e2e_latency;        // pipeline: enqueue on ingest thread to match done

    [[nodiscard]] double throughput() const noexcept {
        return wall_seconds > 0 ? static_cast<double>(requests) / wall_seconds : 0.0;
    }
};

// Replays the contents of a LOBSTER message file held in memory.
ReplayStats replay_lobster(std::string_view text, const ReplayOptions& options);

std::string to_json(const ReplayStats& stats, ReplayMode mode);
std::string_view to_string(ReplayMode mode) noexcept;

}  // namespace tickmatch
