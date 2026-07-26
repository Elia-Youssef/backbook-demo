#pragma once

#include "backbook/domain/date.hpp"
#include "backbook/domain/outcome.hpp"

#include <cstdint>
#include <vector>

namespace backbook::domain {

enum class HolidayCalendarError : std::uint8_t {
    DuplicateHoliday,
};

class HolidayCalendar final {
public:
    [[nodiscard]] static Outcome<HolidayCalendar, HolidayCalendarError>
    create(std::vector<IsoDate> holidays);

    [[nodiscard]] bool is_business_day(const IsoDate& date) const noexcept;

    [[nodiscard]] friend bool operator==(const HolidayCalendar&,
                                         const HolidayCalendar&) = default;

private:
    explicit HolidayCalendar(std::vector<IsoDate> holidays) noexcept;

    std::vector<IsoDate> holidays_;
};

enum class SettlementDateError : std::uint8_t {
    DateOutOfRange,
    NoJointBusinessDayInMonth,
};

[[nodiscard]] bool
is_joint_business_day(const IsoDate& date,
                      const HolidayCalendar& first_calendar,
                      const HolidayCalendar& second_calendar) noexcept;

[[nodiscard]] Outcome<IsoDate, SettlementDateError>
adjust_modified_following(const IsoDate& unadjusted_date,
                          const HolidayCalendar& first_calendar,
                          const HolidayCalendar& second_calendar);

[[nodiscard]] Outcome<IsoDate, SettlementDateError>
calculate_t_plus_two(const IsoDate& trade_date,
                     const HolidayCalendar& first_calendar,
                     const HolidayCalendar& second_calendar);

} // namespace backbook::domain
