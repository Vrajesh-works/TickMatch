#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "tickmatch/types.hpp"

namespace tickmatch {

// Open addressing hash map from OrderId to a 32-bit pool index.
// Linear probing with backward-shift deletion (no tombstones), fixed capacity
// sized at construction so the hot path never rehashes or allocates.
class IdIndex {
  public:
    static constexpr std::uint32_t kEmpty = std::numeric_limits<std::uint32_t>::max();

    explicit IdIndex(std::size_t max_entries) {
        std::size_t cap = 16;
        while (cap < max_entries * 2) cap <<= 1;  // load factor <= 0.5
        slots_.assign(cap, Slot{0, kEmpty});
        mask_ = cap - 1;
    }

    [[nodiscard]] std::uint32_t find(OrderId key) const noexcept {
        for (std::size_t i = home(key);; i = (i + 1) & mask_) {
            const Slot& s = slots_[i];
            if (s.val == kEmpty) return kEmpty;
            if (s.key == key) return s.val;
        }
    }

    // Returns false if the key is already present. Caller guarantees size() < max_entries.
    bool insert(OrderId key, std::uint32_t val) noexcept {
        for (std::size_t i = home(key);; i = (i + 1) & mask_) {
            Slot& s = slots_[i];
            if (s.val == kEmpty) {
                s = Slot{key, val};
                ++size_;
                return true;
            }
            if (s.key == key) return false;
        }
    }

    bool erase(OrderId key) noexcept {
        std::size_t i = home(key);
        for (;; i = (i + 1) & mask_) {
            if (slots_[i].val == kEmpty) return false;
            if (slots_[i].key == key) break;
        }
        // Backward shift: pull later entries of the probe run into the hole
        // unless their home slot lies cyclically in (hole, j].
        std::size_t j = i;
        for (;;) {
            j = (j + 1) & mask_;
            if (slots_[j].val == kEmpty) break;
            const std::size_t h = home(slots_[j].key);
            const bool stays = (i <= j) ? (i < h && h <= j) : (i < h || h <= j);
            if (!stays) {
                slots_[i] = slots_[j];
                i = j;
            }
        }
        slots_[i].val = kEmpty;
        --size_;
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return slots_.size(); }

  private:
    struct Slot {
        OrderId key;
        std::uint32_t val;
    };

    static std::uint64_t mix(std::uint64_t x) noexcept {  // splitmix64 finalizer
        x ^= x >> 30;
        x *= 0xbf58476d1ce4e5b9ULL;
        x ^= x >> 27;
        x *= 0x94d049bb133111ebULL;
        x ^= x >> 31;
        return x;
    }
    [[nodiscard]] std::size_t home(OrderId key) const noexcept {
        return static_cast<std::size_t>(mix(key)) & mask_;
    }

    std::vector<Slot> slots_;
    std::size_t mask_{0};
    std::size_t size_{0};
};

}  // namespace tickmatch
