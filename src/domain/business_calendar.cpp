#include "backbook/domain/business_calendar.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <utility>

namespace backbook::domain {
namespace {

[[nodiscard]] bool is_weekend(const IsoDate& date) noexcept {
    const auto weekday =
        std::chrono::weekday{std::chrono::sys_days{date.value()}};
    return weekday == std::chrono::Saturday || weekday == std::chrono::Sunday;
}

[[nodiscard]] Outcome<IsoDate, SettlementDateError>
offset_date(const IsoDate& date, const std::int32_t offset) {
    const auto epoch_days =
        static_cast<std::int64_t>(date.to_epoch_days()) + offset;
    if (epoch_days < std::numeric_limits<std::int32_t>::min() ||
        epoch_days > std::numeric_limits<std::int32_t>::max()) {
        return Outcome<IsoDate, SettlementDateError>::failure(
            SettlementDateError::DateOutOfRange);
    }
    const auto candidate =
        IsoDate::from_epoch_days(static_cast<std::int32_t>(epoch_days));
    if (!candidate) {
        return Outcome<IsoDate, SettlementDateError>::failure(
            SettlementDateError::DateOutOfRange);
    }
    return Outcome<IsoDate, SettlementDateError>::success(candidate.value());
}

} // namespace

HolidayCalendar::HolidayCalendar(std::vector<IsoDate> holidays) noexcept
    : holidays_(std::move(holidays)) {}

Outcome<HolidayCalendar, HolidayCalendarError>
HolidayCalendar::create(std::vector<IsoDate> holidays) {
    std::sort(holidays.begin(), holidays.end());
    const auto duplicate = std::adjacent_find(holidays.begin(), holidays.end());
    if (duplicate != holidays.end()) {
        return Outcome<HolidayCalendar, HolidayCalendarError>::failure(
            HolidayCalendarError::DuplicateHoliday);
    }

    return Outcome<HolidayCalendar, HolidayCalendarError>::success(
        HolidayCalendar{std::move(holidays)});
}

bool HolidayCalendar::is_business_day(const IsoDate& date) const noexcept {
    return !is_weekend(date) &&
           !std::binary_search(holidays_.begin(), holidays_.end(), date);
}

bool is_joint_business_day(const IsoDate& date,
                           const HolidayCalendar& first_calendar,
                           const HolidayCalendar& second_calendar) noexcept {
    return first_calendar.is_business_day(date) &&
           second_calendar.is_business_day(date);
}

Outcome<IsoDate, SettlementDateError>
adjust_modified_following(const IsoDate& unadjusted_date,
                          const HolidayCalendar& first_calendar,
                          const HolidayCalendar& second_calendar) {
    if (is_joint_business_day(unadjusted_date, first_calendar,
                              second_calendar)) {
        return Outcome<IsoDate, SettlementDateError>::success(unadjusted_date);
    }

    const auto original_month = unadjusted_date.value().month();
    auto following = unadjusted_date;
    while (true) {
        auto next = offset_date(following, 1);
        if (!next || next.value().value().month() != original_month) {
            break;
        }
        following = next.value();
        if (is_joint_business_day(following, first_calendar, second_calendar)) {
            return Outcome<IsoDate, SettlementDateError>::success(following);
        }
    }

    auto preceding = unadjusted_date;
    while (true) {
        auto previous = offset_date(preceding, -1);
        if (!previous || previous.value().value().month() != original_month) {
            return Outcome<IsoDate, SettlementDateError>::failure(
                SettlementDateError::NoJointBusinessDayInMonth);
        }
        preceding = previous.value();
        if (is_joint_business_day(preceding, first_calendar, second_calendar)) {
            return Outcome<IsoDate, SettlementDateError>::success(preceding);
        }
    }
}

Outcome<IsoDate, SettlementDateError>
calculate_t_plus_two(const IsoDate& trade_date,
                     const HolidayCalendar& first_calendar,
                     const HolidayCalendar& second_calendar) {
    constexpr std::uint8_t settlement_lag = 2U;

    auto candidate = trade_date;
    std::uint8_t elapsed_business_days = 0U;
    while (elapsed_business_days < settlement_lag) {
        auto next = offset_date(candidate, 1);
        if (!next) {
            return Outcome<IsoDate, SettlementDateError>::failure(next.error());
        }
        candidate = next.value();
        if (is_joint_business_day(candidate, first_calendar, second_calendar)) {
            ++elapsed_business_days;
        }
    }

    return adjust_modified_following(candidate, first_calendar,
                                     second_calendar);
}

} // namespace backbook::domain
