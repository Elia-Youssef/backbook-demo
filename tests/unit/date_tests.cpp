#include "backbook/domain/date.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string_view>

namespace backbook::domain {
namespace {

TEST(IsoDateTest, ParsesAndFormatsLeapDayInYear2000) {
    const auto result = IsoDate::parse("2000-02-29");

    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().to_string(), "2000-02-29");
}

TEST(IsoDateTest, RejectsNonLeapCenturyAndInvalidMonthLengths) {
    constexpr std::array invalid_dates{
        std::string_view{"1900-02-29"},
        std::string_view{"2026-02-29"},
        std::string_view{"2026-04-31"},
        std::string_view{"2026-06-31"},
        std::string_view{"2026-09-31"},
        std::string_view{"2026-11-31"},
        std::string_view{"2026-00-01"},
        std::string_view{"2026-13-01"},
        std::string_view{"2026-01-00"},
        std::string_view{"2026-01-32"},
    };

    for (const auto text : invalid_dates) {
        const auto result = IsoDate::parse(text);
        ASSERT_FALSE(result) << text;
        EXPECT_EQ(result.error(), DateError::InvalidCalendarDate) << text;
    }
}

TEST(IsoDateTest, AcceptsExactSupportedRangeBoundaries) {
    const auto minimum = IsoDate::parse("0001-01-01");
    const auto maximum = IsoDate::parse("9999-12-31");

    ASSERT_TRUE(minimum);
    ASSERT_TRUE(maximum);
    EXPECT_EQ(minimum.value().to_string(), "0001-01-01");
    EXPECT_EQ(maximum.value().to_string(), "9999-12-31");
    EXPECT_LT(minimum.value(), maximum.value());
}

TEST(IsoDateTest, RejectsYearZeroAsOutOfRange) {
    const auto result = IsoDate::parse("0000-12-31");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), DateError::OutOfRange);
}

TEST(IsoDateTest, RejectsNonCanonicalFormats) {
    constexpr std::array invalid_formats{
        std::string_view{""},
        std::string_view{"2026-1-01"},
        std::string_view{"2026-01-1"},
        std::string_view{"026-01-01"},
        std::string_view{"02026-01-01"},
        std::string_view{"2026/01/01"},
        std::string_view{"2026.01.01"},
        std::string_view{"+026-01-01"},
        std::string_view{"-001-01-01"},
        std::string_view{" 2026-01-01"},
        std::string_view{"2026-01-01 "},
        std::string_view{"2026-0a-01"},
        std::string_view{"2026-01-\xFF" "1"},
    };

    for (const auto text : invalid_formats) {
        const auto result = IsoDate::parse(text);
        ASSERT_FALSE(result) << text;
        EXPECT_EQ(result.error(), DateError::InvalidFormat) << text;
    }
}

TEST(IsoDateTest, EpochDayZeroIsUnixEpoch) {
    const auto epoch = IsoDate::from_epoch_days(0);

    ASSERT_TRUE(epoch);
    EXPECT_EQ(epoch.value().to_string(), "1970-01-01");
    EXPECT_EQ(epoch.value().to_epoch_days(), 0);
}

TEST(IsoDateTest, RoundTripsBoundaryAndDatesOnBothSidesOfEpoch) {
    constexpr std::array dates{
        std::string_view{"0001-01-01"},
        std::string_view{"1900-03-01"},
        std::string_view{"1969-12-31"},
        std::string_view{"1970-01-01"},
        std::string_view{"2000-02-29"},
        std::string_view{"2026-07-25"},
        std::string_view{"9999-12-31"},
    };

    for (const auto text : dates) {
        const auto parsed = IsoDate::parse(text);
        ASSERT_TRUE(parsed) << text;

        const auto reconstructed =
            IsoDate::from_epoch_days(parsed.value().to_epoch_days());
        ASSERT_TRUE(reconstructed) << text;
        EXPECT_EQ(reconstructed.value(), parsed.value()) << text;
        EXPECT_EQ(reconstructed.value().to_string(), text) << text;
    }
}

TEST(IsoDateTest, RejectsEpochDaysImmediatelyOutsideSupportedRange) {
    const auto minimum = IsoDate::parse("0001-01-01");
    const auto maximum = IsoDate::parse("9999-12-31");
    ASSERT_TRUE(minimum);
    ASSERT_TRUE(maximum);

    const auto before_minimum =
        IsoDate::from_epoch_days(minimum.value().to_epoch_days() - 1);
    const auto after_maximum =
        IsoDate::from_epoch_days(maximum.value().to_epoch_days() + 1);

    ASSERT_FALSE(before_minimum);
    ASSERT_FALSE(after_maximum);
    EXPECT_EQ(before_minimum.error(), DateError::OutOfRange);
    EXPECT_EQ(after_maximum.error(), DateError::OutOfRange);
}

TEST(IsoDateTest, OrdersDatesChronologically) {
    const auto earlier = IsoDate::parse("2026-07-25");
    const auto later = IsoDate::parse("2026-07-26");
    ASSERT_TRUE(earlier);
    ASSERT_TRUE(later);

    EXPECT_LT(earlier.value(), later.value());
    EXPECT_NE(earlier.value(), later.value());
}

}  // namespace
}  // namespace backbook::domain
