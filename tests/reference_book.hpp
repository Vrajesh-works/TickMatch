#pragma once

// Deliberately simple reference order book used by the differential fuzz
// test. It favours obviously-correct code (std::map of std::deque) over speed.

#include <algorithm>
#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

#include "tickmatch/types.hpp"

namespace tickmatch::reference {

class ReferenceBook {
  public:
    explicit ReferenceBook(std::size_t capacity) : capacity_(capacity) {}

    Status submit(const OrderRequest& r, std::vector<Fill>& fills) {
        switch (r.type) {
            case OrderType::Limit:
                return limit(r, fills);
            case OrderType::Market:
                return market(r, fills);
            case OrderType::Cancel:
                return cancel(r.id);
            case OrderType::Reduce:
                return reduce(r.id, r.qty);
        }
        return Status::RejectInvalid;
    }

    [[nodiscard]] std::size_t size() const { return live_.size(); }
    [[nodiscard]] std::vector<OrderId> live_ids() const {
        std::vector<OrderId> ids;
        ids.reserve(live_.size());
        for (const auto& [id, _] : live_) ids.push_back(id);
        std::sort(ids.begin(), ids.end());
        return ids;
    }

  private:
    struct Resting {
        OrderId id;
        Qty qty;
    };
    struct Loc {
        Side side;
        Price price;
    };

    template <typename Map>
    Qty sweep(Map& opp, const OrderRequest& r, bool has_limit, std::vector<Fill>& fills) {
        Qty qty = r.qty;
        while (qty > 0 && !opp.empty()) {
            auto it = opp.begin();
            const Price px = it->first;
            if (has_limit) {
                const bool crosses = r.side == Side::Buy ? px <= r.price : px >= r.price;
                if (!crosses) break;
            }
            auto& q = it->second;
            while (qty > 0 && !q.empty()) {
                Resting& m = q.front();
                const Qty f = std::min(qty, m.qty);
                m.qty -= f;
                qty -= f;
                fills.push_back(Fill{m.id, r.id, px, f, r.side, m.qty == 0});
                if (m.qty == 0) {
                    live_.erase(m.id);
                    q.pop_front();
                }
            }
            if (q.empty()) opp.erase(it);
        }
        return qty;
    }

    Qty sweep_opposite(const OrderRequest& r, bool has_limit, std::vector<Fill>& fills) {
        return r.side == Side::Buy ? sweep(asks_, r, has_limit, fills)
                                   : sweep(bids_, r, has_limit, fills);
    }

    Status limit(const OrderRequest& r, std::vector<Fill>& fills) {
        if (r.qty == 0) return Status::RejectInvalid;
        if (live_.count(r.id)) return Status::RejectDuplicateId;
        if (live_.size() >= capacity_) return Status::RejectInvalid;
        const Qty left = sweep_opposite(r, true, fills);
        if (left == 0) return Status::Filled;
        if (r.side == Side::Buy) {
            bids_[r.price].push_back({r.id, left});
        } else {
            asks_[r.price].push_back({r.id, left});
        }
        live_[r.id] = Loc{r.side, r.price};
        return Status::Accepted;
    }

    Status market(const OrderRequest& r, std::vector<Fill>& fills) {
        if (r.qty == 0) return Status::RejectInvalid;
        const Qty left = sweep_opposite(r, false, fills);
        if (left == 0) return Status::Filled;
        return left == r.qty ? Status::Cancelled : Status::PartialCancelled;
    }

    template <typename Map>
    static std::deque<Resting>::iterator locate(Map& side, Price px, OrderId id) {
        auto& q = side.at(px);
        return std::find_if(q.begin(), q.end(), [id](const Resting& o) { return o.id == id; });
    }

    template <typename Map>
    void erase_from(Map& side, Price px, OrderId id) {
        auto& q = side.at(px);
        q.erase(locate(side, px, id));
        if (q.empty()) side.erase(px);
    }

    void erase(OrderId id, const Loc& loc) {
        if (loc.side == Side::Buy) {
            erase_from(bids_, loc.price, id);
        } else {
            erase_from(asks_, loc.price, id);
        }
        live_.erase(id);
    }

    Status cancel(OrderId id) {
        auto it = live_.find(id);
        if (it == live_.end()) return Status::RejectUnknownId;
        erase(id, it->second);
        return Status::Cancelled;
    }

    Status reduce(OrderId id, Qty qty) {
        auto it = live_.find(id);
        if (it == live_.end()) return Status::RejectUnknownId;
        const Loc loc = it->second;
        auto o =
            loc.side == Side::Buy ? locate(bids_, loc.price, id) : locate(asks_, loc.price, id);
        if (qty == 0 || qty > o->qty) return Status::RejectInvalid;
        if (qty == o->qty) {
            erase(id, loc);
        } else {
            o->qty -= qty;
        }
        return Status::Reduced;
    }

    std::size_t capacity_;
    std::map<Price, std::deque<Resting>, std::greater<>> bids_;  // best (highest) first
    std::map<Price, std::deque<Resting>, std::less<>> asks_;     // best (lowest) first
    std::unordered_map<OrderId, Loc> live_;
};

}  // namespace tickmatch::reference
