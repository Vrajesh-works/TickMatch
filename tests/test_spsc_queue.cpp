#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include "tickmatch/spsc_queue.hpp"

using tickmatch::SpscQueue;

TEST(SpscQueue, CapacityRoundsUpToPowerOfTwo) {
    SpscQueue<int> q(5);
    EXPECT_EQ(q.capacity(), 8u);
    EXPECT_TRUE(q.empty());
}

TEST(SpscQueue, FifoAndFullEmpty) {
    SpscQueue<int> q(4);
    for (int i = 0; i < 4; ++i) EXPECT_TRUE(q.try_push(i));
    EXPECT_FALSE(q.try_push(99));  // full
    EXPECT_EQ(q.size(), 4u);
    for (int i = 0; i < 4; ++i) {
        auto v = q.try_pop();
        ASSERT_TRUE(v.has_value());
        EXPECT_EQ(*v, i);
    }
    EXPECT_FALSE(q.try_pop().has_value());
}

TEST(SpscQueue, WrapsAroundManyTimes) {
    SpscQueue<std::uint64_t> q(8);
    std::uint64_t next_in = 0;
    std::uint64_t next_out = 0;
    for (int round = 0; round < 1000; ++round) {
        while (q.try_push(next_in)) ++next_in;
        for (int k = 0; k < 5; ++k) {
            auto v = q.try_pop();
            ASSERT_TRUE(v);
            ASSERT_EQ(*v, next_out++);
        }
    }
    while (auto v = q.try_pop()) ASSERT_EQ(*v, next_out++);
    EXPECT_EQ(next_in, next_out);
}

TEST(SpscQueue, FrontAndPopAvoidCopies) {
    SpscQueue<std::unique_ptr<int>> q(2);
    EXPECT_TRUE(q.try_push(std::make_unique<int>(7)));
    auto* p = q.front();
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(**p, 7);
    q.pop();
    EXPECT_EQ(q.front(), nullptr);
}

TEST(SpscQueue, DestructorReleasesRemainingElements) {
    auto counter = std::make_shared<int>(0);
    {
        SpscQueue<std::shared_ptr<int>> q(4);
        q.try_push(counter);
        q.try_push(counter);
        EXPECT_EQ(counter.use_count(), 3);
    }
    EXPECT_EQ(counter.use_count(), 1);
}

TEST(SpscQueue, NonTrivialTypes) {
    SpscQueue<std::string> q(4);
    q.try_emplace(std::size_t{3}, 'x');
    q.try_push(std::string("hello"));
    EXPECT_EQ(*q.try_pop(), "xxx");
    EXPECT_EQ(*q.try_pop(), "hello");
}

// Two threads: the consumer must see every value exactly once, in order.
// Run under ThreadSanitizer in CI.
TEST(SpscQueue, TwoThreadsPreserveOrderWithoutLoss) {
    constexpr std::uint64_t kCount = 2'000'000;
    SpscQueue<std::uint64_t> q(1024);
    std::thread producer([&] {
        for (std::uint64_t i = 0; i < kCount; ++i) {
            while (!q.try_push(i)) std::this_thread::yield();
        }
    });
    std::uint64_t expected = 0;
    std::uint64_t sum = 0;
    while (expected < kCount) {
        if (auto* v = q.front()) {
            ASSERT_EQ(*v, expected);
            sum += *v;
            q.pop();
            ++expected;
        } else {
            std::this_thread::yield();
        }
    }
    producer.join();
    EXPECT_EQ(sum, kCount * (kCount - 1) / 2);
    EXPECT_TRUE(q.empty());
}
