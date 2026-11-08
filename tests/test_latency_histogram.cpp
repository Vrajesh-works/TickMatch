#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <random>
#include <sstream>
#include <vector>

#include "tickmatch/latency_histogram.hpp"

using tickmatch::LatencyHistogram;

TEST(LatencyHistogram, EmptyIsZero) {
    LatencyHistogram h;
    EXPECT_EQ(h.count(), 0u);
    EXPECT_EQ(h.percentile(50), 0u);
    EXPECT_EQ(h.min(), 0u);
    EXPECT_DOUBLE_EQ(h.mean(), 0.0);
}

TEST(LatencyHistogram, SmallValuesAreExact) {
    LatencyHistogram h;
    for (std::uint64_t v = 1; v <= 50; ++v) h.record(v);
    EXPECT_EQ(h.percentile(50), 25u);
    EXPECT_EQ(h.percentile(100), 50u);
    EXPECT_EQ(h.min(), 1u);
    EXPECT_EQ(h.max(), 50u);
    EXPECT_DOUBLE_EQ(h.mean(), 25.5);
}

TEST(LatencyHistogram, BucketsAreContiguousAndCoverValues) {
    for (std::size_t i = 0; i + 1 < LatencyHistogram::kBuckets; ++i) {
        ASSERT_EQ(LatencyHistogram::bucket_upper(i) + 1, LatencyHistogram::bucket_lower(i + 1))
            << i;
    }
    std::mt19937_64 rng(1);
    for (int k = 0; k < 100000; ++k) {
        const std::uint64_t v = rng() >> (rng() % 64);
        const std::size_t i = LatencyHistogram::index_of(v);
        ASSERT_LT(i, LatencyHistogram::kBuckets);
        ASSERT_LE(LatencyHistogram::bucket_lower(i), v);
        ASSERT_GE(LatencyHistogram::bucket_upper(i), v);
    }
}

TEST(LatencyHistogram, RelativeErrorBounded) {
    for (std::uint64_t v : {100ULL, 1'000ULL, 12'345ULL, 999'999ULL, 50'000'000ULL}) {
        const std::size_t i = LatencyHistogram::index_of(v);
        const double width = static_cast<double>(LatencyHistogram::bucket_upper(i) -
                                                 LatencyHistogram::bucket_lower(i) + 1);
        EXPECT_LE(width / static_cast<double>(v), 1.0 / 32.0 + 1e-9) << v;
    }
}

TEST(LatencyHistogram, PercentilesTrackExactWithinError) {
    std::mt19937_64 rng(42);
    std::lognormal_distribution<double> dist(6.0, 1.0);  // median about 400 ns
    auto h = std::make_unique<LatencyHistogram>();
    std::vector<std::uint64_t> raw;
    for (int i = 0; i < 200000; ++i) {
        const auto v = static_cast<std::uint64_t>(dist(rng)) + 1;
        raw.push_back(v);
        h->record(v);
    }
    std::sort(raw.begin(), raw.end());
    for (double p : {50.0, 90.0, 99.0, 99.9}) {
        const auto rank =
            static_cast<std::size_t>(std::ceil(p / 100.0 * static_cast<double>(raw.size()))) - 1;
        const double exact = static_cast<double>(raw[rank]);
        const double approx = static_cast<double>(h->percentile(p));
        EXPECT_GE(approx, exact) << p;  // reports the bucket upper bound
        EXPECT_LE(approx, exact * (1.0 + 1.0 / 32.0) + 1.0) << p;
    }
    EXPECT_EQ(h->max(), raw.back());
}

TEST(LatencyHistogram, HugeValuesClamp) {
    LatencyHistogram h;
    h.record(UINT64_MAX);
    EXPECT_EQ(LatencyHistogram::index_of(UINT64_MAX), LatencyHistogram::kBuckets - 1);
    EXPECT_EQ(h.percentile(50), UINT64_MAX);
}

TEST(LatencyHistogram, MergeAddsCounts) {
    LatencyHistogram a;
    LatencyHistogram b;
    a.record(10);
    b.record(1000);
    b.record(20);
    a.merge(b);
    EXPECT_EQ(a.count(), 3u);
    EXPECT_EQ(a.min(), 10u);
    EXPECT_EQ(a.max(), 1000u);
    EXPECT_EQ(a.percentile(50), 20u);
}

TEST(LatencyHistogram, CsvListsNonEmptyBuckets) {
    LatencyHistogram h;
    h.record(5);
    h.record(5);
    h.record(1000);
    std::ostringstream os;
    h.write_csv(os);
    const std::string csv = os.str();
    EXPECT_EQ(csv.rfind("lower_ns,upper_ns,count\n", 0), 0u);
    EXPECT_NE(csv.find("5,5,2\n"), std::string::npos);
    EXPECT_EQ(std::count(csv.begin(), csv.end(), '\n'), 3);
}
