// Randomized differential test: every operation goes to both TickMatch and a
// simple reference book. Statuses and fills must match exactly, the book's
// structural invariants (including "never crossed") must hold after every
// step, and every order's quantity must be conserved:
//   submitted = filled + cancelled + resting.

#include <gtest/gtest.h>

#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "reference_book.hpp"
#include "tickmatch/order_book.hpp"

using namespace tickmatch;

namespace {

struct Ledger {
    std::uint64_t submitted{0};
    std::uint64_t filled{0};
    std::uint64_t cancelled{0};
};

bool same_fill(const Fill& a, const Fill& b) {
    return a.maker_id == b.maker_id && a.taker_id == b.taker_id && a.price == b.price &&
           a.qty == b.qty && a.taker_side == b.taker_side && a.maker_done == b.maker_done;
}

struct FuzzParams {
    std::uint64_t seed;
    std::size_t capacity;
    int steps;
};

void PrintTo(const FuzzParams& p, std::ostream* os) {
    *os << "seed=" << p.seed << " capacity=" << p.capacity << " steps=" << p.steps;
}

class FuzzTest : public ::testing::TestWithParam<FuzzParams> {};

TEST_P(FuzzTest, MatchesReferenceAndKeepsInvariants) {
    const auto [seed, capacity, steps] = GetParam();
    std::mt19937_64 rng(seed);
    OrderBook book(capacity);
    reference::ReferenceBook ref(capacity);
    std::unordered_map<OrderId, Ledger> ledger;
    std::vector<Fill> got;
    std::vector<Fill> want;
    OrderId next_id = 1;
    Price mid = 10'000;
    std::uint64_t total_fills = 0;

    auto pick_live = [&]() -> OrderId {
        const auto ids = ref.live_ids();
        if (ids.empty()) return next_id + 1'000'000;  // unknown id
        return ids[rng() % ids.size()];
    };

    for (int step = 0; step < steps; ++step) {
        if (rng() % 50 == 0) mid += static_cast<Price>(rng() % 7) - 3;  // drift
        const auto roll = rng() % 100;
        OrderRequest req;
        if (roll < 55) {
            const Side side = (rng() & 1) ? Side::Buy : Side::Sell;
            // Prices straddle the mid so a good share of limits cross.
            const Price px = mid + static_cast<Price>(rng() % 21) - 10;
            const Qty qty = static_cast<Qty>(rng() % 100) + (rng() % 40 == 0 ? 0 : 1);
            const OrderId id = (rng() % 50 == 0 && ref.size() > 0) ? pick_live() : next_id++;
            req = OrderRequest::limit(id, side, px, qty);
        } else if (roll < 65) {
            const Side side = (rng() & 1) ? Side::Buy : Side::Sell;
            req = OrderRequest::market(next_id++, side, static_cast<Qty>(rng() % 250) + 1);
        } else if (roll < 88) {
            req = OrderRequest::cancel(rng() % 20 == 0 ? next_id + 5'000'000 : pick_live());
        } else {
            const OrderId id = pick_live();
            const auto view = book.find(id);
            const Qty base = view ? view->qty : 10;
            // Mostly valid reductions, sometimes full or too large.
            const Qty qty = static_cast<Qty>(rng() % (base + 2));
            req = OrderRequest::reduce(id, qty);
        }

        got.clear();
        want.clear();
        const Status s_got = book.submit(req, got);
        const Status s_want = ref.submit(req, want);
        ASSERT_EQ(to_string(s_got), to_string(s_want)) << "step " << step << " seed " << seed;
        ASSERT_EQ(got.size(), want.size()) << "step " << step;
        for (std::size_t i = 0; i < got.size(); ++i) {
            ASSERT_TRUE(same_fill(got[i], want[i])) << "fill " << i << " at step " << step;
        }

        const std::string defect = book.check_invariants();
        ASSERT_EQ(defect, "") << "step " << step << " seed " << seed;
        ASSERT_EQ(book.order_count(), ref.size());

        // Quantity ledger.
        if (!is_reject(s_got)) {
            if (req.type == OrderType::Limit || req.type == OrderType::Market) {
                ledger[req.id].submitted += req.qty;
            }
            std::uint64_t taker_filled = 0;
            for (const Fill& f : got) {
                ledger[f.maker_id].filled += f.qty;
                taker_filled += f.qty;
            }
            ledger[req.id].filled += taker_filled;
            total_fills += got.size();
            if (req.type == OrderType::Market) {
                ledger[req.id].cancelled += req.qty - taker_filled;
            } else if (req.type == OrderType::Reduce) {
                ledger[req.id].cancelled += req.qty;
            }
        }
        if (req.type == OrderType::Cancel && s_got == Status::Cancelled) {
            const auto& l = ledger[req.id];
            ledger[req.id].cancelled += l.submitted - l.filled - l.cancelled;
        }
    }

    for (const auto& [id, l] : ledger) {
        const auto view = book.find(id);
        const std::uint64_t resting = view ? view->qty : 0;
        ASSERT_EQ(l.submitted, l.filled + l.cancelled + resting) << "order " << id;
    }
    // The stream must actually exercise matching, not just rest orders.
    EXPECT_GT(total_fills, static_cast<std::uint64_t>(steps) / 10);
    RecordProperty("fills", std::to_string(total_fills));
    RecordProperty("final_orders", std::to_string(book.order_count()));
}

INSTANTIATE_TEST_SUITE_P(Seeds, FuzzTest,
                         ::testing::Values(FuzzParams{1, 1 << 16, 50'000},
                                           FuzzParams{2, 1 << 16, 50'000},
                                           FuzzParams{3, 1 << 16, 50'000},
                                           FuzzParams{4, 1 << 16, 50'000},
                                           // Tiny pool: exercises capacity rejects.
                                           FuzzParams{5, 8, 20'000}, FuzzParams{6, 32, 20'000}),
                         [](const ::testing::TestParamInfo<FuzzParams>& param_info) {
                             return "seed" + std::to_string(param_info.param.seed) + "_cap" +
                                    std::to_string(param_info.param.capacity);
                         });

}  // namespace
