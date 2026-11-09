#include <benchmark/benchmark.h>

#include <cstdint>
#include <memory>
#include <random>
#include <thread>
#include <vector>

#include "tickmatch/latency_histogram.hpp"
#include "tickmatch/order_book.hpp"
#include "tickmatch/spsc_queue.hpp"

using namespace tickmatch;

namespace {

constexpr Price kMid = 1'000'000;
constexpr Price kTick = 100;

// Resting book with `depth` orders per side spread over 50 price levels.
std::unique_ptr<OrderBook> make_book(std::size_t depth, OrderId& next_id) {
    auto book = std::make_unique<OrderBook>(depth * 2 + 1024);
    std::vector<Fill> fills;
    for (std::size_t i = 0; i < depth; ++i) {
        const Price off = static_cast<Price>(1 + i % 50) * kTick;
        book->add_limit(next_id++, Side::Buy, kMid - off, 100, fills);
        book->add_limit(next_id++, Side::Sell, kMid + off, 100, fills);
    }
    return book;
}

// Add a passive order then cancel it: the most common pair in real order flow.
void BM_AddCancelPassive(benchmark::State& state) {
    OrderId next_id = 1;
    auto book = make_book(static_cast<std::size_t>(state.range(0)), next_id);
    std::vector<Fill> fills;
    std::mt19937_64 rng(1);
    for (auto _ : state) {
        const Side side = (rng() & 1) ? Side::Buy : Side::Sell;
        const Price off = static_cast<Price>(1 + rng() % 50) * kTick;
        const Price px = side == Side::Buy ? kMid - off : kMid + off;
        const OrderId id = next_id++;
        benchmark::DoNotOptimize(book->add_limit(id, side, px, 100, fills));
        benchmark::DoNotOptimize(book->cancel(id));
    }
    state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_AddCancelPassive)->Arg(100)->Arg(10'000)->Arg(100'000);

// Aggressive order that fully fills the resting order at the touch, then a
// replenishing passive order so the book shape stays constant.
void BM_MatchAtTouch(benchmark::State& state) {
    OrderId next_id = 1;
    auto book = make_book(static_cast<std::size_t>(state.range(0)), next_id);
    std::vector<Fill> fills;
    fills.reserve(16);
    for (auto _ : state) {
        fills.clear();
        benchmark::DoNotOptimize(book->add_limit(next_id++, Side::Buy, kMid + kTick, 100, fills));
        benchmark::DoNotOptimize(book->add_limit(next_id++, Side::Sell, kMid + kTick, 100, fills));
    }
    state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_MatchAtTouch)->Arg(100)->Arg(10'000);

// Market order sweeping several levels, then the swept liquidity is restored.
void BM_SweepFiveLevels(benchmark::State& state) {
    OrderId next_id = 1;
    auto book = make_book(1'000, next_id);
    std::vector<Fill> fills;
    fills.reserve(256);
    for (auto _ : state) {
        fills.clear();
        // Levels 1..5 hold 20 orders of 100 each: sweep all of them.
        book->add_market(next_id++, Side::Buy, 5 * 20 * 100, fills);
        state.PauseTiming();
        for (int lvl = 1; lvl <= 5; ++lvl) {
            for (int k = 0; k < 20; ++k) {
                book->add_limit(next_id++, Side::Sell, kMid + lvl * kTick, 100, fills);
            }
        }
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SweepFiveLevels);

// Mixed stream: 55% limits around the mid (some cross), 30% cancels of live
// orders, 15% small market orders. Each batch replays the same stream on a
// fresh book.
struct MixedStream {
    std::vector<OrderRequest> reqs;
};

MixedStream make_stream(std::size_t n) {
    MixedStream s;
    s.reqs.reserve(n);
    std::mt19937_64 rng(42);
    std::vector<OrderId> live;
    OrderId next_id = 1;
    for (std::size_t i = 0; i < n; ++i) {
        const auto roll = rng() % 100;
        if (roll < 55 || live.empty()) {
            const Side side = (rng() & 1) ? Side::Buy : Side::Sell;
            const Price off = static_cast<Price>(rng() % 20) - 4;  // mostly passive
            const Price px = side == Side::Buy ? kMid - off * kTick : kMid + off * kTick;
            s.reqs.push_back(OrderRequest::limit(next_id, side, px, 100));
            live.push_back(next_id++);
        } else if (roll < 85) {
            const std::size_t k = rng() % live.size();
            s.reqs.push_back(OrderRequest::cancel(live[k]));
            live[k] = live.back();
            live.pop_back();
        } else {
            const Side side = (rng() & 1) ? Side::Buy : Side::Sell;
            s.reqs.push_back(OrderRequest::market(next_id++, side, 50));
        }
    }
    return s;
}

void BM_MixedStream(benchmark::State& state) {
    static const MixedStream stream = make_stream(100'000);
    std::vector<Fill> fills;
    fills.reserve(1024);
    for (auto _ : state) {
        state.PauseTiming();
        auto book = std::make_unique<OrderBook>(1 << 17);
        state.ResumeTiming();
        for (const auto& r : stream.reqs) {
            fills.clear();
            benchmark::DoNotOptimize(book->submit(r, fills));
        }
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(stream.reqs.size()));
}
BENCHMARK(BM_MixedStream)->Unit(benchmark::kMillisecond);

void BM_SpscTransfer(benchmark::State& state) {
    constexpr std::uint64_t kItems = 1'000'000;
    for (auto _ : state) {
        SpscQueue<std::uint64_t> q(4096);
        std::thread producer([&] {
            for (std::uint64_t i = 0; i < kItems; ++i) {
                while (!q.try_push(i)) {
                }
            }
        });
        std::uint64_t got = 0;
        std::uint64_t sum = 0;
        while (got < kItems) {
            if (auto* v = q.front()) {
                sum += *v;
                q.pop();
                ++got;
            }
        }
        producer.join();
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kItems));
}
BENCHMARK(BM_SpscTransfer)->Unit(benchmark::kMillisecond)->UseRealTime();

void BM_HistogramRecord(benchmark::State& state) {
    auto h = std::make_unique<LatencyHistogram>();
    std::uint64_t v = 1;
    for (auto _ : state) {
        v = v * 6364136223846793005ULL + 1442695040888963407ULL;
        h->record((v >> 40) & 0xFFFFF);
    }
    benchmark::DoNotOptimize(h->count());
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_HistogramRecord);

}  // namespace

BENCHMARK_MAIN();
