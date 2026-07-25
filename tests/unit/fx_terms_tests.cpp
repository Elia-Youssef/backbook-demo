#include "backbook/domain/fx_terms.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace backbook::domain {
namespace {

static_assert(!std::is_default_constructible_v<FxTerms>);
static_assert(!std::is_aggregate_v<FxTerms>);
static_assert(!std::is_constructible_v<
              FxTerms,
              InstrumentKind,
              IsoDate,
              IsoDate,
              Money,
              Money>);
static_assert(std::is_same_v<
              decltype(std::declval<const FxTerms&>().trade_date()),
              const IsoDate&>);
static_assert(std::is_same_v<
              decltype(std::declval<const FxTerms&>().value_date()),
              const IsoDate&>);
static_assert(std::is_same_v<
              decltype(std::declval<const FxTerms&>().pay()),
              const Money&>);
static_assert(std::is_same_v<
              decltype(std::declval<const FxTerms&>().receive()),
              const Money&>);

[[nodiscard]] IsoDate make_date(const char* text) {
    return IsoDate::parse(text).value();
}

[[nodiscard]] Money make_money(const Currency currency, const std::int64_t minor_units) {
    return Money::from_minor_units(currency, minor_units).value();
}

TEST(FxTermsTest, CreatesSpotFromExplicitCashflowsAndDates) {
    const IsoDate trade_date = make_date("2026-07-25");
    const IsoDate value_date = make_date("2026-07-27");
    const Money pay = make_money(Currency::Usd, 10'000'000);
    const Money receive = make_money(Currency::Jpy, 1'500'000'000);

    const auto result =
        FxTerms::create(InstrumentKind::FxSpot, trade_date, value_date, pay, receive);

    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().kind(), InstrumentKind::FxSpot);
    EXPECT_EQ(result.value().trade_date(), trade_date);
    EXPECT_EQ(result.value().value_date(), value_date);
    EXPECT_EQ(result.value().pay(), pay);
    EXPECT_EQ(result.value().receive(), receive);
}

TEST(FxTermsTest, CreatesForwardFromExplicitCashflowsAndDates) {
    const IsoDate trade_date = make_date("2026-07-25");
    const IsoDate value_date = make_date("2026-10-25");
    const Money pay = make_money(Currency::Kwd, 125'000);
    const Money receive = make_money(Currency::Usd, 405'000);

    const auto result =
        FxTerms::create(InstrumentKind::FxForward, trade_date, value_date, pay, receive);

    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().kind(), InstrumentKind::FxForward);
    EXPECT_EQ(result.value().trade_date(), trade_date);
    EXPECT_EQ(result.value().value_date(), value_date);
    EXPECT_EQ(result.value().pay(), pay);
    EXPECT_EQ(result.value().receive(), receive);
}

TEST(FxTermsTest, RejectsZeroAndNegativePayAmounts) {
    const IsoDate trade_date = make_date("2026-07-25");
    const IsoDate value_date = make_date("2026-07-27");
    const Money receive = make_money(Currency::Jpy, 1);

    for (const std::int64_t minor_units : {std::int64_t{0}, std::int64_t{-1}}) {
        const auto result = FxTerms::create(
            InstrumentKind::FxSpot,
            trade_date,
            value_date,
            make_money(Currency::Usd, minor_units),
            receive);

        ASSERT_FALSE(result);
        EXPECT_EQ(result.error(), FxTermsError::NonPositivePayAmount);
    }
}

TEST(FxTermsTest, RejectsZeroAndNegativeReceiveAmounts) {
    const IsoDate trade_date = make_date("2026-07-25");
    const IsoDate value_date = make_date("2026-07-27");
    const Money pay = make_money(Currency::Usd, 1);

    for (const std::int64_t minor_units : {std::int64_t{0}, std::int64_t{-1}}) {
        const auto result = FxTerms::create(
            InstrumentKind::FxSpot,
            trade_date,
            value_date,
            pay,
            make_money(Currency::Jpy, minor_units));

        ASSERT_FALSE(result);
        EXPECT_EQ(result.error(), FxTermsError::NonPositiveReceiveAmount);
    }
}

TEST(FxTermsTest, RejectsCashflowsInTheSameCurrency) {
    const auto result = FxTerms::create(
        InstrumentKind::FxForward,
        make_date("2026-07-25"),
        make_date("2026-10-25"),
        make_money(Currency::Usd, 10'000),
        make_money(Currency::Usd, 12'000));

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), FxTermsError::SameCurrency);
}

TEST(FxTermsTest, RejectsInvalidCastCreatedInstrumentKind) {
    const auto result = FxTerms::create(
        static_cast<InstrumentKind>(255),
        make_date("2026-07-25"),
        make_date("2026-07-27"),
        make_money(Currency::Usd, 10'000),
        make_money(Currency::Jpy, 1'500'000));

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), FxTermsError::InvalidInstrumentKind);
}

TEST(FxTermsTest, SpotAndForwardUseIdenticalValidationBeyondTheirKindLabel) {
    const IsoDate trade_date = make_date("2026-07-25");
    const IsoDate value_date = make_date("2026-07-24");
    const Money pay = make_money(Currency::Usd, 10'000);
    const Money receive = make_money(Currency::Jpy, 1'500'000);

    const auto spot =
        FxTerms::create(InstrumentKind::FxSpot, trade_date, value_date, pay, receive);
    const auto forward =
        FxTerms::create(InstrumentKind::FxForward, trade_date, value_date, pay, receive);

    ASSERT_TRUE(spot);
    ASSERT_TRUE(forward);
    EXPECT_EQ(spot.value().trade_date(), forward.value().trade_date());
    EXPECT_EQ(spot.value().value_date(), forward.value().value_date());
    EXPECT_EQ(spot.value().pay(), forward.value().pay());
    EXPECT_EQ(spot.value().receive(), forward.value().receive());
    EXPECT_NE(spot.value().kind(), forward.value().kind());
}

}  // namespace
}  // namespace backbook::domain
