#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>

namespace tickmatch {

inline std::uint64_t now_ns() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

// Log-linear latency histogram in nanoseconds (HdrHistogram style, fixed size).
//
// Values below 64 ns get one bucket each. Above that every power of two is
// split into 32 linear sub-buckets, so any recorded value is reported with at
// most ~3% relative error. Values above 2^40 ns (about 18 minutes) clamp into
// the last bucket. Recording is a few integer ops and one increment, with no
// allocation, so it is safe on the hot path.
class LatencyHistogram {
  public:
    static constexpr unsigned kSubBits = 5;
    static constexpr std::uint64_t kSubCount = std::uint64_t{1} << kSubBits;  // 32
    static constexpr std::uint64_t kLinearLimit = kSubCount * 2;              // 64
    static constexpr unsigned kMaxBits = 40;
    static constexpr std::size_t kBuckets =
        kLinearLimit + (kMaxBits - (kSubBits + 1) + 1) * kSubCount;

    void record(std::uint64_t ns) noexcept {
        ++counts_[index_of(ns)];
        ++count_;
        sum_ += ns;
        if (ns < min_) min_ = ns;
        if (ns > max_) max_ = ns;
    }

    void merge(const LatencyHistogram& other) noexcept;
    void reset() noexcept { *this = LatencyHistogram{}; }

    [[nodiscard]] std::uint64_t count() const noexcept { return count_; }
    [[nodiscard]] std::uint64_t min() const noexcept { return count_ ? min_ : 0; }
    [[nodiscard]] std::uint64_t max() const noexcept { return max_; }
    [[nodiscard]] double mean() const noexcept {
        return count_ ? static_cast<double>(sum_) / static_cast<double>(count_) : 0.0;
    }
    // Value at percentile p in [0, 100]: the upper bound of the bucket that
    // holds the ceil(p% * count)-th smallest sample, clamped to the true max.
    [[nodiscard]] std::uint64_t percentile(double p) const noexcept;

    static std::size_t index_of(std::uint64_t ns) noexcept;
    static std::uint64_t bucket_lower(std::size_t index) noexcept;
    static std::uint64_t bucket_upper(std::size_t index) noexcept;
    [[nodiscard]] std::uint64_t bucket_count(std::size_t index) const noexcept {
        return counts_[index];
    }

    // CSV with header "lower_ns,upper_ns,count", non-empty buckets only.
    void write_csv(std::ostream& os) const;
    // One line: count, min, p50, p90, p99, p99.9, max, mean.
    [[nodiscard]] std::string summary() const;

  private:
    std::array<std::uint64_t, kBuckets> counts_{};
    std::uint64_t count_{0};
    std::uint64_t sum_{0};
    std::uint64_t min_{UINT64_MAX};
    std::uint64_t max_{0};
};

}  // namespace tickmatch
