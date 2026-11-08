#pragma once

#include <cstdint>
#include <string_view>

namespace tickmatch {

using OrderId = std::uint64_t;
using Price = std::int64_t;  // integer ticks (LOBSTER: dollars x 10000)
using Qty = std::uint32_t;

enum class Side : std::uint8_t { Buy = 0, Sell = 1 };

constexpr Side opposite(Side s) noexcept {
    return s == Side::Buy ? Side::Sell : Side::Buy;
}

enum class OrderType : std::uint8_t {
    Limit = 0,   // good till cancel, remainder rests
    Market = 1,  // immediate, remainder cancelled
    Cancel = 2,  // remove a live order
    Reduce = 3,  // decrease a live order's quantity, keeps priority
};

struct OrderRequest {
    OrderType type{OrderType::Limit};
    Side side{Side::Buy};
    OrderId id{0};
    Price price{0};
    Qty qty{0};

    static constexpr OrderRequest limit(OrderId id, Side side, Price price, Qty qty) noexcept {
        return {OrderType::Limit, side, id, price, qty};
    }
    static constexpr OrderRequest market(OrderId id, Side side, Qty qty) noexcept {
        return {OrderType::Market, side, id, 0, qty};
    }
    static constexpr OrderRequest cancel(OrderId id) noexcept {
        return {OrderType::Cancel, Side::Buy, id, 0, 0};
    }
    static constexpr OrderRequest reduce(OrderId id, Qty qty) noexcept {
        return {OrderType::Reduce, Side::Buy, id, 0, qty};
    }
};

struct Fill {
    OrderId maker_id;
    OrderId taker_id;
    Price price;
    Qty qty;
    Side taker_side;
    bool maker_done;  // the resting order was fully filled and removed
};

enum class Status : std::uint8_t {
    Accepted = 0,      // limit rested (possibly after partial fills)
    Filled,            // limit or market fully filled
    PartialCancelled,  // market partially filled, remainder cancelled
    Cancelled,         // cancel succeeded, or market found no liquidity
    Reduced,           // reduce succeeded
    RejectDuplicateId,
    RejectUnknownId,
    RejectInvalid,  // zero quantity, bad reduce amount, pool exhausted
};

constexpr bool is_reject(Status s) noexcept {
    return s == Status::RejectDuplicateId || s == Status::RejectUnknownId ||
           s == Status::RejectInvalid;
}

constexpr std::string_view to_string(Status s) noexcept {
    switch (s) {
        case Status::Accepted:
            return "accepted";
        case Status::Filled:
            return "filled";
        case Status::PartialCancelled:
            return "partial_cancelled";
        case Status::Cancelled:
            return "cancelled";
        case Status::Reduced:
            return "reduced";
        case Status::RejectDuplicateId:
            return "reject_duplicate_id";
        case Status::RejectUnknownId:
            return "reject_unknown_id";
        case Status::RejectInvalid:
            return "reject_invalid";
    }
    return "unknown";
}

}  // namespace tickmatch
