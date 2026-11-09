#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "tickmatch/replay.hpp"

using namespace tickmatch;

namespace {

const char* kStream =
    "34200.0,1,1,100,1000000,-1\n"
    "34200.1,1,2,50,1000000,-1\n"
    "34200.2,1,3,70,999900,1\n"
    "this,is,not,a,valid,line\n"
    "34200.3,4,1,40,1000000,-1\n"
    "34200.35,5,0,10,999950,1\n"  // hidden execution: skipped
    "34200.4,2,2,10,1000000,-1\n"
    "34200.5,3,3,70,999900,1\r\n"
    "34200.6,3,999,70,999900,1\n"  // unknown id (order placed before the file started)
    "\n";

class ReplayTest : public ::testing::TestWithParam<ReplayMode> {};

TEST_P(ReplayTest, CountsAndFinalState) {
    ReplayOptions opt;
    opt.mode = GetParam();
    opt.book_capacity = 1024;
    opt.queue_capacity = 4;  // tiny queue forces the producer to wait
    opt.check_every = 1;
    const auto s = std::make_unique<ReplayStats>(replay_lobster(kStream, opt));
    EXPECT_EQ(s->lines, 9u);
    EXPECT_EQ(s->malformed, 1u);
    EXPECT_EQ(s->skipped, 1u);
    EXPECT_EQ(s->requests, 7u);
    EXPECT_EQ(s->limits, 3u);
    EXPECT_EQ(s->markets, 1u);
    EXPECT_EQ(s->cancels, 2u);
    EXPECT_EQ(s->reduces, 1u);
    EXPECT_EQ(s->fills, 1u);
    EXPECT_EQ(s->volume, 40u);
    EXPECT_EQ(s->executions, 1u);
    EXPECT_EQ(s->executions_matched, 1u);
    EXPECT_EQ(s->reject_unknown_id, 1u);
    EXPECT_EQ(s->resting_orders, 2u);
    EXPECT_FALSE(s->best_bid.has_value());
    EXPECT_EQ(s->best_ask, 1000000);
    EXPECT_TRUE(s->invariants_ok) << s->invariant_error;
    EXPECT_EQ(s->book_latency.count(), 7u);
    if (GetParam() == ReplayMode::Pipeline) {
        EXPECT_EQ(s->e2e_latency.count(), 7u);
    } else {
        EXPECT_EQ(s->e2e_latency.count(), 0u);
    }

    const std::string json = to_json(*s, GetParam());
    EXPECT_NE(json.find("\"invariants_ok\":true"), std::string::npos);
    EXPECT_NE(json.find("\"best_bid\":null"), std::string::npos);
    EXPECT_NE(json.find("\"executions_matched\":1"), std::string::npos);
}

INSTANTIATE_TEST_SUITE_P(Modes, ReplayTest,
                         ::testing::Values(ReplayMode::Inline, ReplayMode::Pipeline),
                         [](const ::testing::TestParamInfo<ReplayMode>& param_info) {
                             return std::string(to_string(param_info.param));
                         });

TEST(Replay, EmptyInput) {
    const auto s = std::make_unique<ReplayStats>(replay_lobster("", ReplayOptions{}));
    EXPECT_EQ(s->requests, 0u);
    EXPECT_TRUE(s->invariants_ok);
}

}  // namespace
