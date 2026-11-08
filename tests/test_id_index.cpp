#include <gtest/gtest.h>

#include <random>
#include <unordered_map>

#include "tickmatch/id_index.hpp"

using tickmatch::IdIndex;

TEST(IdIndex, InsertFindErase) {
    IdIndex idx(8);
    EXPECT_EQ(idx.find(1), IdIndex::kEmpty);
    EXPECT_TRUE(idx.insert(1, 10));
    EXPECT_FALSE(idx.insert(1, 11));
    EXPECT_EQ(idx.find(1), 10u);
    EXPECT_TRUE(idx.erase(1));
    EXPECT_FALSE(idx.erase(1));
    EXPECT_EQ(idx.size(), 0u);
}

TEST(IdIndex, RandomOpsMatchStdMap) {
    // Small table so probe runs collide and wrap, exercising backward shift.
    constexpr std::size_t kMax = 64;
    IdIndex idx(kMax);
    std::unordered_map<std::uint64_t, std::uint32_t> ref;
    std::mt19937_64 rng(7);
    for (int step = 0; step < 200000; ++step) {
        const std::uint64_t key = rng() % 200;
        const auto op = rng() % 3;
        if (op == 0 && ref.size() < kMax) {
            const auto val = static_cast<std::uint32_t>(rng() % 1000);
            const bool inserted = idx.insert(key, val);
            EXPECT_EQ(inserted, ref.emplace(key, val).second);
        } else if (op == 1) {
            EXPECT_EQ(idx.erase(key), ref.erase(key) == 1);
        } else {
            auto it = ref.find(key);
            EXPECT_EQ(idx.find(key), it == ref.end() ? IdIndex::kEmpty : it->second);
        }
        ASSERT_EQ(idx.size(), ref.size());
    }
    for (const auto& [k, v] : ref) EXPECT_EQ(idx.find(k), v);
}
