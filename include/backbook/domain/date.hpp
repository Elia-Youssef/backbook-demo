#pragma once

#include "backbook/domain/outcome.hpp"

#include <chrono>
#include <compare>
#include <cstdint>
#include <string>
#include <string_view>

namespace backbook::domain {

enum class DateError : std::uint8_t {
    InvalidFormat,
    OutOfRange,
    InvalidCalendarDate,
};

class IsoDate final {
public:
    [[nodiscard]] static Outcome<IsoDate, DateError> parse(std::string_view text);
    [[nodiscard]] static Outcome<IsoDate, DateError> from_epoch_days(
        std::int32_t epoch_days);

    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] std::int32_t to_epoch_days() const noexcept;
    [[nodiscard]] std::chrono::year_month_day value() const noexcept;

    [[nodiscard]] friend bool operator==(const IsoDate&, const IsoDate&) = default;
    [[nodiscard]] friend auto operator<=>(const IsoDate&, const IsoDate&) = default;

private:
    explicit IsoDate(std::chrono::year_month_day value) noexcept;

    std::chrono::year_month_day value_;
};

}  // namespace backbook::domain
