#include "backbook/domain/money.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace backbook::domain {
namespace {

[[nodiscard]] constexpr bool is_ascii_digit(char value) noexcept {
    return value >= '0' && value <= '9';
}

[[nodiscard]] constexpr std::uint64_t absolute_magnitude(std::int64_t value) noexcept {
    if (value >= 0) {
        return static_cast<std::uint64_t>(value);
    }
    return static_cast<std::uint64_t>(-(value + 1)) + 1U;
}

}  // namespace

Outcome<Money, MoneyError> Money::from_minor_units(
    const Currency currency,
    const MinorUnits minor_units) {
    if (!is_supported_currency(currency)) {
        return Outcome<Money, MoneyError>::failure(MoneyError::UnsupportedCurrency);
    }
    return Outcome<Money, MoneyError>::success(Money(currency, minor_units));
}

Outcome<Money, MoneyError> Money::parse(
    const Currency currency,
    const std::string_view decimal) {
    const auto exponent_result = currency_exponent(currency);
    if (!exponent_result.has_value()) {
        return Outcome<Money, MoneyError>::failure(MoneyError::UnsupportedCurrency);
    }
    if (decimal.empty()) {
        return Outcome<Money, MoneyError>::failure(MoneyError::InvalidFormat);
    }

    std::size_t position = 0;
    const bool negative = decimal.front() == '-';
    if (negative) {
        position = 1;
        if (position == decimal.size()) {
            return Outcome<Money, MoneyError>::failure(MoneyError::InvalidFormat);
        }
    }

    const std::size_t integer_begin = position;
    while (position < decimal.size() && is_ascii_digit(decimal[position])) {
        ++position;
    }
    const std::size_t integer_length = position - integer_begin;
    if (integer_length == 0U) {
        return Outcome<Money, MoneyError>::failure(MoneyError::InvalidFormat);
    }
    if (integer_length > 1U && decimal[integer_begin] == '0') {
        return Outcome<Money, MoneyError>::failure(MoneyError::InvalidFormat);
    }

    const std::uint8_t exponent = *exponent_result;
    std::size_t fraction_begin = position;
    std::size_t fraction_length = 0;
    if (position < decimal.size()) {
        if (decimal[position] != '.' || exponent == 0U) {
            return Outcome<Money, MoneyError>::failure(MoneyError::InvalidFormat);
        }
        ++position;
        fraction_begin = position;
        while (position < decimal.size() && is_ascii_digit(decimal[position])) {
            ++position;
        }
        fraction_length = position - fraction_begin;
        if (fraction_length == 0U || fraction_length > exponent) {
            return Outcome<Money, MoneyError>::failure(MoneyError::InvalidFormat);
        }
    }
    if (position != decimal.size()) {
        return Outcome<Money, MoneyError>::failure(MoneyError::InvalidFormat);
    }

    const std::uint64_t positive_limit =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    const std::uint64_t negative_limit = positive_limit + 1U;
    const std::uint64_t limit = negative ? negative_limit : positive_limit;
    std::uint64_t magnitude = 0;

    const auto append_digit = [&magnitude, limit](const char digit) -> bool {
        const auto numeric_digit = static_cast<std::uint64_t>(digit - '0');
        if (magnitude > (limit - numeric_digit) / 10U) {
            return false;
        }
        magnitude = magnitude * 10U + numeric_digit;
        return true;
    };

    for (std::size_t index = integer_begin; index < integer_begin + integer_length; ++index) {
        if (!append_digit(decimal[index])) {
            return Outcome<Money, MoneyError>::failure(MoneyError::Overflow);
        }
    }
    for (std::size_t index = fraction_begin; index < fraction_begin + fraction_length; ++index) {
        if (!append_digit(decimal[index])) {
            return Outcome<Money, MoneyError>::failure(MoneyError::Overflow);
        }
    }
    for (std::size_t index = fraction_length; index < exponent; ++index) {
        if (!append_digit('0')) {
            return Outcome<Money, MoneyError>::failure(MoneyError::Overflow);
        }
    }

    if (negative && magnitude == 0U) {
        return Outcome<Money, MoneyError>::failure(MoneyError::NegativeZero);
    }

    std::int64_t minor_units = 0;
    if (negative) {
        if (magnitude == negative_limit) {
            minor_units = std::numeric_limits<std::int64_t>::min();
        } else {
            minor_units = -static_cast<std::int64_t>(magnitude);
        }
    } else {
        minor_units = static_cast<std::int64_t>(magnitude);
    }
    return Outcome<Money, MoneyError>::success(Money(currency, minor_units));
}

std::string Money::format() const {
    const bool negative = minor_units_ < 0;
    const std::uint64_t magnitude = absolute_magnitude(minor_units_);
    const std::uint8_t exponent = *currency_exponent(currency_);

    std::string digits = std::to_string(magnitude);
    if (exponent != 0U) {
        const auto required_digits = static_cast<std::size_t>(exponent) + 1U;
        if (digits.size() < required_digits) {
            digits.insert(0, required_digits - digits.size(), '0');
        }
        digits.insert(digits.size() - exponent, 1, '.');
    }
    if (negative) {
        digits.insert(digits.begin(), '-');
    }
    return digits;
}

Outcome<Money, MoneyError> Money::checked_add(const Money& other) const noexcept {
    if (currency_ != other.currency_) {
        return Outcome<Money, MoneyError>::failure(MoneyError::CurrencyMismatch);
    }

    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    if ((other.minor_units_ > 0 && minor_units_ > maximum - other.minor_units_)
        || (other.minor_units_ < 0 && minor_units_ < minimum - other.minor_units_)) {
        return Outcome<Money, MoneyError>::failure(MoneyError::Overflow);
    }
    return Outcome<Money, MoneyError>::success(
        Money(currency_, minor_units_ + other.minor_units_));
}

Outcome<Money, MoneyError> Money::checked_subtract(const Money& other) const noexcept {
    if (currency_ != other.currency_) {
        return Outcome<Money, MoneyError>::failure(MoneyError::CurrencyMismatch);
    }

    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    if ((other.minor_units_ > 0 && minor_units_ < minimum + other.minor_units_)
        || (other.minor_units_ < 0 && minor_units_ > maximum + other.minor_units_)) {
        return Outcome<Money, MoneyError>::failure(MoneyError::Overflow);
    }
    return Outcome<Money, MoneyError>::success(
        Money(currency_, minor_units_ - other.minor_units_));
}

Outcome<Money, MoneyError> Money::checked_negate() const noexcept {
    if (minor_units_ == std::numeric_limits<std::int64_t>::min()) {
        return Outcome<Money, MoneyError>::failure(MoneyError::Overflow);
    }
    return Outcome<Money, MoneyError>::success(Money(currency_, -minor_units_));
}

}  // namespace backbook::domain
