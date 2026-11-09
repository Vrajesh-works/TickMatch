#include <gtest/gtest.h>

#include <vector>

#include "tickmatch/order_book.hpp"

using namespace tickmatch;

namespace {

class OrderBookTest : public ::testing::Test {
  protected:
    OrderBook book{1024};
    std::vector<Fill> fills;

    Status limit(OrderId id, Side s, Price p, Qty q) { return book.add_limit(id, s, p, q, fills); }
    Status market(OrderId id, Side s, Qty q) { return book.add_market(id, s, q, fills); }
    void TearDown() override { EXPECT_EQ(book.check_invariants(), ""); }
};

TEST_F(OrderBookTest, EmptyBook) {
    EXPECT_FALSE(book.best_bid());
    EXPECT_FALSE(book.best_ask());
    EXPECT_EQ(book.order_count(), 0u);
}

TEST_F(OrderBookTest, NonCrossingLimitsRest) {
    EXPECT_EQ(limit(1, Side::Buy, 100, 10), Status::Accepted);
    EXPECT_EQ(limit(2, Side::Sell, 101, 5), Status::Accepted);
    EXPECT_EQ(limit(3, Side::Buy, 99, 7), Status::Accepted);
    EXPECT_EQ(*book.best_bid(), 100);
    EXPECT_EQ(*book.best_ask(), 101);
    EXPECT_EQ(book.order_count(), 3u);
    EXPECT_EQ(book.level_count(Side::Buy), 2u);
    EXPECT_TRUE(fills.empty());
}

TEST_F(OrderBookTest, LevelsAreReportedBestFirst) {
    limit(1, Side::Buy, 98, 1);
    limit(2, Side::Buy, 100, 2);
    limit(3, Side::Buy, 99, 3);
    limit(4, Side::Sell, 103, 4);
    limit(5, Side::Sell, 101, 5);
    auto bids = book.levels(Side::Buy);
    ASSERT_EQ(bids.size(), 3u);
    EXPECT_EQ(bids[0].price, 100);
    EXPECT_EQ(bids[1].price, 99);
    EXPECT_EQ(bids[2].price, 98);
    auto asks = book.levels(Side::Sell, 1);
    ASSERT_EQ(asks.size(), 1u);
    EXPECT_EQ(asks[0].price, 101);
    EXPECT_EQ(asks[0].total_qty, 5u);
}

TEST_F(OrderBookTest, AggressiveLimitFullyFillsAtMakerPrice) {
    limit(1, Side::Sell, 101, 10);
    EXPECT_EQ(limit(2, Side::Buy, 105, 10), Status::Filled);
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].maker_id, 1u);
    EXPECT_EQ(fills[0].taker_id, 2u);
    EXPECT_EQ(fills[0].price, 101);  // trade at the resting price
    EXPECT_EQ(fills[0].qty, 10u);
    EXPECT_EQ(fills[0].taker_side, Side::Buy);
    EXPECT_TRUE(fills[0].maker_done);
    EXPECT_EQ(book.order_count(), 0u);
}

TEST_F(OrderBookTest, PartialFillRestsRemainder) {
    limit(1, Side::Sell, 101, 4);
    EXPECT_EQ(limit(2, Side::Buy, 101, 10), Status::Accepted);
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].qty, 4u);
    EXPECT_FALSE(book.best_ask());
    EXPECT_EQ(*book.best_bid(), 101);
    EXPECT_EQ(book.find(2)->qty, 6u);
}

TEST_F(OrderBookTest, PartialFillOfMakerKeepsItResting) {
    limit(1, Side::Buy, 100, 10);
    EXPECT_EQ(limit(2, Side::Sell, 100, 3), Status::Filled);
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_FALSE(fills[0].maker_done);
    EXPECT_EQ(book.find(1)->qty, 7u);
    EXPECT_EQ(book.depth_at(Side::Buy, 100), 7u);
}

