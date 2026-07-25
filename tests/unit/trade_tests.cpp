#include "backbook/domain/trade.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace backbook::domain {
namespace {

static_assert(!std::is_default_constructible_v<Trade>);
static_assert(!std::is_aggregate_v<Trade>);
static_assert(!std::is_constructible_v<
              Trade,
              TradeId,
              std::uint32_t,
              BookId,
              CounterpartyId,
              NettingSetId,
              FxTerms,
              TradeState,
              std::optional<std::uint32_t>,
              std::optional<std::uint32_t>>);
static_assert(!std::is_default_constructible_v<TradeAmendment>);
static_assert(!std::is_aggregate_v<TradeAmendment>);
static_assert(std::is_same_v<
              decltype(std::declval<const Trade&>().terms()),
              const FxTerms&>);
static_assert(std::is_same_v<
              decltype(std::declval<const TradeAmendment&>().superseded()),
              const Trade&>);
static_assert(std::is_same_v<
              decltype(std::declval<const TradeAmendment&>().replacement()),
              const Trade&>);

template <typename IdType>
[[nodiscard]] IdType make_id(const char* text) {
    return IdType::parse(text).value();
}

[[nodiscard]] IsoDate make_date(const char* text) {
    return IsoDate::parse(text).value();
}

[[nodiscard]] Money make_money(
    const Currency currency,
    const std::int64_t minor_units) {
    return Money::from_minor_units(currency, minor_units).value();
}

[[nodiscard]] FxTerms make_terms(
    const InstrumentKind kind,
    const char* value_date,
    const Currency pay_currency,
    const std::int64_t pay_minor_units,
    const Currency receive_currency,
    const std::int64_t receive_minor_units) {
    return FxTerms::create(
               kind,
               make_date("2026-07-25"),
               make_date(value_date),
               make_money(pay_currency, pay_minor_units),
               make_money(receive_currency, receive_minor_units))
        .value();
}

[[nodiscard]] Trade capture_trade() {
    return Trade::capture(
        make_id<TradeId>("TRD-1001"),
        make_id<BookId>("BOOK-FX-1"),
        make_id<CounterpartyId>("CPTY-A"),
        make_id<NettingSetId>("NET-A"),
        make_terms(
            InstrumentKind::FxSpot,
            "2026-07-27",
            Currency::Usd,
            10'000'000,
            Currency::Jpy,
            1'500'000'000));
}

[[nodiscard]] Trade confirm_trade(const Trade& captured) {
    return captured.apply(TradeAction::ConfirmTrade).value();
}

TEST(TradeTest, CapturesVersionOneWithNoVersionLinks) {
    const Trade trade = capture_trade();

    EXPECT_EQ(trade.id().value(), "TRD-1001");
    EXPECT_EQ(trade.version(), 1U);
    EXPECT_EQ(trade.kind(), InstrumentKind::FxSpot);
    EXPECT_EQ(trade.book_id().value(), "BOOK-FX-1");
    EXPECT_EQ(trade.counterparty_id().value(), "CPTY-A");
    EXPECT_EQ(trade.netting_set_id().value(), "NET-A");
    EXPECT_EQ(trade.state(), TradeState::Captured);
    EXPECT_EQ(trade.terms().pay(), make_money(Currency::Usd, 10'000'000));
    EXPECT_EQ(
        trade.terms().receive(),
        make_money(Currency::Jpy, 1'500'000'000));
    EXPECT_FALSE(trade.supersedes().has_value());
    EXPECT_FALSE(trade.superseded_by().has_value());
}

TEST(TradeTest, AppliesOrdinaryLifecycleTransitionToACopy) {
    const Trade captured = capture_trade();

    const auto result = captured.apply(TradeAction::ConfirmTrade);

    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().state(), TradeState::Confirmed);
    EXPECT_EQ(result.value().id(), captured.id());
    EXPECT_EQ(result.value().version(), captured.version());
    EXPECT_EQ(result.value().book_id(), captured.book_id());
    EXPECT_EQ(result.value().counterparty_id(), captured.counterparty_id());
    EXPECT_EQ(result.value().netting_set_id(), captured.netting_set_id());
    EXPECT_EQ(result.value().terms(), captured.terms());
    EXPECT_EQ(result.value().supersedes(), captured.supersedes());
    EXPECT_EQ(result.value().superseded_by(), captured.superseded_by());
    EXPECT_EQ(captured.state(), TradeState::Captured);
}

TEST(TradeTest, RejectsBookingAndCompositeAmendmentThroughOrdinaryApply) {
    const Trade captured = capture_trade();
    const Trade confirmed = confirm_trade(captured);

    const auto booking = captured.apply(TradeAction::BookTrade);
    const auto amendment = confirmed.apply(TradeAction::AmendTrade);

    ASSERT_FALSE(booking);
    EXPECT_EQ(booking.error(), LifecycleError::IllegalTransition);
    ASSERT_FALSE(amendment);
    EXPECT_EQ(amendment.error(), LifecycleError::IllegalTransition);
    EXPECT_EQ(captured.state(), TradeState::Captured);
    EXPECT_EQ(confirmed.state(), TradeState::Confirmed);
}

TEST(TradeTest, PropagatesInvalidLifecycleActionWithoutChangingSource) {
    const Trade captured = capture_trade();

    const auto result = captured.apply(static_cast<TradeAction>(0xFFU));

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), LifecycleError::InvalidAction);
    EXPECT_EQ(captured.state(), TradeState::Captured);
    EXPECT_EQ(captured.version(), 1U);
}

TEST(TradeTest, AmendmentReturnsExactLinkedSupersededAndReplacementPair) {
    const Trade captured = capture_trade();
    const Trade confirmed = confirm_trade(captured);
    const FxTerms replacement_terms = make_terms(
        InstrumentKind::FxForward,
        "2026-10-25",
        Currency::Usd,
        10'125'000,
        Currency::Jpy,
        1'518'750'000);

    const auto result = amend_trade(confirmed, replacement_terms);

    ASSERT_TRUE(result);
    const Trade& superseded = result.value().superseded();
    const Trade& replacement = result.value().replacement();

    EXPECT_EQ(superseded.id(), confirmed.id());
    EXPECT_EQ(superseded.version(), 1U);
    EXPECT_EQ(superseded.state(), TradeState::Superseded);
    EXPECT_EQ(superseded.terms(), confirmed.terms());
    EXPECT_FALSE(superseded.supersedes().has_value());
    ASSERT_TRUE(superseded.superseded_by().has_value());
    EXPECT_EQ(*superseded.superseded_by(), 2U);

    EXPECT_EQ(replacement.id(), confirmed.id());
    EXPECT_EQ(replacement.version(), 2U);
    EXPECT_EQ(replacement.state(), TradeState::Confirmed);
    EXPECT_EQ(replacement.terms(), replacement_terms);
    ASSERT_TRUE(replacement.supersedes().has_value());
    EXPECT_EQ(*replacement.supersedes(), superseded.version());
    EXPECT_FALSE(replacement.superseded_by().has_value());
    EXPECT_EQ(*superseded.superseded_by(), replacement.version());
}

TEST(TradeTest, AmendmentPreservesPartiesAndLeavesInputUnchanged) {
    const Trade confirmed = confirm_trade(capture_trade());
    const Trade original_snapshot = confirmed;

    const auto result = amend_trade(
        confirmed,
        make_terms(
            InstrumentKind::FxSpot,
            "2026-07-28",
            Currency::Kwd,
            30'000,
            Currency::Usd,
            97'500));

    ASSERT_TRUE(result);
    for (const Trade* version :
         {&result.value().superseded(), &result.value().replacement()}) {
        EXPECT_EQ(version->id(), confirmed.id());
        EXPECT_EQ(version->book_id(), confirmed.book_id());
        EXPECT_EQ(version->counterparty_id(), confirmed.counterparty_id());
        EXPECT_EQ(version->netting_set_id(), confirmed.netting_set_id());
    }
    EXPECT_EQ(confirmed, original_snapshot);
    EXPECT_EQ(confirmed.state(), TradeState::Confirmed);
    EXPECT_FALSE(confirmed.superseded_by().has_value());
}

TEST(TradeTest, SubsequentAmendmentPreservesPredecessorAndCreatesReciprocalLinks) {
    const Trade confirmed = confirm_trade(capture_trade());
    const auto first = amend_trade(
        confirmed,
        make_terms(
            InstrumentKind::FxSpot,
            "2026-07-28",
            Currency::Usd,
            10'125'000,
            Currency::Jpy,
            1'518'750'000));
    ASSERT_TRUE(first);

    const auto second = amend_trade(
        first.value().replacement(),
        make_terms(
            InstrumentKind::FxForward,
            "2026-10-25",
            Currency::Usd,
            10'200'000,
            Currency::Jpy,
            1'530'000'000));

    ASSERT_TRUE(second);
    const Trade& superseded = second.value().superseded();
    const Trade& replacement = second.value().replacement();
    ASSERT_TRUE(superseded.supersedes().has_value());
    ASSERT_TRUE(superseded.superseded_by().has_value());
    ASSERT_TRUE(replacement.supersedes().has_value());
    EXPECT_EQ(*superseded.supersedes(), 1U);
    EXPECT_EQ(*superseded.superseded_by(), 3U);
    EXPECT_EQ(*replacement.supersedes(), 2U);
    EXPECT_EQ(replacement.version(), 3U);
}

TEST(TradeTest, RejectsAmendmentUnlessCurrentVersionIsConfirmed) {
    const Trade captured = capture_trade();
    const auto cancelled = captured.apply(TradeAction::CancelTrade);
    ASSERT_TRUE(cancelled);
    const FxTerms replacement_terms = make_terms(
        InstrumentKind::FxSpot,
        "2026-07-28",
        Currency::Usd,
        10'125'000,
        Currency::Jpy,
        1'518'750'000);

    const auto captured_result = amend_trade(captured, replacement_terms);
    const auto cancelled_result =
        amend_trade(cancelled.value(), replacement_terms);

    ASSERT_FALSE(captured_result);
    EXPECT_EQ(
        captured_result.error(),
        TradeError::AmendmentRequiresConfirmed);
    ASSERT_FALSE(cancelled_result);
    EXPECT_EQ(
        cancelled_result.error(),
        TradeError::AmendmentRequiresConfirmed);
}

TEST(TradeTest, VersionIncrementRejectsZeroAndUnsignedOverflow) {
    const auto zero = next_trade_version(0U);
    const auto maximum =
        next_trade_version(std::numeric_limits<std::uint32_t>::max());
    const auto valid =
        next_trade_version(std::numeric_limits<std::uint32_t>::max() - 1U);

    ASSERT_FALSE(zero);
    EXPECT_EQ(zero.error(), TradeError::InvalidVersion);
    ASSERT_FALSE(maximum);
    EXPECT_EQ(maximum.error(), TradeError::VersionOverflow);
    ASSERT_TRUE(valid);
    EXPECT_EQ(valid.value(), std::numeric_limits<std::uint32_t>::max());
}

}  // namespace
}  // namespace backbook::domain
