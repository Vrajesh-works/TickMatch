#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace tickmatch {

// Cache line size used for padding. std::hardware_destructive_interference_size
// is not reliably available across compilers, so use the common x86/ARM value.
inline constexpr std::size_t kCacheLine = 64;

// Bounded lock-free single-producer single-consumer ring buffer.
//
// - Capacity is rounded up to a power of two so indices wrap with a mask.
// - head_ (consumer) and tail_ (producer) live on separate cache lines to
//   avoid false sharing.
// - Each side keeps a cached copy of the other side's index and only reloads
//   it (an acquire load of a contended line) when the cached value says the
//   queue looks full or empty.
// - Indices grow monotonically; size = tail - head.
//
// Exactly one thread may call the producer methods (try_push, emplace) and
// exactly one other thread the consumer methods (try_pop, front, pop).
template <typename T>
class SpscQueue {
    static_assert(std::is_nothrow_move_constructible_v<T>, "T must be nothrow movable");
    static_assert(std::is_nothrow_destructible_v<T>, "T must be nothrow destructible");

  public:
    explicit SpscQueue(std::size_t min_capacity) {
        std::size_t cap = 2;
        while (cap < min_capacity) cap <<= 1;
        capacity_ = cap;
        mask_ = cap - 1;
        storage_ = std::allocator<Slot>{}.allocate(cap);
    }

    ~SpscQueue() {
        while (front() != nullptr) pop();
        std::allocator<Slot>{}.deallocate(storage_, capacity_);
    }

    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;

    // Producer side.
    template <typename... Args>
    bool try_emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail - head_cache_ == capacity_) {
            head_cache_ = head_.load(std::memory_order_acquire);
            if (tail - head_cache_ == capacity_) return false;  // full
        }
        ::new (static_cast<void*>(&storage_[tail & mask_])) T(std::forward<Args>(args)...);
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    bool try_push(const T& v) noexcept(std::is_nothrow_copy_constructible_v<T>) {
        return try_emplace(v);
    }
    bool try_push(T&& v) noexcept { return try_emplace(std::move(v)); }

    // Consumer side. Returns nullptr when empty.
    T* front() noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_cache_) {
            tail_cache_ = tail_.load(std::memory_order_acquire);
            if (head == tail_cache_) return nullptr;  // empty
        }
        return std::launder(reinterpret_cast<T*>(&storage_[head & mask_]));
    }

    // Precondition: front() returned non-null.
    void pop() noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        std::launder(reinterpret_cast<T*>(&storage_[head & mask_]))->~T();
        head_.store(head + 1, std::memory_order_release);
    }

    std::optional<T> try_pop() noexcept {
        T* p = front();
        if (p == nullptr) return std::nullopt;
        std::optional<T> out{std::move(*p)};
        pop();
        return out;
    }

    // Approximate when called concurrently; exact when quiescent.
    [[nodiscard]] std::size_t size() const noexcept {
        return tail_.load(std::memory_order_acquire) - head_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool empty() const noexcept { return size() == 0; }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

  private:
    struct Slot {
        alignas(T) unsigned char bytes[sizeof(T)];
    };

    // Read-mostly configuration shared by both sides.
    std::size_t capacity_{0};
    std::size_t mask_{0};
    Slot* storage_{nullptr};

    // Consumer-owned line.
    alignas(kCacheLine) std::atomic<std::size_t> head_{0};
    std::size_t tail_cache_{0};

    // Producer-owned line.
    alignas(kCacheLine) std::atomic<std::size_t> tail_{0};
    std::size_t head_cache_{0};

    // Keep the next object off the producer line.
    alignas(kCacheLine) char pad_[1]{};
};

}  // namespace tickmatch