TEST_F(OrderBookTest, TimePriorityWithinLevel) {
    limit(1, Side::Sell, 101, 5);
    limit(2, Side::Sell, 101, 5);
    limit(3, Side::Sell, 101, 5);
    limit(10, Side::Buy, 101, 12);
    ASSERT_EQ(fills.size(), 3u);
    EXPECT_EQ(fills[0].maker_id, 1u);
    EXPECT_EQ(fills[1].maker_id, 2u);
    EXPECT_EQ(fills[2].maker_id, 3u);
    EXPECT_EQ(fills[2].qty, 2u);
    EXPECT_EQ(book.find(3)->qty, 3u);
}

TEST_F(OrderBookTest, PricePriorityAcrossLevels) {
    limit(1, Side::Sell, 103, 5);
    limit(2, Side::Sell, 101, 5);
    limit(3, Side::Sell, 102, 5);
    limit(10, Side::Buy, 102, 20);
    ASSERT_EQ(fills.size(), 2u);
    EXPECT_EQ(fills[0].price, 101);
    EXPECT_EQ(fills[1].price, 102);
    // Remainder rests at its limit, below the untouched 103 ask.
    EXPECT_EQ(*book.best_bid(), 102);
    EXPECT_EQ(*book.best_ask(), 103);
    EXPECT_EQ(book.find(10)->qty, 10u);
}

TEST_F(OrderBookTest, SellSideSweepsBidsHighestFirst) {
    limit(1, Side::Buy, 99, 5);
    limit(2, Side::Buy, 100, 5);
    limit(3, Side::Sell, 99, 8);
    ASSERT_EQ(fills.size(), 2u);
    EXPECT_EQ(fills[0].price, 100);
    EXPECT_EQ(fills[1].price, 99);
    EXPECT_EQ(book.find(1)->qty, 2u);
}

TEST_F(OrderBookTest, MarketOrderFillsAndNeverRests) {
    limit(1, Side::Sell, 101, 5);
    limit(2, Side::Sell, 102, 5);
    EXPECT_EQ(market(3, Side::Buy, 7), Status::Filled);
    ASSERT_EQ(fills.size(), 2u);
    EXPECT_EQ(fills[1].qty, 2u);
    EXPECT_FALSE(book.contains(3));
    EXPECT_EQ(book.depth_at(Side::Sell, 102), 3u);
}

TEST_F(OrderBookTest, MarketOrderRemainderCancelled) {
    limit(1, Side::Buy, 100, 5);
    EXPECT_EQ(market(2, Side::Sell, 9), Status::PartialCancelled);
    EXPECT_EQ(book.order_count(), 0u);
    EXPECT_FALSE(book.best_ask());
}

TEST_F(OrderBookTest, MarketOrderOnEmptySideIsCancelled) {
    EXPECT_EQ(market(1, Side::Buy, 5), Status::Cancelled);
    EXPECT_TRUE(fills.empty());
}

TEST_F(OrderBookTest, CancelRemovesOrderAndEmptyLevel) {
    limit(1, Side::Buy, 100, 5);
    limit(2, Side::Buy, 100, 6);
    limit(3, Side::Buy, 99, 7);
    EXPECT_EQ(book.cancel(1), Status::Cancelled);
    EXPECT_EQ(book.depth_at(Side::Buy, 100), 6u);
    EXPECT_EQ(book.cancel(2), Status::Cancelled);
    EXPECT_EQ(*book.best_bid(), 99);
    EXPECT_EQ(book.level_count(Side::Buy), 1u);
}

TEST_F(OrderBookTest, CancelMiddleOfQueueKeepsOrder) {
    limit(1, Side::Sell, 101, 1);
    limit(2, Side::Sell, 101, 1);
    limit(3, Side::Sell, 101, 1);
    book.cancel(2);
    auto q = book.orders_at(Side::Sell, 101);
    ASSERT_EQ(q.size(), 2u);
    EXPECT_EQ(q[0].id, 1u);
    EXPECT_EQ(q[1].id, 3u);
}

TEST_F(OrderBookTest, CancelUnknownAndDoubleCancelRejected) {
    EXPECT_EQ(book.cancel(42), Status::RejectUnknownId);
    limit(1, Side::Buy, 100, 5);
    EXPECT_EQ(book.cancel(1), Status::Cancelled);
    EXPECT_EQ(book.cancel(1), Status::RejectUnknownId);
}

