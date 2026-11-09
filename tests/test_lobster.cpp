#include <gtest/gtest.h>

#include <vector>

#include "tickmatch/lobster.hpp"
#include "tickmatch/order_book.hpp"

using namespace tickmatch;
using namespace tickmatch::lobster;

TEST(LobsterParse, SubmitLine) {
    auto m = parse_line("34200.004241176,1,16113575,18,5853300,1");
    ASSERT_TRUE(m);
    EXPECT_NEAR(m->time, 34200.004241176, 1e-9);
    EXPECT_EQ(m->type, EventType::Submit);
    EXPECT_EQ(m->order_id, 16113575u);
    EXPECT_EQ(m->size, 18u);
    EXPECT_EQ(m->price, 5853300);
    EXPECT_EQ(m->side, Side::Buy);
}

TEST(LobsterParse, SellDirectionAndCrlf) {
    auto m = parse_line("34200.1,3,42,100,5853400,-1\r\n");
    ASSERT_TRUE(m);
    EXPECT_EQ(m->type, EventType::Delete);
    EXPECT_EQ(m->side, Side::Sell);
}

TEST(LobsterParse, RejectsMalformed) {
    EXPECT_FALSE(parse_line(""));
    EXPECT_FALSE(parse_line("34200.1,1,42,100,5853400"));            // 5 fields
    EXPECT_FALSE(parse_line("34200.1,1,42,100,5853400,1,9"));        // 7 fields
    EXPECT_FALSE(parse_line("34200.1,9,42,100,5853400,1"));          // bad type
    EXPECT_FALSE(parse_line("34200.1,1,42,100,5853400,0"));          // bad direction
    EXPECT_FALSE(parse_line("34200.1,1,abc,100,5853400,1"));         // non-numeric id
    EXPECT_FALSE(parse_line("34200.1,1,42,-5,5853400,1"));           // negative size
    EXPECT_FALSE(parse_line("34200.1,1,42,99999999999,5853400,1"));  // size overflow
    EXPECT_FALSE(parse_line("time,type,id,size,price,dir"));         // header row
}

TEST(LobsterMap, EventTypesMapToRequests) {
    OrderId synth = kSyntheticIdBase;
    Message m{1.0, EventType::Submit, 7, 10, 1000, Side::Sell};
    auto r = to_request(m, synth);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->type, OrderType::Limit);
    EXPECT_EQ(r->side, Side::Sell);

    m.type = EventType::PartialCancel;
    EXPECT_EQ(to_request(m, synth)->type, OrderType::Reduce);
    m.type = EventType::Delete;
    EXPECT_EQ(to_request(m, synth)->type, OrderType::Cancel);

    m.type = EventType::ExecuteVisible;
    r = to_request(m, synth);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->type, OrderType::Market);
    EXPECT_EQ(r->side, Side::Buy);  // aggressor is opposite the resting sell
    EXPECT_EQ(r->id, kSyntheticIdBase);
    EXPECT_EQ(synth, kSyntheticIdBase + 1);

    for (auto t : {EventType::ExecuteHidden, EventType::Cross, EventType::Halt}) {
        m.type = t;
        EXPECT_FALSE(to_request(m, synth));
    }
}

TEST(LobsterMap, ExecutionHitsTheNamedRestingOrder) {
    const char* lines[] = {
        "34200.0,1,1,100,1000000,-1",  // ask 100 @ 100.00
        "34200.1,1,2,50,1000000,-1",   // ask 50 @ 100.00, behind order 1
        "34200.2,1,3,70,999900,1",     // bid 70 @ 99.99
        "34200.3,4,1,40,1000000,-1",   // execute 40 of order 1
        "34200.4,2,2,10,1000000,-1",   // partial cancel 10 of order 2
        "34200.5,3,3,70,999900,1",     // delete order 3
    };
    OrderBook book(64);
    std::vector<Fill> fills;
    OrderId synth = kSyntheticIdBase;
    for (const char* line : lines) {
        auto m = parse_line(line);
        ASSERT_TRUE(m) << line;
        auto r = to_request(*m, synth);
        ASSERT_TRUE(r);
        EXPECT_FALSE(is_reject(book.submit(*r, fills))) << line;
    }
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].maker_id, 1u);
    EXPECT_EQ(fills[0].qty, 40u);
    EXPECT_EQ(book.find(1)->qty, 60u);
    EXPECT_EQ(book.find(2)->qty, 40u);
    EXPECT_FALSE(book.contains(3));
    EXPECT_EQ(book.check_invariants(), "");
}
