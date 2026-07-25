#include "backbook/domain/money.hpp"

#include <gtest/gtest.h>

#include <concepts>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>

namespace backbook::domain {
namespace {

template <typename T>
concept AcceptedMinorUnitArgument = requires(T value) {
    Money::from_minor_units(Currency::Usd, value);
};

static_assert(AcceptedMinorUnitArgument<std::int64_t>);
static_assert(!AcceptedMinorUnitArgument<float>);
static_assert(!AcceptedMinorUnitArgument<double>);
static_assert(!AcceptedMinorUnitArgument<long double>);
static_assert(!std::is_constructible_v<Money, Currency, float>);
static_assert(!std::is_constructible_v<Money, Currency, double>);
static_assert(!std::is_constructible_v<Money, Currency, long double>);

[[nodiscard]] Money make_money(const Currency currency, const std::int64_t minor_units) {
    return Money::from_minor_units(currency, minor_units).value();
}

TEST(CurrencyTest, ExposesStableCodesNamesAndExponents) {
    EXPECT_EQ(currency_code(Currency::Usd).value(), "USD");
    EXPECT_EQ(currency_name(Currency::Usd).value(), "US dollar");
    EXPECT_EQ(currency_exponent(Currency::Usd).value(), 2);

    EXPECT_EQ(currency_code(Currency::Jpy).value(), "JPY");
    EXPECT_EQ(currency_name(Currency::Jpy).value(), "Japanese yen");
    EXPECT_EQ(currency_exponent(Currency::Jpy).value(), 0);

    EXPECT_EQ(currency_code(Currency::Kwd).value(), "KWD");
    EXPECT_EQ(currency_name(Currency::Kwd).value(), "Kuwaiti dinar");
    EXPECT_EQ(currency_exponent(Currency::Kwd).value(), 3);
}

TEST(MoneyTest, BuildsIntegralMinorUnitsWithoutChangingTheirValue) {
    const auto result = Money::from_minor_units(Currency::Usd, std::int64_t{-12'345});

    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().currency(), Currency::Usd);
    EXPECT_EQ(result.value().minor_units(), -12'345);
    EXPECT_EQ(result.value().format(), "-123.45");
}

TEST(MoneyTest, RejectsUnsupportedCurrencyAcrossEveryPublicConstructionPath) {
    constexpr Currency unsupported = static_cast<Currency>(255);
    static_assert(!is_supported_currency(unsupported));
    static_assert(!currency_exponent(unsupported).has_value());
    static_assert(!currency_code(unsupported).has_value());
    static_assert(!currency_name(unsupported).has_value());

    const auto integral = Money::from_minor_units(unsupported, 100);
    const auto decimal = Money::parse(unsupported, "100");

    ASSERT_FALSE(integral);
    ASSERT_FALSE(decimal);
    EXPECT_EQ(integral.error(), MoneyError::UnsupportedCurrency);
    EXPECT_EQ(decimal.error(), MoneyError::UnsupportedCurrency);
}

TEST(MoneyTest, ParsesAndFormatsEverySupportedCurrencyExactly) {
    const auto usd = Money::parse(Currency::Usd, "101250.00");
    ASSERT_TRUE(usd);
    EXPECT_EQ(usd.value().minor_units(), 10'125'000);
    EXPECT_EQ(usd.value().format(), "101250.00");

    const auto jpy = Money::parse(Currency::Jpy, "15187500");
    ASSERT_TRUE(jpy);
    EXPECT_EQ(jpy.value().minor_units(), 15'187'500);
    EXPECT_EQ(jpy.value().format(), "15187500");

    const auto kwd = Money::parse(Currency::Kwd, "1234.567");
    ASSERT_TRUE(kwd);
    EXPECT_EQ(kwd.value().minor_units(), 1'234'567);
    EXPECT_EQ(kwd.value().format(), "1234.567");
}

TEST(MoneyTest, PadsAcceptedShortFractionalPartsToTheCurrencyExponent) {
    const auto usd = Money::parse(Currency::Usd, "12.3");
    const auto kwd_one = Money::parse(Currency::Kwd, "12.3");
    const auto kwd_two = Money::parse(Currency::Kwd, "12.34");

    ASSERT_TRUE(usd);
    ASSERT_TRUE(kwd_one);
    ASSERT_TRUE(kwd_two);
    EXPECT_EQ(usd.value().minor_units(), 1'230);
    EXPECT_EQ(usd.value().format(), "12.30");
    EXPECT_EQ(kwd_one.value().minor_units(), 12'300);
    EXPECT_EQ(kwd_one.value().format(), "12.300");
    EXPECT_EQ(kwd_two.value().minor_units(), 12'340);
    EXPECT_EQ(kwd_two.value().format(), "12.340");
}

TEST(MoneyTest, FormatsSubUnitAndZeroValuesCanonically) {
    EXPECT_EQ(make_money(Currency::Usd, 0).format(), "0.00");
    EXPECT_EQ(make_money(Currency::Usd, 1).format(), "0.01");
    EXPECT_EQ(make_money(Currency::Usd, -1).format(), "-0.01");
    EXPECT_EQ(make_money(Currency::Jpy, 0).format(), "0");
    EXPECT_EQ(make_money(Currency::Kwd, 1).format(), "0.001");
}

TEST(MoneyTest, RejectsNonCanonicalDecimalGrammar) {
    constexpr std::string_view invalid_usd[] = {
        "",
        "-",
        "+1",
        "01",
        "00.00",
        "01.00",
        ".25",
        "1.",
        "1.234",
        " 1.00",
        "1.00 ",
        "1 000.00",
        "1,000.00",
        "1_000.00",
        "1e2",
        "1E2",
        "NaN",
        "inf",
        "1a",
        "--1",
    };

    for (const auto value : invalid_usd) {
        const auto result = Money::parse(Currency::Usd, value);
        EXPECT_FALSE(result) << value;
        EXPECT_EQ(result.error(), MoneyError::InvalidFormat) << value;
    }

    EXPECT_EQ(Money::parse(Currency::Jpy, "1.0").error(), MoneyError::InvalidFormat);
    EXPECT_EQ(Money::parse(Currency::Kwd, "1.0000").error(), MoneyError::InvalidFormat);
}

TEST(MoneyTest, RejectsEveryNegativeZeroSpelling) {
    constexpr std::string_view usd_negative_zeroes[] = {"-0", "-0.0", "-0.00"};
    for (const auto value : usd_negative_zeroes) {
        const auto result = Money::parse(Currency::Usd, value);
        ASSERT_FALSE(result) << value;
        EXPECT_EQ(result.error(), MoneyError::NegativeZero) << value;
    }

    EXPECT_EQ(Money::parse(Currency::Jpy, "-0").error(), MoneyError::NegativeZero);
    EXPECT_EQ(Money::parse(Currency::Kwd, "-0.000").error(), MoneyError::NegativeZero);
}

TEST(MoneyTest, ParsesSignedInt64BoundariesForEachCurrency) {
    const auto usd_max = Money::parse(Currency::Usd, "92233720368547758.07");
    const auto usd_min = Money::parse(Currency::Usd, "-92233720368547758.08");
    const auto jpy_max = Money::parse(Currency::Jpy, "9223372036854775807");
    const auto jpy_min = Money::parse(Currency::Jpy, "-9223372036854775808");
    const auto kwd_max = Money::parse(Currency::Kwd, "9223372036854775.807");
    const auto kwd_min = Money::parse(Currency::Kwd, "-9223372036854775.808");

    ASSERT_TRUE(usd_max);
    ASSERT_TRUE(usd_min);
    ASSERT_TRUE(jpy_max);
    ASSERT_TRUE(jpy_min);
    ASSERT_TRUE(kwd_max);
    ASSERT_TRUE(kwd_min);
    EXPECT_EQ(usd_max.value().minor_units(), std::numeric_limits<std::int64_t>::max());
    EXPECT_EQ(usd_min.value().minor_units(), std::numeric_limits<std::int64_t>::min());
    EXPECT_EQ(jpy_max.value().minor_units(), std::numeric_limits<std::int64_t>::max());
    EXPECT_EQ(jpy_min.value().minor_units(), std::numeric_limits<std::int64_t>::min());
    EXPECT_EQ(kwd_max.value().minor_units(), std::numeric_limits<std::int64_t>::max());
    EXPECT_EQ(kwd_min.value().minor_units(), std::numeric_limits<std::int64_t>::min());
    EXPECT_EQ(usd_min.value().format(), "-92233720368547758.08");
    EXPECT_EQ(jpy_min.value().format(), "-9223372036854775808");
    EXPECT_EQ(kwd_min.value().format(), "-9223372036854775.808");
}

TEST(MoneyTest, RejectsValuesBeyondSignedInt64Boundaries) {
    constexpr std::string_view overflow_values[] = {
        "92233720368547758.08",
        "-92233720368547758.09",
        "999999999999999999999999999999.99",
    };
    for (const auto value : overflow_values) {
        const auto result = Money::parse(Currency::Usd, value);
        ASSERT_FALSE(result) << value;
        EXPECT_EQ(result.error(), MoneyError::Overflow) << value;
    }

    EXPECT_EQ(
        Money::parse(Currency::Jpy, "9223372036854775808").error(),
        MoneyError::Overflow);
    EXPECT_EQ(
        Money::parse(Currency::Jpy, "-9223372036854775809").error(),
        MoneyError::Overflow);
    EXPECT_EQ(
        Money::parse(Currency::Kwd, "9223372036854775.808").error(),
        MoneyError::Overflow);
}

TEST(MoneyTest, PerformsCheckedSameCurrencyArithmetic) {
    const Money left = make_money(Currency::Usd, 125);
    const Money right = make_money(Currency::Usd, -25);

    const auto sum = left.checked_add(right);
    const auto difference = left.checked_subtract(right);
    const auto negated = left.checked_negate();

    ASSERT_TRUE(sum);
    ASSERT_TRUE(difference);
    ASSERT_TRUE(negated);
    EXPECT_EQ(sum.value(), make_money(Currency::Usd, 100));
    EXPECT_EQ(difference.value(), make_money(Currency::Usd, 150));
    EXPECT_EQ(negated.value(), make_money(Currency::Usd, -125));
}

TEST(MoneyTest, ReportsArithmeticOverflowWithoutUndefinedBehavior) {
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    const Money min_money = make_money(Currency::Usd, minimum);
    const Money max_money = make_money(Currency::Usd, maximum);
    const Money positive_one = make_money(Currency::Usd, 1);
    const Money negative_one = make_money(Currency::Usd, -1);

    EXPECT_EQ(max_money.checked_add(positive_one).error(), MoneyError::Overflow);
    EXPECT_EQ(min_money.checked_add(negative_one).error(), MoneyError::Overflow);
    EXPECT_EQ(min_money.checked_subtract(positive_one).error(), MoneyError::Overflow);
    EXPECT_EQ(max_money.checked_subtract(negative_one).error(), MoneyError::Overflow);
    EXPECT_EQ(min_money.checked_negate().error(), MoneyError::Overflow);
}

TEST(MoneyTest, ReportsTypedCurrencyMismatchForBinaryArithmetic) {
    const Money usd = make_money(Currency::Usd, 100);
    const Money jpy = make_money(Currency::Jpy, 100);

    const auto addition = usd.checked_add(jpy);
    const auto subtraction = usd.checked_subtract(jpy);

    ASSERT_FALSE(addition);
    ASSERT_FALSE(subtraction);
    EXPECT_EQ(addition.error(), MoneyError::CurrencyMismatch);
    EXPECT_EQ(subtraction.error(), MoneyError::CurrencyMismatch);
}

TEST(MoneyTest, EqualityIncludesCurrencyAndMinorUnits) {
    EXPECT_EQ(
        make_money(Currency::Usd, 100),
        make_money(Currency::Usd, 100));
    EXPECT_NE(
        make_money(Currency::Usd, 100),
        make_money(Currency::Usd, 101));
    EXPECT_NE(
        make_money(Currency::Usd, 100),
        make_money(Currency::Jpy, 100));
}

}  // namespace
}  // namespace backbook::domain
