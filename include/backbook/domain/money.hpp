#pragma once

#include "backbook/domain/outcome.hpp"

#include <concepts>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace backbook::domain {

enum class Currency : std::uint8_t {
    Usd = 0,
    Jpy = 1,
    Kwd = 2,
};

enum class MoneyError : std::uint8_t {
    InvalidFormat,
    NegativeZero,
    Overflow,
    CurrencyMismatch,
    UnsupportedCurrency,
};

[[nodiscard]] constexpr bool is_supported_currency(Currency currency) noexcept {
    switch (currency) {
    case Currency::Usd:
    case Currency::Jpy:
    case Currency::Kwd:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr std::optional<std::uint8_t> currency_exponent(
    Currency currency) noexcept {
    switch (currency) {
    case Currency::Usd:
        return std::uint8_t{2};
    case Currency::Jpy:
        return std::uint8_t{0};
    case Currency::Kwd:
        return std::uint8_t{3};
    }
    return std::nullopt;
}

[[nodiscard]] constexpr std::optional<std::string_view> currency_code(
    Currency currency) noexcept {
    switch (currency) {
    case Currency::Usd:
        return "USD";
    case Currency::Jpy:
        return "JPY";
    case Currency::Kwd:
        return "KWD";
    }
    return std::nullopt;
}

[[nodiscard]] constexpr std::optional<std::string_view> currency_name(
    Currency currency) noexcept {
    switch (currency) {
    case Currency::Usd:
        return "US dollar";
    case Currency::Jpy:
        return "Japanese yen";
    case Currency::Kwd:
        return "Kuwaiti dinar";
    }
    return std::nullopt;
}

// Money is always stored as an integer number of minor units. Decimal parsing
// happens only at the boundary, so calculations never depend on floating point.
class Money final {
public:
    using MinorUnits = std::int64_t;

    [[nodiscard]] static Outcome<Money, MoneyError> from_minor_units(
        Currency currency,
        MinorUnits minor_units);

    template <std::floating_point Floating>
    [[nodiscard]] static Outcome<Money, MoneyError> from_minor_units(Currency, Floating) = delete;

    [[nodiscard]] static Outcome<Money, MoneyError> parse(
        Currency currency,
        std::string_view decimal);

    [[nodiscard]] constexpr Currency currency() const noexcept {
        return currency_;
    }

    [[nodiscard]] constexpr MinorUnits minor_units() const noexcept {
        return minor_units_;
    }

    [[nodiscard]] std::string format() const;

    [[nodiscard]] Outcome<Money, MoneyError> checked_add(const Money& other) const noexcept;
    [[nodiscard]] Outcome<Money, MoneyError> checked_subtract(const Money& other) const noexcept;
    [[nodiscard]] Outcome<Money, MoneyError> checked_negate() const noexcept;

    [[nodiscard]] friend constexpr bool operator==(const Money&, const Money&) noexcept = default;

private:
    constexpr Money(Currency currency, MinorUnits minor_units) noexcept
        : currency_(currency), minor_units_(minor_units) {}

    Currency currency_;
    MinorUnits minor_units_;
};

}  // namespace backbook::domain
