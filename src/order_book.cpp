#include "tickmatch/order_book.hpp"

#include <algorithm>
#include <sstream>

namespace tickmatch {

OrderBook::OrderBook(std::size_t max_orders) : index_(max_orders) {
    pool_.resize(max_orders);
    // Thread the free list through `next`, lowest index first.
    for (std::size_t i = 0; i < max_orders; ++i) {
        pool_[i].next = (i + 1 < max_orders) ? static_cast<std::uint32_t>(i + 1) : kNil;
    }
    free_head_ = max_orders > 0 ? 0 : kNil;
    levels_[0].reserve(1024);
    levels_[1].reserve(1024);
}

Status OrderBook::submit(const OrderRequest& req, std::vector<Fill>& fills) {
    switch (req.type) {
        case OrderType::Limit:
            return add_limit(req.id, req.side, req.price, req.qty, fills);
        case OrderType::Market:
            return add_market(req.id, req.side, req.qty, fills);
        case OrderType::Cancel:
            return cancel(req.id);
        case OrderType::Reduce:
            return reduce(req.id, req.qty);
    }
    return Status::RejectInvalid;
}

Status OrderBook::add_limit(OrderId id, Side side, Price price, Qty qty, std::vector<Fill>& fills) {
    if (qty == 0) return Status::RejectInvalid;
    if (contains(id)) return Status::RejectDuplicateId;
    // Conservative: require a free slot up front so a partially filled order
    // can always rest its remainder.
    if (free_head_ == kNil) return Status::RejectInvalid;

    const Qty remaining = match(side, id, qty, true, price, fills);
    if (remaining == 0) return Status::Filled;

    auto& book = levels_[idx(side)];
    const Price k = key(side, price);
    // Most adds land at or near the touch, so scan from the back first.
    std::size_t pos = book.size();
    while (pos > 0 && key(side, book[pos - 1].price) > k) --pos;
    if (pos == 0 || book[pos - 1].price != price) {
        book.insert(book.begin() + static_cast<std::ptrdiff_t>(pos),
                    PriceLevel{price, 0, 0, kNil, kNil});
    } else {
        --pos;
    }
    PriceLevel& level = book[pos];

    const std::uint32_t slot = alloc_slot();
    Order& o = pool_[slot];
    o = Order{id, price, remaining, level.tail, kNil, side};
    if (level.tail != kNil) {
        pool_[level.tail].next = slot;
    } else {
        level.head = slot;
    }
    level.tail = slot;
    level.total += remaining;
    ++level.count;
    index_.insert(id, slot);
    return Status::Accepted;
}

Status OrderBook::add_market(OrderId id, Side side, Qty qty, std::vector<Fill>& fills) {
    if (qty == 0) return Status::RejectInvalid;
    const Qty remaining = match(side, id, qty, false, 0, fills);
    if (remaining == 0) return Status::Filled;
    return remaining == qty ? Status::Cancelled : Status::PartialCancelled;
}

Qty OrderBook::match(Side taker_side, OrderId taker_id, Qty qty, bool has_limit, Price limit,
                     std::vector<Fill>& fills) {
    auto& book = levels_[idx(opposite(taker_side))];
    while (qty > 0 && !book.empty()) {
        PriceLevel& level = book.back();
        if (has_limit) {
            const bool crosses =
                taker_side == Side::Buy ? level.price <= limit : level.price >= limit;
            if (!crosses) break;
        }
        while (qty > 0 && level.head != kNil) {
            const std::uint32_t slot = level.head;
            Order& o = pool_[slot];
            const Qty q = std::min(qty, o.qty);
            o.qty -= q;
            qty -= q;
            level.total -= q;
            const bool done = o.qty == 0;
            fills.push_back(Fill{o.id, taker_id, level.price, q, taker_side, done});
            if (done) {
                level.head = o.next;
                if (level.head != kNil) {
                    pool_[level.head].prev = kNil;
                } else {
                    level.tail = kNil;
                }
                --level.count;
                index_.erase(o.id);
                free_slot(slot);
            }
        }
        if (level.head == kNil) book.pop_back();
    }
    return qty;
}

Status OrderBook::cancel(OrderId id) {
    const std::uint32_t slot = index_.find(id);
    if (slot == kNil) return Status::RejectUnknownId;
    const Order& o = pool_[slot];
    const std::size_t pos = find_level(o.side, o.price);
    remove_order(o.side, pos, slot);
    return Status::Cancelled;
}

Status OrderBook::reduce(OrderId id, Qty qty) {
    const std::uint32_t slot = index_.find(id);
    if (slot == kNil) return Status::RejectUnknownId;
    Order& o = pool_[slot];
    if (qty == 0 || qty > o.qty) return Status::RejectInvalid;
    const std::size_t pos = find_level(o.side, o.price);
    if (qty == o.qty) {
        remove_order(o.side, pos, slot);
    } else {
        o.qty -= qty;
        levels_[idx(o.side)][pos].total -= qty;
    }
    return Status::Reduced;
}

std::size_t OrderBook::find_level(Side side, Price price) const noexcept {
    const auto& book = levels_[idx(side)];
    const Price k = key(side, price);
    auto it = std::lower_bound(book.begin(), book.end(), k, [side](const PriceLevel& l, Price kk) {
        return key(side, l.price) < kk;
    });
    if (it == book.end() || it->price != price) return book.size();
    return static_cast<std::size_t>(it - book.begin());
}

void OrderBook::unlink(PriceLevel& level, std::uint32_t slot) noexcept {
    const Order& o = pool_[slot];
    if (o.prev != kNil) {
        pool_[o.prev].next = o.next;
    } else {
        level.head = o.next;
    }
    if (o.next != kNil) {
        pool_[o.next].prev = o.prev;
    } else {
        level.tail = o.prev;
    }
}

void OrderBook::remove_order(Side side, std::size_t level_pos, std::uint32_t slot) noexcept {
    auto& book = levels_[idx(side)];
    PriceLevel& level = book[level_pos];
    unlink(level, slot);
    level.total -= pool_[slot].qty;
    --level.count;
    index_.erase(pool_[slot].id);
    free_slot(slot);
    if (level.head == kNil) book.erase(book.begin() + static_cast<std::ptrdiff_t>(level_pos));
}

std::uint32_t OrderBook::alloc_slot() noexcept {
    const std::uint32_t slot = free_head_;
    free_head_ = pool_[slot].next;
    return slot;
}

void OrderBook::free_slot(std::uint32_t slot) noexcept {
    pool_[slot].next = free_head_;
    free_head_ = slot;
}

std::optional<Price> OrderBook::best_bid() const noexcept {
    const auto& b = levels_[idx(Side::Buy)];
    if (b.empty()) return std::nullopt;
    return b.back().price;
}

std::optional<Price> OrderBook::best_ask() const noexcept {
    const auto& a = levels_[idx(Side::Sell)];
    if (a.empty()) return std::nullopt;
    return a.back().price;
}

std::uint64_t OrderBook::depth_at(Side side, Price price) const noexcept {
    const std::size_t pos = find_level(side, price);
    const auto& book = levels_[idx(side)];
    return pos == book.size() ? 0 : book[pos].total;
}

std::optional<OrderView> OrderBook::find(OrderId id) const noexcept {
    const std::uint32_t slot = index_.find(id);
    if (slot == kNil) return std::nullopt;
    const Order& o = pool_[slot];
    return OrderView{o.id, o.side, o.price, o.qty};
}

std::vector<LevelView> OrderBook::levels(Side side, std::size_t max_levels) const {
    const auto& book = levels_[idx(side)];
    std::vector<LevelView> out;
    for (auto it = book.rbegin(); it != book.rend() && out.size() < max_levels; ++it) {
        out.push_back(LevelView{it->price, it->total, it->count});
    }
    return out;
}

std::vector<OrderView> OrderBook::orders_at(Side side, Price price) const {
    std::vector<OrderView> out;
    const std::size_t pos = find_level(side, price);
    const auto& book = levels_[idx(side)];
    if (pos == book.size()) return out;
    for (std::uint32_t s = book[pos].head; s != kNil; s = pool_[s].next) {
        const Order& o = pool_[s];
        out.push_back(OrderView{o.id, o.side, o.price, o.qty});
    }
    return out;
}

std::string OrderBook::check_invariants() const {
    std::ostringstream err;
    std::size_t orders_seen = 0;
    for (Side side : {Side::Buy, Side::Sell}) {
        const auto& book = levels_[idx(side)];
        const char* name = side == Side::Buy ? "bid" : "ask";
        for (std::size_t i = 0; i < book.size(); ++i) {
            const PriceLevel& l = book[i];
            if (i > 0 && !(key(side, book[i - 1].price) < key(side, l.price))) {
                err << name << " levels not strictly sorted at " << l.price;
                return err.str();
            }
            if (l.head == kNil || l.count == 0) {
                err << name << " empty level at " << l.price;
                return err.str();
            }
            std::uint64_t total = 0;
            std::uint32_t count = 0;
            std::uint32_t prev = kNil;
            for (std::uint32_t s = l.head; s != kNil; s = pool_[s].next) {
                const Order& o = pool_[s];
                if (o.prev != prev) {
                    err << "broken prev link for order " << o.id;
                    return err.str();
                }
                if (o.price != l.price || o.side != side || o.qty == 0) {
                    err << "order " << o.id << " has bad price/side/qty";
                    return err.str();
                }
                if (index_.find(o.id) != s) {
                    err << "index mismatch for order " << o.id;
                    return err.str();
                }
                total += o.qty;
                ++count;
                prev = s;
                if (count > pool_.size()) {
                    err << "cycle in level " << l.price;
                    return err.str();
                }
            }
            if (prev != l.tail) {
                err << name << " tail mismatch at " << l.price;
                return err.str();
            }
            if (total != l.total || count != l.count) {
                err << name << " level " << l.price << " total/count mismatch: " << l.total << "/"
                    << l.count << " vs " << total << "/" << count;
                return err.str();
            }
            orders_seen += count;
        }
    }
    if (orders_seen != index_.size()) {
        err << "index holds " << index_.size() << " ids but levels hold " << orders_seen;
        return err.str();
    }
    const auto bb = best_bid();
    const auto ba = best_ask();
    if (bb && ba && *bb >= *ba) {
        err << "book crossed: bid " << *bb << " >= ask " << *ba;
        return err.str();
    }
    return {};
}

}  // namespace tickmatch
