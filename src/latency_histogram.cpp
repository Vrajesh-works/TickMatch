#include "tickmatch/latency_histogram.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <ostream>
#include <sstream>

namespace tickmatch {

std::size_t LatencyHistogram::index_of(std::uint64_t ns) noexcept {
    if (ns < kLinearLimit) return static_cast<std::size_t>(ns);
    const unsigned msb = static_cast<unsigned>(std::bit_width(ns)) - 1;  // >= kSubBits + 1
    if (msb > kMaxBits) return kBuckets - 1;
    const unsigned shift = msb - kSubBits;
    const std::uint64_t sub = (ns >> shift) - kSubCount;  // in [0, kSubCount)
    return static_cast<std::size_t>(kLinearLimit + (msb - (kSubBits + 1)) * kSubCount + sub);
}

std::uint64_t LatencyHistogram::bucket_lower(std::size_t index) noexcept {
    if (index < kLinearLimit) return index;
    const std::uint64_t rel = index - kLinearLimit;
    const std::uint64_t octave = rel / kSubCount;
    const std::uint64_t sub = rel % kSubCount;
    const std::uint64_t shift = octave + 1;  // msb - kSubBits
    return (kSubCount + sub) << shift;
}

std::uint64_t LatencyHistogram::bucket_upper(std::size_t index) noexcept {
    if (index < kLinearLimit) return index;
    if (index == kBuckets - 1) return UINT64_MAX;
    return bucket_lower(index + 1) - 1;
}

void LatencyHistogram::merge(const LatencyHistogram& other) noexcept {
    for (std::size_t i = 0; i < kBuckets; ++i) counts_[i] += other.counts_[i];
    count_ += other.count_;
    sum_ += other.sum_;
    min_ = std::min(min_, other.min_);
    max_ = std::max(max_, other.max_);
}

std::uint64_t LatencyHistogram::percentile(double p) const noexcept {
    if (count_ == 0) return 0;
    p = std::clamp(p, 0.0, 100.0);
    auto rank = static_cast<std::uint64_t>(std::ceil(p / 100.0 * static_cast<double>(count_)));
    rank = std::clamp<std::uint64_t>(rank, 1, count_);
    std::uint64_t seen = 0;
    for (std::size_t i = 0; i < kBuckets; ++i) {
        seen += counts_[i];
        if (seen >= rank) return std::min(bucket_upper(i), max_);
    }
    return max_;
}

void LatencyHistogram::write_csv(std::ostream& os) const {
    os << "lower_ns,upper_ns,count\n";
    for (std::size_t i = 0; i < kBuckets; ++i) {
        if (counts_[i] == 0) continue;
        os << bucket_lower(i) << ',' << std::min(bucket_upper(i), max_) << ',' << counts_[i]
           << '\n';
    }
}

std::string LatencyHistogram::summary() const {
    std::ostringstream os;
    os << "count=" << count() << " min=" << min() << "ns p50=" << percentile(50)
       << "ns p90=" << percentile(90) << "ns p99=" << percentile(99)
       << "ns p99.9=" << percentile(99.9) << "ns max=" << max() << "ns mean=" << mean() << "ns";
    return os.str();
}

}  // namespace tickmatch
