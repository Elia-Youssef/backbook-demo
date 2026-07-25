#include "backbook/domain/date.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace backbook::domain {
namespace {

using namespace std::chrono;

constexpr year_month_day minimum_date{year{1}, month{1}, day{1}};
constexpr year_month_day maximum_date{year{9999}, month{12}, day{31}};
constexpr std::array<std::size_t, 8U> digit_indexes{
    0U, 1U, 2U, 3U, 5U, 6U, 8U, 9U};

[[nodiscard]] constexpr bool is_ascii_digit(const char character) noexcept {
    return character >= '0' && character <= '9';
}

[[nodiscard]] constexpr unsigned parse_digits(
    const std::string_view text,
    const std::size_t offset,
    const std::size_t count) noexcept {
    unsigned result = 0U;
    for (std::size_t index = 0U; index < count; ++index) {
        result = (result * 10U) +
                 static_cast<unsigned>(text[offset + index] - '0');
    }
    return result;
}

[[nodiscard]] constexpr std::int64_t minimum_epoch_day() noexcept {
    return static_cast<std::int64_t>(
        sys_days{minimum_date}.time_since_epoch().count());
}

[[nodiscard]] constexpr std::int64_t maximum_epoch_day() noexcept {
    return static_cast<std::int64_t>(
        sys_days{maximum_date}.time_since_epoch().count());
}

void write_decimal_digit(
    std::string& output,
    const std::size_t index,
    const unsigned value) {
    output[index] = static_cast<char>('0' + value);
}

}  // namespace

IsoDate::IsoDate(const std::chrono::year_month_day value) noexcept : value_(value) {}

Outcome<IsoDate, DateError> IsoDate::parse(const std::string_view text) {
    if (text.size() != 10U || text[4] != '-' || text[7] != '-') {
        return Outcome<IsoDate, DateError>::failure(DateError::InvalidFormat);
    }

    for (const std::size_t index : digit_indexes) {
        if (!is_ascii_digit(text[index])) {
            return Outcome<IsoDate, DateError>::failure(DateError::InvalidFormat);
        }
    }

    const unsigned year_value = parse_digits(text, 0U, 4U);
    const unsigned month_value = parse_digits(text, 5U, 2U);
    const unsigned day_value = parse_digits(text, 8U, 2U);

    if (year_value == 0U) {
        return Outcome<IsoDate, DateError>::failure(DateError::OutOfRange);
    }

    const auto parsed = year_month_day{
        year{static_cast<int>(year_value)},
        month{month_value},
        day{day_value},
    };
    if (!parsed.ok()) {
        return Outcome<IsoDate, DateError>::failure(
            DateError::InvalidCalendarDate);
    }

    return Outcome<IsoDate, DateError>::success(IsoDate{parsed});
}

Outcome<IsoDate, DateError> IsoDate::from_epoch_days(
    const std::int32_t epoch_days) {
    const auto wide_epoch_days = static_cast<std::int64_t>(epoch_days);
    if (wide_epoch_days < minimum_epoch_day() ||
        wide_epoch_days > maximum_epoch_day()) {
        return Outcome<IsoDate, DateError>::failure(DateError::OutOfRange);
    }

    const auto parsed = year_month_day{
        sys_days{days{static_cast<days::rep>(wide_epoch_days)}}};
    if (!parsed.ok() || parsed < minimum_date || parsed > maximum_date) {
        return Outcome<IsoDate, DateError>::failure(DateError::OutOfRange);
    }

    return Outcome<IsoDate, DateError>::success(IsoDate{parsed});
}

std::string IsoDate::to_string() const {
    const auto year_value = static_cast<unsigned>(static_cast<int>(value_.year()));
    const auto month_value = static_cast<unsigned>(value_.month());
    const auto day_value = static_cast<unsigned>(value_.day());

    std::string result{"0000-00-00"};
    write_decimal_digit(result, 0U, (year_value / 1000U) % 10U);
    write_decimal_digit(result, 1U, (year_value / 100U) % 10U);
    write_decimal_digit(result, 2U, (year_value / 10U) % 10U);
    write_decimal_digit(result, 3U, year_value % 10U);
    write_decimal_digit(result, 5U, (month_value / 10U) % 10U);
    write_decimal_digit(result, 6U, month_value % 10U);
    write_decimal_digit(result, 8U, (day_value / 10U) % 10U);
    write_decimal_digit(result, 9U, day_value % 10U);
    return result;
}

std::int32_t IsoDate::to_epoch_days() const noexcept {
    return static_cast<std::int32_t>(
        sys_days{value_}.time_since_epoch().count());
}

std::chrono::year_month_day IsoDate::value() const noexcept {
    return value_;
}

}  // namespace backbook::domain