TEST_F(OrderBookTest, CancelAfterFullFillRejected) {
    limit(1, Side::Buy, 100, 5);
    limit(2, Side::Sell, 100, 5);
    EXPECT_EQ(book.cancel(1), Status::RejectUnknownId);
}

TEST_F(OrderBookTest, DuplicateLiveIdRejected) {
    limit(1, Side::Buy, 100, 5);
    EXPECT_EQ(limit(1, Side::Sell, 200, 5), Status::RejectDuplicateId);
    EXPECT_EQ(book.order_count(), 1u);
}

TEST_F(OrderBookTest, IdCanBeReusedAfterRemoval) {
    limit(1, Side::Buy, 100, 5);
    book.cancel(1);
    EXPECT_EQ(limit(1, Side::Buy, 100, 5), Status::Accepted);
}

TEST_F(OrderBookTest, ZeroQuantityRejected) {
    EXPECT_EQ(limit(1, Side::Buy, 100, 0), Status::RejectInvalid);
    EXPECT_EQ(market(2, Side::Buy, 0), Status::RejectInvalid);
}

TEST_F(OrderBookTest, ReduceKeepsTimePriority) {
    limit(1, Side::Sell, 101, 10);
    limit(2, Side::Sell, 101, 10);
    EXPECT_EQ(book.reduce(1, 4), Status::Reduced);
    EXPECT_EQ(book.depth_at(Side::Sell, 101), 16u);
    limit(3, Side::Buy, 101, 6);
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].maker_id, 1u);
    EXPECT_TRUE(fills[0].maker_done);
}

TEST_F(OrderBookTest, ReduceToZeroRemoves) {
    limit(1, Side::Buy, 100, 10);
    EXPECT_EQ(book.reduce(1, 10), Status::Reduced);
    EXPECT_FALSE(book.contains(1));
    EXPECT_FALSE(book.best_bid());
}

TEST_F(OrderBookTest, ReduceInvalidAmountsRejected) {
    limit(1, Side::Buy, 100, 10);
    EXPECT_EQ(book.reduce(1, 0), Status::RejectInvalid);
    EXPECT_EQ(book.reduce(1, 11), Status::RejectInvalid);
    EXPECT_EQ(book.reduce(9, 1), Status::RejectUnknownId);
    EXPECT_EQ(book.find(1)->qty, 10u);
}

TEST_F(OrderBookTest, SubmitDispatchesAllTypes) {
    EXPECT_EQ(book.submit(OrderRequest::limit(1, Side::Sell, 101, 5), fills), Status::Accepted);
    EXPECT_EQ(book.submit(OrderRequest::reduce(1, 1), fills), Status::Reduced);
    EXPECT_EQ(book.submit(OrderRequest::market(2, Side::Buy, 2), fills), Status::Filled);
    EXPECT_EQ(book.submit(OrderRequest::cancel(1), fills), Status::Cancelled);
    EXPECT_EQ(book.order_count(), 0u);
}

TEST(OrderBookCapacity, PoolExhaustionRejectsAndRecovers) {
    OrderBook book(4);
    std::vector<Fill> fills;
    for (OrderId id = 1; id <= 4; ++id) {
        EXPECT_EQ(book.add_limit(id, Side::Buy, 100 - static_cast<Price>(id), 1, fills),
                  Status::Accepted);
    }
    EXPECT_EQ(book.add_limit(5, Side::Buy, 50, 1, fills), Status::RejectInvalid);
    book.cancel(2);
    EXPECT_EQ(book.add_limit(5, Side::Buy, 50, 1, fills), Status::Accepted);
    EXPECT_EQ(book.check_invariants(), "");
}

TEST(OrderBookPrices, NegativePricesWork) {
    OrderBook book(16);
    std::vector<Fill> fills;
    book.add_limit(1, Side::Buy, -5, 1, fills);
    book.add_limit(2, Side::Sell, -3, 1, fills);
    EXPECT_EQ(*book.best_bid(), -5);
    EXPECT_EQ(*book.best_ask(), -3);
    book.add_limit(3, Side::Sell, -6, 1, fills);
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].price, -5);
    EXPECT_EQ(book.check_invariants(), "");
}

}  // namespace
