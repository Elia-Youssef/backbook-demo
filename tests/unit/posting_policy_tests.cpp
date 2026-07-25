#include "backbook/domain/posting_policy.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace backbook::domain {
namespace {

template <typename IdType>
[[nodiscard]] IdType make_id(const char* text) {
    return IdType::parse(text).value();
}

[[nodiscard]] Money make_money(
    const Currency currency,
    const std::int64_t minor_units) {
    return Money::from_minor_units(currency, minor_units).value();
}

[[nodiscard]] Trade make_trade() {
    const FxTerms terms = FxTerms::create(
                              InstrumentKind::FxSpot,
                              IsoDate::parse("2026-07-25").value(),
                              IsoDate::parse("2026-07-27").value(),
                              make_money(Currency::Usd, 10'000'000),
                              make_money(Currency::Jpy, 1'500'000'000))
                              .value();
    return Trade::capture(
        make_id<TradeId>("TRD-1001"),
        make_id<BookId>("BOOK-FX-1"),
        make_id<CounterpartyId>("CPTY-A"),
        make_id<NettingSetId>("NET-A"),
        terms);
}

[[nodiscard]] Trade confirmed_trade() {
    return make_trade().apply(TradeAction::ConfirmTrade).value();
}

[[nodiscard]] ConfirmationPostingIds confirmation_ids() {
    return {
        make_id<PostingId>("P-PAY-CONTROL-D"),
        make_id<PostingId>("P-PAY-PAYABLE-C"),
        make_id<PostingId>("P-RECEIVE-RECEIVABLE-D"),
        make_id<PostingId>("P-RECEIVE-CONTROL-C")};
}

[[nodiscard]] ReversalPostingIds reversal_ids() {
    return {
        make_id<PostingId>("R-PAY-CONTROL-C"),
        make_id<PostingId>("R-PAY-PAYABLE-D"),
        make_id<PostingId>("R-RECEIVE-RECEIVABLE-C"),
        make_id<PostingId>("R-RECEIVE-CONTROL-D")};
}

[[nodiscard]] LedgerEntry confirmation_entry() {
    return build_confirmation_entry(confirmed_trade(), confirmation_ids())
        .value();
}

TEST(PostingPolicyTest, BuildsExactFourPostingConfirmationPolicyInOrder) {
    const Trade trade = confirmed_trade();
    const auto result = build_confirmation_entry(trade, confirmation_ids());

    ASSERT_TRUE(result.has_value());
    const auto& postings = result.value().postings();
    ASSERT_EQ(postings.size(), 4U);

    const std::array<const char*, 4U> expected_ids{
        "P-PAY-CONTROL-D",
        "P-PAY-PAYABLE-C",
        "P-RECEIVE-RECEIVABLE-D",
        "P-RECEIVE-CONTROL-C"};
    const std::array<const char*, 4U> expected_accounts{
        "COUNTERPARTY_CONTROL:CPTY-A",
        "SETTLEMENT_PAYABLE:BOOK-FX-1",
        "SETTLEMENT_RECEIVABLE:BOOK-FX-1",
        "COUNTERPARTY_CONTROL:CPTY-A"};
    const std::array<PostingSide, 4U> expected_sides{
        PostingSide::Debit,
        PostingSide::Credit,
        PostingSide::Debit,
        PostingSide::Credit};
    const std::array<Money, 4U> expected_amounts{
        trade.terms().pay(),
        trade.terms().pay(),
        trade.terms().receive(),
        trade.terms().receive()};

    for (std::size_t index = 0U; index < postings.size(); ++index) {
        EXPECT_EQ(postings[index].id().value(), expected_ids[index]);
        EXPECT_EQ(postings[index].account(), expected_accounts[index]);
        EXPECT_EQ(postings[index].side(), expected_sides[index]);
        EXPECT_EQ(postings[index].amount(), expected_amounts[index]);
        EXPECT_EQ(postings[index].trade_id(), trade.id());
        EXPECT_EQ(postings[index].trade_version(), trade.version());
        EXPECT_FALSE(postings[index].reversal_of().has_value());
    }

    EXPECT_EQ(postings[0U].amount().currency(), Currency::Usd);
    EXPECT_EQ(postings[1U].amount().currency(), Currency::Usd);
    EXPECT_EQ(postings[2U].amount().currency(), Currency::Jpy);
    EXPECT_EQ(postings[3U].amount().currency(), Currency::Jpy);
}

TEST(PostingPolicyTest, ConfirmationIsBalancedIndependentlyPerCurrency) {
    const LedgerEntry entry = confirmation_entry();
    const auto& postings = entry.postings();
    std::array<std::int64_t, 3U> balances{};

    for (const Posting& posting : postings) {
        const std::size_t currency =
            static_cast<std::size_t>(posting.amount().currency());
        const auto signed_amount =
            posting.side() == PostingSide::Debit
                ? posting.amount().minor_units()
                : -posting.amount().minor_units();
        balances[currency] += signed_amount;
    }

    EXPECT_EQ(balances[0U], 0);
    EXPECT_EQ(balances[1U], 0);
    EXPECT_EQ(balances[2U], 0);
}

TEST(PostingPolicyTest, RejectsTradeThatIsNotConfirmed) {
    const auto result =
        build_confirmation_entry(make_trade(), confirmation_ids());

    ASSERT_TRUE(result.has_error());
    EXPECT_EQ(result.error(), PostingPolicyError::TradeNotConfirmed);
}

TEST(PostingPolicyTest, RejectsDuplicateConfirmationPostingIds) {
    auto ids = confirmation_ids();
    ids.receive_control_credit = ids.pay_control_debit;

    const auto result = build_confirmation_entry(confirmed_trade(), ids);

    ASSERT_TRUE(result.has_error());
    EXPECT_EQ(
        result.error(),
        PostingPolicyError::LedgerDuplicatePostingId);
}

TEST(PostingPolicyTest, BuildsExactReversalForEveryOriginalPosting) {
    const LedgerEntry original = confirmation_entry();
    const auto result = build_reversal_entry(original, reversal_ids());

    ASSERT_TRUE(result.has_value());
    const auto& originals = original.postings();
    const auto& reversals = result.value().postings();
    ASSERT_EQ(reversals.size(), 4U);
    const std::array<const char*, 4U> expected_ids{
        "R-PAY-CONTROL-C",
        "R-PAY-PAYABLE-D",
        "R-RECEIVE-RECEIVABLE-C",
        "R-RECEIVE-CONTROL-D"};

    for (std::size_t index = 0U; index < reversals.size(); ++index) {
        const Posting& original_posting = originals[index];
        const Posting& reversal = reversals[index];
        EXPECT_EQ(reversal.id().value(), expected_ids[index]);
        EXPECT_EQ(reversal.trade_id(), original_posting.trade_id());
        EXPECT_EQ(reversal.trade_version(), original_posting.trade_version());
        EXPECT_EQ(reversal.account(), original_posting.account());
        EXPECT_EQ(reversal.amount(), original_posting.amount());
        EXPECT_NE(reversal.side(), original_posting.side());
        ASSERT_TRUE(reversal.reversal_of().has_value());
        EXPECT_EQ(reversal.reversal_of().value(), original_posting.id());
    }

    EXPECT_EQ(reversals[0U].side(), PostingSide::Credit);
    EXPECT_EQ(reversals[1U].side(), PostingSide::Debit);
    EXPECT_EQ(reversals[2U].side(), PostingSide::Credit);
    EXPECT_EQ(reversals[3U].side(), PostingSide::Debit);

    std::array<std::int64_t, 3U> balances{};
    for (const Posting& reversal : reversals) {
        const std::size_t currency =
            static_cast<std::size_t>(reversal.amount().currency());
        const auto signed_amount =
            reversal.side() == PostingSide::Debit
                ? reversal.amount().minor_units()
                : -reversal.amount().minor_units();
        balances[currency] += signed_amount;
    }
    EXPECT_EQ(balances[0U], 0);
    EXPECT_EQ(balances[1U], 0);
    EXPECT_EQ(balances[2U], 0);
}

TEST(PostingPolicyTest, RejectsDuplicateReversalPostingIds) {
    auto ids = reversal_ids();
    ids.receive_control_debit = ids.pay_control_credit;

    const auto result = build_reversal_entry(confirmation_entry(), ids);

    ASSERT_TRUE(result.has_error());
    EXPECT_EQ(
        result.error(),
        PostingPolicyError::LedgerDuplicatePostingId);
}

TEST(PostingPolicyTest, RejectsOriginalEntryThatIsNotFourPostings) {
    std::vector<Posting> postings;
    postings.push_back(
        Posting::create(
            make_id<PostingId>("P-TWO-D"),
            make_id<TradeId>("TRD-1001"),
            1U,
            "ACCOUNT",
            PostingSide::Debit,
            make_money(Currency::Usd, 100))
            .value());
    postings.push_back(
        Posting::create(
            make_id<PostingId>("P-TWO-C"),
            make_id<TradeId>("TRD-1001"),
            1U,
            "ACCOUNT",
            PostingSide::Credit,
            make_money(Currency::Usd, 100))
            .value());
    const LedgerEntry original =
        LedgerEntry::create(std::move(postings)).value();

    const auto result = build_reversal_entry(original, reversal_ids());

    ASSERT_TRUE(result.has_error());
    EXPECT_EQ(result.error(), PostingPolicyError::InvalidOriginalShape);
}

TEST(PostingPolicyTest, RejectsMalformedBalancedFourPostingShape) {
    const LedgerEntry base_entry = confirmation_entry();
    const auto& base = base_entry.postings();
    std::vector<Posting> postings;
    postings.reserve(base.size());
    for (const Posting& posting : base) {
        postings.push_back(posting);
    }
    std::swap(postings[0U], postings[1U]);
    const LedgerEntry malformed =
        LedgerEntry::create(std::move(postings)).value();

    const auto result = build_reversal_entry(malformed, reversal_ids());

    ASSERT_TRUE(result.has_error());
    EXPECT_EQ(result.error(), PostingPolicyError::InvalidOriginalShape);
}

TEST(PostingPolicyTest, RejectsReversalIdThatMatchesItsOriginal) {
    const LedgerEntry original = confirmation_entry();
    auto ids = reversal_ids();
    ids.pay_control_credit = original.postings()[0U].id();

    const auto result = build_reversal_entry(original, ids);

    ASSERT_TRUE(result.has_error());
    EXPECT_EQ(result.error(), PostingPolicyError::PostingSelfReversal);
}

}  // namespace
}  // namespace backbook::domain
