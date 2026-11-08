# TickMatch - Product Document

## Problem

Electronic exchanges and internal crossing engines all sit on the same core
component: a limit order book that matches buy and sell orders by price-time
priority. Getting it right is subtle (partial fills, cancels racing fills,
never letting the book cross) and getting it fast is a systems problem
(memory layout, allocation, cross-thread handoff, tail latency).

Most public examples are either toy maps-of-queues with no latency story, or
large production engines that are hard to read. TickMatch is a small, readable
C++20 engine that is correct by construction, tested against a reference
model, replayed on exchange-format data, and measured in nanoseconds at p50
and p99.

## Target users

- **Engineers evaluating a matching core**: want a compact engine whose
  behaviour is pinned down by tests and whose latency is reported honestly
  (histograms, not averages).
- **Quant and trading-systems developers**: want to replay LOBSTER-format
  message streams through a book and inspect fills, top of book and latency.
- **Hiring reviewers for C++ / low-latency roles**: want to see data-structure
  choices, lock-free code, sanitizers and benchmarks in one place.

## Core features (MVP, shipped today)

1. **Order types**: limit (good till cancel), market (immediate, remainder
   cancelled), cancel, and partial cancel (reduce quantity, needed for LOBSTER
   type 2 messages).
2. **Price-time priority matching** with partial fills and trade reports that
   carry maker id, taker id, price and quantity.
3. **Cache-friendly book layout**: price levels stored in a contiguous sorted
   vector per side (best price at the back), orders stored in a preallocated
   pool with index-based intrusive FIFO lists, so the hot path does no heap
   allocation once warmed up.
4. **Lock-free SPSC queue** between an ingest thread (parsing) and the matching
   thread, with cache-line separated head and tail indices.
5. **Latency measurement**: log-linear nanosecond histogram with p50, p90, p99,
   p99.9 and max, printed by the replay tool and exported to CSV.
6. **Replay of LOBSTER message files** (real samples if available, plus a
   synthetic generator that writes the same format).
7. **Python client** that generates data, drives the replay binary and plots
   the latency histogram.
8. **Quality gates**: GoogleTest unit tests, a randomized differential test
   against a simple reference book that also checks "never crossed" and
   quantity conservation, Google Benchmark microbenchmarks, and GitHub Actions
   CI on gcc and clang with ASan/UBSan and TSan jobs.

## Non-goals

- Network gateway, FIX/OUCH/ITCH protocol handling, or session management.
- Persistence, recovery or replication of book state.
- Multiple instruments in one process, auctions, iceberg, stop or pegged
  orders.
- Risk checks, self-trade prevention, fees.
- Cloud deployment (AWS, Azure, managed services). Listed as future work.

## Success metrics

| Metric | Target |
| --- | --- |
| Correctness | Differential fuzz test matches the reference book on 100k+ random operations; book never crosses; every order's quantity is conserved |
| Test health | All unit and integration tests green on gcc and clang, clean under ASan, UBSan and TSan |
| Latency | Replay prints p50 and p99 for the in-book operation; target p50 under 1 microsecond on a laptop-class CPU for synthetic streams |
| Throughput | Microbenchmarks report millions of operations per second for add/cancel/match |
| Usability | One command (`make demo` or `docker compose up`) builds, generates data, replays it and writes a latency report |
