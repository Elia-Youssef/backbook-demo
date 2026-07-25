#include "backbook/journal/fingerprint.hpp"
#include "backbook/journal/replay.hpp"

#include "journal_test_support.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace backbook::journal {
namespace {

TEST(JournalReplayTest, ReconstructsCanonicalStateIncludingNoOpEod) {
    const auto batches = test_support::canonical_batches(true);

    const auto first = replay(test_support::limits(), batches);
    const auto second = replay(test_support::limits(), batches);

    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(first.value().version(), 4U);
    ASSERT_EQ(first.value().trade_versions().size(), 2U);
    EXPECT_EQ(first.value().ledger_entries().size(), 3U);
    EXPECT_EQ(first.value().posting_count(), 12U);
    EXPECT_EQ(first.value().settlements().size(), 2U);
    EXPECT_EQ(first.value()
                  .current_trade(test_support::id<domain::TradeId>("TRD-1001"))
                  ->state(),
              domain::TradeState::Settled);

    const auto first_bytes = canonical_state_bytes(first.value());
    const auto second_bytes = canonical_state_bytes(second.value());
    ASSERT_TRUE(first_bytes);
    ASSERT_TRUE(second_bytes);
    EXPECT_EQ(first_bytes.value(), second_bytes.value());

    const auto first_fingerprint = state_fingerprint(first.value());
    const auto second_fingerprint = state_fingerprint(second.value());
    ASSERT_TRUE(first_fingerprint);
    ASSERT_TRUE(second_fingerprint);
    EXPECT_EQ(first_fingerprint.value(), 0x3e861e6ba08cf018ULL);
    EXPECT_EQ(first_fingerprint.value(), second_fingerprint.value());
}

TEST(JournalReplayTest, RejectsSequenceGap) {
    auto batch = test_support::canonical_batches().front();
    auto moved = CommandBatch::create(2U,
                                      batch.command_id(),
                                      batch.canonical_request(),
                                      batch.events(),
                                      batch.result());
    ASSERT_TRUE(moved);

    const std::vector<CommandBatch> batches{std::move(moved).value()};
    const auto result = replay(test_support::limits(), batches);

    ASSERT_TRUE(result.has_error());
    EXPECT_EQ(result.error().code, ReplayErrorCode::SequenceMismatch);
    EXPECT_EQ(result.error().expected_sequence, 1U);
    EXPECT_EQ(result.error().batch_sequence, 2U);
}

TEST(JournalReplayTest, RejectsBatchWithoutExactlyOneCommandEvent) {
    const auto trade_id =
        test_support::id<domain::TradeId>("TRD-1001");
    auto empty = CommandBatch::create(
        1U,
        test_support::id<domain::CommandId>("CMD-EMPTY"),
        {},
        {},
        TradeBookedResult{trade_id, 1U, 0U});
    ASSERT_TRUE(empty);
    const std::vector<CommandBatch> batches{std::move(empty).value()};

    const auto result = replay(test_support::limits(), batches);

    ASSERT_TRUE(result.has_error());
    EXPECT_EQ(result.error().code, ReplayErrorCode::InvalidBatchShape);
}

TEST(JournalReplayTest, RejectsEventThatCannotApply) {
    auto batches = test_support::canonical_batches();
    batches.erase(batches.begin() + 1, batches.end());
    const auto duplicate = test_support::batch(
        2U,
        std::get<TradeBookedEvent>(batches.front().events().front()),
        TradeBookedResult{
            test_support::id<domain::TradeId>("TRD-1001"), 1U, 2U});
    batches.push_back(duplicate);

    const auto result = replay(test_support::limits(), batches);

    ASSERT_TRUE(result.has_error());
    EXPECT_EQ(result.error().code, ReplayErrorCode::EventApplicationFailed);
    ASSERT_TRUE(result.error().state_error.has_value());
    EXPECT_EQ(result.error().state_error->code,
              domain::StateErrorCode::DuplicateTradeId);
}

TEST(JournalReplayTest, RejectsResultThatDoesNotDescribeAppliedEvent) {
    const auto trade_id = test_support::id<domain::TradeId>("TRD-1001");
    const auto path = test_support::limit_path();
    const std::vector<CommandBatch> batches{test_support::batch(
        1U,
        TradeBookedEvent{trade_id,
                         path.book_id(),
                         path.counterparty_id(),
                         path.netting_set_id(),
                         test_support::terms()},
        TradeBookedResult{
            test_support::id<domain::TradeId>("TRD-WRONG"), 1U, 1U})};

    const auto result = replay(test_support::limits(), batches);

    ASSERT_TRUE(result.has_error());
    EXPECT_EQ(result.error().code, ReplayErrorCode::ResultMismatch);
}

TEST(StateFingerprintTest, UsesStandardFnv1a64Vector) {
    constexpr std::array<std::uint8_t, 5U> hello{'h', 'e', 'l', 'l', 'o'};
    EXPECT_EQ(fnv1a64(hello), 0xa430d84680aabd0bULL);
}

TEST(StateFingerprintTest, ChangesWhenCanonicalStateChanges) {
    const auto settled =
        replay(test_support::limits(), test_support::canonical_batches());
    ASSERT_TRUE(settled);

    auto shorter_batches = test_support::canonical_batches();
    shorter_batches.pop_back();
    const auto confirmed = replay(test_support::limits(), shorter_batches);
    ASSERT_TRUE(confirmed);

    const auto settled_fingerprint = state_fingerprint(settled.value());
    const auto confirmed_fingerprint = state_fingerprint(confirmed.value());
    ASSERT_TRUE(settled_fingerprint);
    ASSERT_TRUE(confirmed_fingerprint);
    EXPECT_NE(settled_fingerprint.value(), confirmed_fingerprint.value());
}

}  // namespace
}  // namespace backbook::journal
