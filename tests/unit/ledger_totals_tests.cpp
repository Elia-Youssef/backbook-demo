#include "backbook/domain/ledger_totals.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace backbook::domain {
namespace {

[[nodiscard]] Posting make_posting(
    const std::string_view posting_id,
    const PostingSide side,
    const Currency currency,
    const Money::MinorUnits minor_units) {
    auto id = PostingId::parse(posting_id);
    auto trade_id = TradeId::parse("TRD-1001");
    auto amount = Money::from_minor_units(currency, minor_units);

    EXPECT_TRUE(id);
    EXPECT_TRUE(trade_id);
    EXPECT_TRUE(amount);

    auto posting = Posting::create(
        std::move(id).value(),
        std::move(trade_id).value(),
        1U,
        "TEST-ACCOUNT",
        side,
        std::move(amount).value());
    EXPECT_TRUE(posting);
    return std::move(posting).value();
}

[[nodiscard]] LedgerEntry make_balanced_entry(
    const std::string_view debit_id,
    const std::string_view credit_id,
    const Currency currency,
    const Money::MinorUnits minor_units) {
    std::vector<Posting> postings;
    postings.push_back(make_posting(
        debit_id,
        PostingSide::Debit,
        currency,
        minor_units));
    postings.push_back(make_posting(
        credit_id,
        PostingSide::Credit,
        currency,
        minor_units));

    auto entry = LedgerEntry::create(std::move(postings));
    EXPECT_TRUE(entry);
    return std::move(entry).value();
}

[[nodiscard]] Money require_total(
    const LedgerTotals& totals,
    const Currency currency) {
    auto result = totals.total(currency);
    EXPECT_TRUE(result);
    return std::move(result).value();
}

TEST(LedgerTotalsTest, ZeroHasCanonicalTotalForEverySupportedCurrency) {
    const auto totals = LedgerTotals::zero();

    EXPECT_EQ(require_total(totals, Currency::Usd).format(), "0.00");
    EXPECT_EQ(require_total(totals, Currency::Jpy).format(), "0");
    EXPECT_EQ(require_total(totals, Currency::Kwd).format(), "0.000");
}

TEST(LedgerTotalsTest, DebitIsPositiveAndCreditIsNegative) {
    const auto debit = make_posting(
        "PST-DEBIT",
        PostingSide::Debit,
        Currency::Usd,
        12'345);
    const auto credit = make_posting(
        "PST-CREDIT",
        PostingSide::Credit,
        Currency::Usd,
        2'345);

    auto after_debit = LedgerTotals::zero().with_posting(debit);
    ASSERT_TRUE(after_debit);
    EXPECT_EQ(
        require_total(after_debit.value(), Currency::Usd).minor_units(),
        12'345);

    auto after_credit = after_debit.value().with_posting(credit);
    ASSERT_TRUE(after_credit);
    EXPECT_EQ(
        require_total(after_credit.value(), Currency::Usd).minor_units(),
        10'000);
}

TEST(LedgerTotalsTest, CurrenciesAccumulateIndependently) {
    const std::array postings{
        make_posting("PST-USD", PostingSide::Debit, Currency::Usd, 100),
        make_posting("PST-JPY", PostingSide::Credit, Currency::Jpy, 200),
        make_posting("PST-KWD", PostingSide::Debit, Currency::Kwd, 300),
    };

    auto totals = LedgerTotals::zero();
    for (const auto& posting : postings) {
        auto updated = totals.with_posting(posting);
        ASSERT_TRUE(updated);
        totals = std::move(updated).value();
    }

    EXPECT_EQ(require_total(totals, Currency::Usd).minor_units(), 100);
    EXPECT_EQ(require_total(totals, Currency::Jpy).minor_units(), -200);
    EXPECT_EQ(require_total(totals, Currency::Kwd).minor_units(), 300);
}

TEST(LedgerTotalsTest, BalancedEntryLeavesAllTotalsAtZero) {
    const auto entry = make_balanced_entry(
        "PST-USD-D",
        "PST-USD-C",
        Currency::Usd,
        100'000);

    auto totals = LedgerTotals::zero().with_entry(entry);

    ASSERT_TRUE(totals);
    EXPECT_EQ(totals.value(), LedgerTotals::zero());
}

TEST(LedgerTotalsTest, MultipleBalancedEntriesLeaveTotalsAtZero) {
    const std::array entries{
        make_balanced_entry(
            "PST-USD-D",
            "PST-USD-C",
            Currency::Usd,
            100'000),
        make_balanced_entry(
            "PST-JPY-D",
            "PST-JPY-C",
            Currency::Jpy,
            15'000'000),
        make_balanced_entry(
            "PST-KWD-D",
            "PST-KWD-C",
            Currency::Kwd,
            25'000),
    };

    auto totals = LedgerTotals::zero();
    for (const auto& entry : entries) {
        auto updated = totals.with_entry(entry);
        ASSERT_TRUE(updated);
        totals = std::move(updated).value();
    }

    EXPECT_EQ(totals, LedgerTotals::zero());
}

TEST(LedgerTotalsTest, PositiveOverflowIsTypedAndDoesNotMutatePriorState) {
    constexpr auto maximum = std::numeric_limits<Money::MinorUnits>::max();
    const auto maximum_debit = make_posting(
        "PST-MAX",
        PostingSide::Debit,
        Currency::Usd,
        maximum);
    const auto one_more = make_posting(
        "PST-ONE",
        PostingSide::Debit,
        Currency::Usd,
        1);

    auto at_maximum = LedgerTotals::zero().with_posting(maximum_debit);
    ASSERT_TRUE(at_maximum);
    const auto snapshot = at_maximum.value();

    const auto overflow = at_maximum.value().with_posting(one_more);

    ASSERT_TRUE(overflow.has_error());
    EXPECT_EQ(overflow.error(), LedgerTotalsError::Overflow);
    EXPECT_EQ(at_maximum.value(), snapshot);
    EXPECT_EQ(
        require_total(at_maximum.value(), Currency::Usd).minor_units(),
        maximum);
}

TEST(LedgerTotalsTest, FailingEntryUpdateDoesNotPublishPartialTotals) {
    constexpr auto maximum = std::numeric_limits<Money::MinorUnits>::max();
    const auto maximum_debit = make_posting(
        "PST-MAX",
        PostingSide::Debit,
        Currency::Usd,
        maximum);
    const auto balanced_entry = make_balanced_entry(
        "PST-ENTRY-D",
        "PST-ENTRY-C",
        Currency::Usd,
        1);

    auto at_maximum = LedgerTotals::zero().with_posting(maximum_debit);
    ASSERT_TRUE(at_maximum);
    const auto snapshot = at_maximum.value();

    const auto overflow = at_maximum.value().with_entry(balanced_entry);

    ASSERT_TRUE(overflow.has_error());
    EXPECT_EQ(overflow.error(), LedgerTotalsError::Overflow);
    EXPECT_EQ(at_maximum.value(), snapshot);
}

TEST(LedgerTotalsTest, NegativeOverflowIsTypedAndDoesNotMutatePriorState) {
    constexpr auto maximum = std::numeric_limits<Money::MinorUnits>::max();
    const auto maximum_credit = make_posting(
        "PST-MAX",
        PostingSide::Credit,
        Currency::Usd,
        maximum);
    const auto first_one = make_posting(
        "PST-ONE-A",
        PostingSide::Credit,
        Currency::Usd,
        1);
    const auto second_one = make_posting(
        "PST-ONE-B",
        PostingSide::Credit,
        Currency::Usd,
        1);

    auto at_negative_maximum =
        LedgerTotals::zero().with_posting(maximum_credit);
    ASSERT_TRUE(at_negative_maximum);
    auto at_minimum = at_negative_maximum.value().with_posting(first_one);
    ASSERT_TRUE(at_minimum);
    const auto snapshot = at_minimum.value();

    const auto overflow = at_minimum.value().with_posting(second_one);

    ASSERT_TRUE(overflow.has_error());
    EXPECT_EQ(overflow.error(), LedgerTotalsError::Overflow);
    EXPECT_EQ(at_minimum.value(), snapshot);
    EXPECT_EQ(
        require_total(at_minimum.value(), Currency::Usd).minor_units(),
        std::numeric_limits<Money::MinorUnits>::min());
}

TEST(LedgerTotalsTest, RunningTotalsEqualIndependentPostingRecomputation) {
    const std::array postings{
        make_posting("PST-1", PostingSide::Debit, Currency::Usd, 150),
        make_posting("PST-2", PostingSide::Credit, Currency::Usd, 25),
        make_posting("PST-3", PostingSide::Credit, Currency::Jpy, 800),
        make_posting("PST-4", PostingSide::Debit, Currency::Kwd, 75),
    };

    auto running = LedgerTotals::zero();
    for (const auto& posting : postings) {
        auto updated = running.with_posting(posting);
        ASSERT_TRUE(updated);
        running = std::move(updated).value();
    }

    const auto recomputed = recompute_ledger_totals(postings);

    ASSERT_TRUE(recomputed);
    EXPECT_EQ(running, recomputed.value());
}

TEST(LedgerTotalsTest, RunningTotalsEqualIndependentEntryRecomputation) {
    const std::array entries{
        make_balanced_entry(
            "PST-USD-D",
            "PST-USD-C",
            Currency::Usd,
            100'000),
        make_balanced_entry(
            "PST-KWD-D",
            "PST-KWD-C",
            Currency::Kwd,
            25'000),
    };

    auto running = LedgerTotals::zero();
    for (const auto& entry : entries) {
        auto updated = running.with_entry(entry);
        ASSERT_TRUE(updated);
        running = std::move(updated).value();
    }

    const auto recomputed = recompute_ledger_totals(entries);

    ASSERT_TRUE(recomputed);
    EXPECT_EQ(running, recomputed.value());
}

TEST(LedgerTotalsTest, IndependentRecomputationDetectsOverflow) {
    constexpr auto maximum = std::numeric_limits<Money::MinorUnits>::max();
    const std::array postings{
        make_posting("PST-MAX", PostingSide::Debit, Currency::Usd, maximum),
        make_posting("PST-ONE", PostingSide::Debit, Currency::Usd, 1),
    };

    const auto recomputed = recompute_ledger_totals(postings);

    ASSERT_TRUE(recomputed.has_error());
    EXPECT_EQ(recomputed.error(), LedgerTotalsError::Overflow);
}

TEST(LedgerTotalsTest, UnsupportedCurrencyLookupReturnsTypedFailure) {
    const auto result =
        LedgerTotals::zero().total(static_cast<Currency>(0xffU));

    ASSERT_TRUE(result.has_error());
    EXPECT_EQ(result.error(), LedgerTotalsError::UnsupportedCurrency);
}

}  // namespace
}  // namespace backbook::domain
