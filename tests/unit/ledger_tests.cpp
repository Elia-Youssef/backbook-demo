#include "backbook/domain/ledger.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace backbook::domain {
namespace {

static_assert(!std::is_default_constructible_v<Posting>);
static_assert(!std::is_aggregate_v<Posting>);
static_assert(!std::is_constructible_v<
              Posting,
              PostingId,
              TradeId,
              std::uint32_t,
              std::string,
              PostingSide,
              Money,
              std::optional<PostingId>>);
static_assert(std::is_copy_constructible_v<Posting>);
static_assert(std::is_move_constructible_v<Posting>);

static_assert(!std::is_default_constructible_v<LedgerEntry>);
static_assert(!std::is_aggregate_v<LedgerEntry>);
static_assert(!std::is_constructible_v<LedgerEntry, std::vector<Posting>>);
static_assert(std::is_copy_constructible_v<LedgerEntry>);
static_assert(std::is_move_constructible_v<LedgerEntry>);

[[nodiscard]] PostingId posting_id(const std::string& value) {
    auto result = PostingId::parse(value);
    EXPECT_TRUE(result.has_value());
    return std::move(result).value();
}

[[nodiscard]] TradeId trade_id() {
    auto result = TradeId::parse("TRD-1001");
    EXPECT_TRUE(result.has_value());
    return std::move(result).value();
}

[[nodiscard]] Money money(
    const Currency currency,
    const std::int64_t minor_units) {
    auto result = Money::from_minor_units(currency, minor_units);
    EXPECT_TRUE(result.has_value());
    return std::move(result).value();
}

[[nodiscard]] Posting posting(
    const std::string& id,
    const Currency currency,
    const std::int64_t minor_units,
    const PostingSide side,
    std::string account = "ACCOUNT") {
    auto result = Posting::create(
        posting_id(id),
        trade_id(),
        1U,
        std::move(account),
        side,
        money(currency, minor_units));
    EXPECT_TRUE(result.has_value());
    return std::move(result).value();
}

TEST(PostingTest, CreatesPositivePostingAndExposesOwnedValues) {
    auto result = Posting::create(
        posting_id("P-1"),
        trade_id(),
        7U,
        "SETTLEMENT_PAYABLE:BOOK-FX-1",
        PostingSide::Credit,
        money(Currency::Usd, 10'000));

    ASSERT_TRUE(result.has_value());
    const auto& value = result.value();
    EXPECT_EQ(value.id().value(), "P-1");
    EXPECT_EQ(value.trade_id().value(), "TRD-1001");
    EXPECT_EQ(value.trade_version(), 7U);
    EXPECT_EQ(value.account(), "SETTLEMENT_PAYABLE:BOOK-FX-1");
    EXPECT_EQ(value.side(), PostingSide::Credit);
    EXPECT_EQ(value.amount(), money(Currency::Usd, 10'000));
    EXPECT_FALSE(value.reversal_of().has_value());
}

TEST(PostingTest, RejectsZeroAndNegativeAmounts) {
    auto zero = Posting::create(
        posting_id("P-ZERO"),
        trade_id(),
        1U,
        "ACCOUNT",
        PostingSide::Debit,
        money(Currency::Usd, 0));
    auto negative = Posting::create(
        posting_id("P-NEGATIVE"),
        trade_id(),
        1U,
        "ACCOUNT",
        PostingSide::Debit,
        money(Currency::Usd, -1));

    ASSERT_TRUE(zero.has_error());
    EXPECT_EQ(zero.error(), PostingError::NonPositiveAmount);
    ASSERT_TRUE(negative.has_error());
    EXPECT_EQ(negative.error(), PostingError::NonPositiveAmount);
}

TEST(PostingTest, RejectsZeroTradeVersion) {
    auto result = Posting::create(
        posting_id("P-1"),
        trade_id(),
        0U,
        "ACCOUNT",
        PostingSide::Debit,
        money(Currency::Usd, 1));

    ASSERT_TRUE(result.has_error());
    EXPECT_EQ(result.error(), PostingError::InvalidTradeVersion);
}

TEST(PostingTest, RejectsEmptyAccount) {
    auto result = Posting::create(
        posting_id("P-1"),
        trade_id(),
        1U,
        "",
        PostingSide::Debit,
        money(Currency::Usd, 1));

    ASSERT_TRUE(result.has_error());
    EXPECT_EQ(result.error(), PostingError::EmptyAccount);
}

TEST(PostingTest, RejectsInvalidSide) {
    auto result = Posting::create(
        posting_id("P-1"),
        trade_id(),
        1U,
        "ACCOUNT",
        static_cast<PostingSide>(255U),
        money(Currency::Usd, 1));

    ASSERT_TRUE(result.has_error());
    EXPECT_EQ(result.error(), PostingError::InvalidSide);
}

TEST(PostingTest, CreatesExactReversalFromOriginalPosting) {
    auto original = posting(
        "P-ORIGINAL",
        Currency::Kwd,
        50'125,
        PostingSide::Debit,
        "SETTLEMENT_RECEIVABLE:BOOK-FX-1");

    auto result = Posting::reverse(posting_id("P-REVERSAL"), original);

    ASSERT_TRUE(result.has_value());
    const auto& reversal = result.value();
    EXPECT_EQ(reversal.id().value(), "P-REVERSAL");
    EXPECT_EQ(reversal.trade_id(), original.trade_id());
    EXPECT_EQ(reversal.trade_version(), original.trade_version());
    EXPECT_EQ(reversal.account(), original.account());
    EXPECT_EQ(reversal.amount(), original.amount());
    EXPECT_EQ(reversal.side(), PostingSide::Credit);
    ASSERT_TRUE(reversal.reversal_of().has_value());
    EXPECT_EQ(reversal.reversal_of().value(), original.id());
}

TEST(PostingTest, ReversalSwapsCreditToDebit) {
    const auto original =
        posting("P-ORIGINAL", Currency::Jpy, 50, PostingSide::Credit);

    auto result = Posting::reverse(posting_id("P-REVERSAL"), original);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().side(), PostingSide::Debit);
}

TEST(PostingTest, RejectsReversalUsingOriginalPostingId) {
    const auto original =
        posting("P-SELF", Currency::Jpy, 50, PostingSide::Credit);

    auto result = Posting::reverse(original.id(), original);

    ASSERT_TRUE(result.has_error());
    EXPECT_EQ(result.error(), PostingError::SelfReversal);
}

TEST(LedgerEntryTest, ConstructsBalancedSingleCurrencyEntry) {
    std::vector<Posting> postings;
    postings.push_back(posting("P-1", Currency::Usd, 10'000, PostingSide::Debit));
    postings.push_back(posting("P-2", Currency::Usd, 10'000, PostingSide::Credit));

    auto result = LedgerEntry::create(std::move(postings));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().postings().size(), 2U);
}

TEST(LedgerEntryTest, ConstructsEntryBalancedIndependentlyAcrossCurrencies) {
    std::vector<Posting> postings;
    postings.push_back(posting("P-USD-D", Currency::Usd, 100, PostingSide::Debit));
    postings.push_back(posting("P-JPY-D", Currency::Jpy, 200, PostingSide::Debit));
    postings.push_back(posting("P-KWD-C", Currency::Kwd, 300, PostingSide::Credit));
    postings.push_back(posting("P-USD-C", Currency::Usd, 100, PostingSide::Credit));
    postings.push_back(posting("P-KWD-D", Currency::Kwd, 300, PostingSide::Debit));
    postings.push_back(posting("P-JPY-C", Currency::Jpy, 200, PostingSide::Credit));

    auto result = LedgerEntry::create(std::move(postings));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().postings().size(), 6U);
}

TEST(LedgerEntryTest, RejectsCrossCurrencyOffsettingImbalance) {
    std::vector<Posting> postings;
    postings.push_back(posting("P-USD-D", Currency::Usd, 100, PostingSide::Debit));
    postings.push_back(posting("P-JPY-C", Currency::Jpy, 100, PostingSide::Credit));

    auto result = LedgerEntry::create(std::move(postings));

    ASSERT_TRUE(result.has_error());
    EXPECT_EQ(result.error(), LedgerError::Unbalanced);
}

TEST(LedgerEntryTest, RejectsEmptyEntry) {
    auto result = LedgerEntry::create({});

    ASSERT_TRUE(result.has_error());
    EXPECT_EQ(result.error(), LedgerError::EmptyPostings);
}

TEST(LedgerEntryTest, RejectsDuplicatePostingIds) {
    std::vector<Posting> postings;
    postings.push_back(posting("P-SAME", Currency::Usd, 100, PostingSide::Debit));
    postings.push_back(posting("P-SAME", Currency::Usd, 100, PostingSide::Credit));

    auto result = LedgerEntry::create(std::move(postings));

    ASSERT_TRUE(result.has_error());
    EXPECT_EQ(result.error(), LedgerError::DuplicatePostingId);
}

TEST(LedgerEntryTest, RejectsUnrepresentableIntermediateBalance) {
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    std::vector<Posting> postings;
    postings.push_back(posting("P-D-MAX", Currency::Usd, maximum, PostingSide::Debit));
    postings.push_back(posting("P-D-ONE", Currency::Usd, 1, PostingSide::Debit));
    postings.push_back(posting("P-C-MAX", Currency::Usd, maximum, PostingSide::Credit));
    postings.push_back(posting("P-C-ONE", Currency::Usd, 1, PostingSide::Credit));

    auto result = LedgerEntry::create(std::move(postings));

    ASSERT_TRUE(result.has_error());
    EXPECT_EQ(result.error(), LedgerError::BalanceOverflow);
}

TEST(LedgerEntryTest, PreservesCallerPostingOrder) {
    std::vector<Posting> postings;
    postings.push_back(posting("P-FIRST", Currency::Kwd, 42, PostingSide::Credit));
    postings.push_back(posting("P-SECOND", Currency::Kwd, 42, PostingSide::Debit));

    auto result = LedgerEntry::create(std::move(postings));

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value().postings().size(), 2U);
    EXPECT_EQ(result.value().postings()[0].id().value(), "P-FIRST");
    EXPECT_EQ(result.value().postings()[1].id().value(), "P-SECOND");
}

}  // namespace
}  // namespace backbook::domain
