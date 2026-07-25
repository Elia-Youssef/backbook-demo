#include "backbook/domain/outcome.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace {

using IntOutcome = backbook::domain::Outcome<int, std::string>;

TEST(OutcomeTest, CreatesAndInspectsSuccess) {
    auto outcome = IntOutcome::success(41);

    EXPECT_TRUE(outcome.has_value());
    EXPECT_FALSE(outcome.has_error());
    EXPECT_TRUE(static_cast<bool>(outcome));

    outcome.value() += 1;
    EXPECT_EQ(outcome.value(), 42);
}

TEST(OutcomeTest, CreatesAndInspectsFailure) {
    auto outcome = IntOutcome::failure("invalid");

    EXPECT_FALSE(outcome.has_value());
    EXPECT_TRUE(outcome.has_error());
    EXPECT_FALSE(static_cast<bool>(outcome));

    outcome.error().append(" input");
    EXPECT_EQ(outcome.error(), "invalid input");
}

TEST(OutcomeTest, ProvidesConstAccess) {
    const auto success = IntOutcome::success(42);
    const auto failure = IntOutcome::failure("invalid");

    EXPECT_EQ(success.value(), 42);
    EXPECT_EQ(failure.error(), "invalid");
}

TEST(OutcomeTest, MovesValuesAndErrorsOut) {
    auto success =
        backbook::domain::Outcome<std::unique_ptr<int>, std::string>::success(
            std::make_unique<int>(42));
    auto failure =
        backbook::domain::Outcome<int, std::unique_ptr<std::string>>::failure(
            std::make_unique<std::string>("invalid"));

    auto value = std::move(success).value();
    auto error = std::move(failure).error();

    ASSERT_NE(value, nullptr);
    EXPECT_EQ(*value, 42);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(*error, "invalid");
}

TEST(OutcomeTest, SupportsIdenticalValueAndErrorTypes) {
    using SameTypeOutcome = backbook::domain::Outcome<int, int>;

    const auto success = SameTypeOutcome::success(42);
    const auto failure = SameTypeOutcome::failure(7);

    EXPECT_EQ(success.value(), 42);
    EXPECT_EQ(failure.error(), 7);
}

static_assert(
    !std::is_copy_constructible_v<
        backbook::domain::Outcome<std::unique_ptr<int>, std::string>>);

}  // namespace
