#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "tickmatch/id_index.hpp"
#include "tickmatch/types.hpp"

namespace tickmatch {

struct LevelView {
    Price price;
    std::uint64_t total_qty;
    std::uint32_t order_count;
};

struct OrderView {
    OrderId id;
    Side side;
    Price price;
    Qty qty;
};

// Single-instrument limit order book with price-time priority.
//
// Layout: each side keeps a contiguous vector of price levels sorted so the
// best price is at the back. Orders live in a preallocated pool and form an
// intrusive FIFO list per level linked by 32-bit indices. An open addressing
// index maps order ids to pool slots for O(1) cancel lookup.
//
// Not thread safe: one thread owns the book (the matching thread).
class OrderBook {
  public:
    explicit OrderBook(std::size_t max_orders = std::size_t{1} << 20);

    // Process one request. Fills are appended to `fills` (not cleared).
    Status submit(const OrderRequest& req, std::vector<Fill>& fills);

    Status add_limit(OrderId id, Side side, Price price, Qty qty, std::vector<Fill>& fills);
    Status add_market(OrderId id, Side side, Qty qty, std::vector<Fill>& fills);
    Status cancel(OrderId id);
    // Decrease a live order by `qty` (LOBSTER partial cancel). Keeps time
    // priority; reducing by the full remaining quantity removes the order.
    Status reduce(OrderId id, Qty qty);

    [[nodiscard]] std::optional<Price> best_bid() const noexcept;
    [[nodiscard]] std::optional<Price> best_ask() const noexcept;
    [[nodiscard]] std::uint64_t depth_at(Side side, Price price) const noexcept;
    [[nodiscard]] std::size_t order_count() const noexcept { return index_.size(); }
    [[nodiscard]] std::size_t level_count(Side side) const noexcept {
        return levels_[idx(side)].size();
    }
    [[nodiscard]] std::size_t capacity() const noexcept { return pool_.size(); }
    [[nodiscard]] bool contains(OrderId id) const noexcept {
        return index_.find(id) != IdIndex::kEmpty;
    }
    [[nodiscard]] std::optional<OrderView> find(OrderId id) const noexcept;

    // Levels best first, up to `max_levels`.
    [[nodiscard]] std::vector<LevelView> levels(Side side, std::size_t max_levels = SIZE_MAX) const;
    // Orders resting at a price in time priority.
    [[nodiscard]] std::vector<OrderView> orders_at(Side side, Price price) const;

    // Full structural check, used by tests and debug replays. Returns an empty
    // string when the book is consistent, otherwise a description of the defect.
    [[nodiscard]] std::string check_invariants() const;

  private:
    static constexpr std::uint32_t kNil = IdIndex::kEmpty;

    struct Order {
        OrderId id;
        Price price;
        Qty qty;
        std::uint32_t prev;
        std::uint32_t next;
        Side side;
    };

    struct PriceLevel {
        Price price;
        std::uint64_t total;
        std::uint32_t count;
        std::uint32_t head;
        std::uint32_t tail;
    };

    static constexpr std::size_t idx(Side s) noexcept { return static_cast<std::size_t>(s); }
    // Sort key: ascending key means worse to better, so the best level is at the back.
    static constexpr Price key(Side s, Price p) noexcept { return s == Side::Buy ? p : -p; }

    Qty match(Side taker_side, OrderId taker_id, Qty qty, bool has_limit, Price limit,
              std::vector<Fill>& fills);
    std::size_t find_level(Side side, Price price) const noexcept;  // returns size() if absent
    void unlink(PriceLevel& level, std::uint32_t slot) noexcept;
    void remove_order(Side side, std::size_t level_pos, std::uint32_t slot) noexcept;
    std::uint32_t alloc_slot() noexcept;
    void free_slot(std::uint32_t slot) noexcept;

    std::vector<Order> pool_;
    std::uint32_t free_head_{kNil};
    IdIndex index_;
    std::vector<PriceLevel> levels_[2];
};

}  // namespace tickmatch
