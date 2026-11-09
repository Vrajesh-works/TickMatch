#pragma once

// LOBSTER message file support (https://lobsterdata.com/).
//
// Each line of a LOBSTER "message" file is:
//   Time,Type,OrderID,Size,Price,Direction
// Time is seconds after midnight with decimal precision, Price is dollars
// times 10000, Direction is 1 for a buy order and -1 for a sell order. For
// executions the direction is the side of the resting order that was hit.

#include <cstdint>
#include <optional>
#include <string_view>

#include "tickmatch/types.hpp"

namespace tickmatch::lobster {

enum class EventType : std::uint8_t {
    Submit = 1,          // new limit order
    PartialCancel = 2,   // cancel part of a resting order
    Delete = 3,          // cancel all of a resting order
    ExecuteVisible = 4,  // execution against a visible resting order
    ExecuteHidden = 5,   // execution against hidden liquidity (no book effect)
    Cross = 6,           // auction cross trade (no book effect)
    Halt = 7,            // trading halt indicator
};

struct Message {
    double time{0.0};
    EventType type{EventType::Submit};
    OrderId order_id{0};
    Qty size{0};
    Price price{0};
    Side side{Side::Buy};
};

// Parses one line. Returns nullopt for malformed lines (wrong field count,
// non-numeric fields, unknown type or direction). Trailing '\r' is accepted.
std::optional<Message> parse_line(std::string_view line) noexcept;

// Ids for synthetic taker orders (used for executions) live in the top half
// of the id space so they never collide with LOBSTER order ids.
inline constexpr OrderId kSyntheticIdBase = OrderId{1} << 63;

// Maps a LOBSTER message to an engine request:
//   1 Submit         -> limit order at the message price
//   2 PartialCancel  -> reduce by `size`
//   3 Delete         -> cancel
//   4 ExecuteVisible -> market order on the opposite side for `size`, which
//                       hits the resting order if the book state matches
//   5, 6, 7          -> nullopt (no effect on the visible book)
// `next_synthetic_id` is advanced for each execution.
std::optional<OrderRequest> to_request(const Message& m, OrderId& next_synthetic_id) noexcept;

}  // namespace tickmatch::lobster
