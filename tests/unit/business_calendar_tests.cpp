#include "backbook/domain/business_calendar.hpp"
#include "backbook/domain/fx_terms.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace backbook::domain {
namespace {

static_assert(!std::is_default_constructible_v<HolidayCalendar>);
static_assert(!std::is_aggregate_v<HolidayCalendar>);
static_assert(!std::is_constructible_v<HolidayCalendar, std::vector<IsoDate>>);

[[nodiscard]] IsoDate date(const std::string_view value) {
    return IsoDate::parse(value).value();
}

[[nodiscard]] HolidayCalendar
calendar(const std::initializer_list<std::string_view> holidays) {
    std::vector<IsoDate> dates;
    dates.reserve(holidays.size());
    for (const auto holiday : holidays) {
        dates.push_back(date(holiday));
    }

    auto created = HolidayCalendar::create(std::move(dates));
    return std::move(created).value();
}

TEST(HolidayCalendarTest, RejectsDuplicateHolidayDefinitions) {
    auto result =
        HolidayCalendar::create({date("2026-07-01"), date("2026-07-01")});

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), HolidayCalendarError::DuplicateHoliday);
}

TEST(HolidayCalendarTest, SortsInputAndClosesWeekendsAndExplicitHolidays) {
    const auto result = calendar({"2026-07-03", "2026-07-01"});

    EXPECT_TRUE(result.is_business_day(date("2026-06-30")));
    EXPECT_FALSE(result.is_business_day(date("2026-07-01")));
    EXPECT_FALSE(result.is_business_day(date("2026-07-03")));
    EXPECT_FALSE(result.is_business_day(date("2026-07-04")));
    EXPECT_FALSE(result.is_business_day(date("2026-07-05")));
    EXPECT_TRUE(result.is_business_day(date("2026-07-06")));
}

TEST(HolidayCalendarTest, EqualHolidaySetsHaveCanonicalEquality) {
    const auto first = calendar({"2026-07-03", "2026-07-01"});
    const auto second = calendar({"2026-07-01", "2026-07-03"});

    EXPECT_EQ(first, second);
}

TEST(SettlementDateTest, RequiresBothCalendarsToBeOpen) {
    const auto first = calendar({"2026-06-30"});
    const auto second = calendar({"2026-07-01"});

    EXPECT_FALSE(is_joint_business_day(date("2026-06-30"), first, second));
    EXPECT_FALSE(is_joint_business_day(date("2026-07-01"), first, second));
    EXPECT_TRUE(is_joint_business_day(date("2026-07-02"), first, second));
}

TEST(SettlementDateTest, CalculatesTPlusTwoAcrossBothHolidayCalendars) {
    const auto first = calendar({"2026-06-30"});
    const auto second = calendar({"2026-07-01"});

    const auto result = calculate_t_plus_two(date("2026-06-29"), first, second);

    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), date("2026-07-03"));
}

TEST(SettlementDateTest, CalculatedDatePopulatesExplicitSpotTerms) {
    const auto first = calendar({"2026-06-30"});
    const auto second = calendar({"2026-07-01"});
    const auto trade_date = date("2026-06-29");
    const auto value_date = calculate_t_plus_two(trade_date, first, second);
    ASSERT_TRUE(value_date);

    const auto terms = FxTerms::create(
        InstrumentKind::FxSpot, trade_date, value_date.value(),
        Money::from_minor_units(Currency::Usd, 10'000).value(),
        Money::from_minor_units(Currency::Jpy, 1'500'000).value());

    ASSERT_TRUE(terms);
    EXPECT_EQ(terms.value().value_date(), date("2026-07-03"));
}

TEST(SettlementDateTest, CountsFromTheDayAfterTheTradeDate) {
    const auto first = calendar({});
    const auto second = calendar({});

    const auto result = calculate_t_plus_two(date("2026-07-24"), first, second);

    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), date("2026-07-28"));
}

TEST(SettlementDateTest, IgnoresWhetherTheTradeDateItselfIsOpen) {
    const auto first = calendar({"2026-07-24"});
    const auto second = calendar({});

    const auto result = calculate_t_plus_two(date("2026-07-24"), first, second);

    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), date("2026-07-28"));
}

TEST(SettlementDateTest, ModifiedFollowingMovesForwardWithinTheSameMonth) {
    const auto first = calendar({});
    const auto second = calendar({});

    const auto result =
        adjust_modified_following(date("2026-08-29"), first, second);

    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), date("2026-08-31"));
}

TEST(SettlementDateTest, ModifiedFollowingRollsBackwardAtAMonthBoundary) {
    const auto first = calendar({"2026-01-30"});
    const auto second = calendar({"2026-01-29"});

    const auto result =
        adjust_modified_following(date("2026-01-31"), first, second);

    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), date("2026-01-28"));
}

TEST(SettlementDateTest, ModifiedFollowingLeavesJointBusinessDayUnchanged) {
    const auto first = calendar({"2026-07-01"});
    const auto second = calendar({"2026-07-02"});
    const auto unadjusted = date("2026-07-03");

    const auto result = adjust_modified_following(unadjusted, first, second);

    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), unadjusted);
}

TEST(SettlementDateTest, ReportsMonthWithNoJointBusinessDay) {
    std::vector<IsoDate> closed_dates;
    for (std::int32_t day = date("2026-02-01").to_epoch_days();
         day <= date("2026-02-28").to_epoch_days(); ++day) {
        closed_dates.push_back(IsoDate::from_epoch_days(day).value());
    }
    auto closed_calendar =
        HolidayCalendar::create(std::move(closed_dates)).value();
    const auto open_calendar = calendar({});

    const auto result = adjust_modified_following(
        date("2026-02-28"), closed_calendar, open_calendar);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), SettlementDateError::NoJointBusinessDayInMonth);
}

TEST(SettlementDateTest, ReportsTPlusTwoBeyondSupportedDateRange) {
    const auto first = calendar({});
    const auto second = calendar({});

    const auto result = calculate_t_plus_two(date("9999-12-31"), first, second);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), SettlementDateError::DateOutOfRange);
}

TEST(SettlementDateTest, ModifiedFollowingFallsBackAtMaximumDate) {
    const auto first = calendar({"9999-12-31"});
    const auto second = calendar({});

    const auto result =
        adjust_modified_following(date("9999-12-31"), first, second);

    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), date("9999-12-30"));
}

} // namespace
} // namespace backbook::domain
