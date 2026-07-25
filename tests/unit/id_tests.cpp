#include "backbook/domain/id.hpp"

#include <gtest/gtest.h>

#include <array>
#include <concepts>
#include <string>
#include <string_view>
#include <type_traits>

namespace {

using backbook::domain::BookId;
using backbook::domain::CommandId;
using backbook::domain::CounterpartyId;
using backbook::domain::IdError;
using backbook::domain::NettingSetId;
using backbook::domain::PostingId;
using backbook::domain::TradeId;

template <typename Left, typename Right>
concept EqualityComparableAcrossTypes =
    requires(const Left& left, const Right& right) {
        { left == right } -> std::convertible_to<bool>;
    };

static_assert(!std::is_same_v<TradeId, BookId>);
static_assert(!std::is_same_v<BookId, CounterpartyId>);
static_assert(!std::is_same_v<NettingSetId, CommandId>);
static_assert(!std::is_same_v<CommandId, PostingId>);
static_assert(std::is_same_v<std::underlying_type_t<IdError>, std::uint8_t>);
static_assert(!EqualityComparableAcrossTypes<TradeId, BookId>);
static_assert(!std::is_constructible_v<TradeId, std::string>);
static_assert(!std::is_constructible_v<TradeId, std::string_view>);

TEST(IdTest, AcceptsOneAndSixtyFourByteBoundaries) {
    const auto one_byte = TradeId::parse("A");
    const auto sixty_four_bytes =
        TradeId::parse(std::string{"A"} + std::string(63U, '-'));

    ASSERT_TRUE(one_byte.has_value());
    EXPECT_EQ(one_byte.value().value(), "A");
    ASSERT_TRUE(sixty_four_bytes.has_value());
    EXPECT_EQ(sixty_four_bytes.value().value().size(), 64U);
}

TEST(IdTest, AcceptsRepresentativeSyntheticIdentifiers) {
    const auto trade = TradeId::parse("TRD-1001");
    const auto book = BookId::parse("BOOK-FX-1");
    const auto counterparty = CounterpartyId::parse("CPTY-A");
    const auto netting_set = NettingSetId::parse("NET_A:2026.07");
    const auto command = CommandId::parse("cmd:confirm-1001");
    const auto posting = PostingId::parse("PST_1001.1");

    EXPECT_TRUE(trade.has_value());
    EXPECT_TRUE(book.has_value());
    EXPECT_TRUE(counterparty.has_value());
    EXPECT_TRUE(netting_set.has_value());
    EXPECT_TRUE(command.has_value());
    EXPECT_TRUE(posting.has_value());
}

TEST(IdTest, RejectsEmptyAndOverlongIdentifiers) {
    const auto empty = TradeId::parse("");
    const auto overlong = TradeId::parse(std::string(65U, 'A'));

    ASSERT_TRUE(empty.has_error());
    EXPECT_EQ(empty.error(), IdError::Empty);
    ASSERT_TRUE(overlong.has_error());
    EXPECT_EQ(overlong.error(), IdError::TooLong);
}

TEST(IdTest, RejectsLeadingPunctuation) {
    for (const std::string_view candidate :
         {".TRD", "_TRD", ":TRD", "-TRD"}) {
        const auto parsed = TradeId::parse(candidate);

        ASSERT_TRUE(parsed.has_error()) << candidate;
        EXPECT_EQ(parsed.error(), IdError::InvalidFirstCharacter) << candidate;
    }
}

TEST(IdTest, RejectsWhitespaceSlashesControlsAndDisallowedPunctuation) {
    const std::string with_control{"TRD\0X", 5U};
    const std::string non_ascii{
        'T',
        'R',
        'D',
        static_cast<char>(0xC3),
        static_cast<char>(0xA9),
    };

    const std::array<std::string, 8U> invalid_ids{
        "TRD 1",
        "TRD\t1",
        "TRD/1",
        "TRD\\1",
        "TRD@1",
        "TRD#1",
        with_control,
        non_ascii,
    };

    for (const std::string_view candidate : invalid_ids) {
        const auto parsed = TradeId::parse(candidate);

        ASSERT_TRUE(parsed.has_error());
        EXPECT_EQ(parsed.error(), IdError::InvalidCharacter);
    }
}

TEST(IdTest, OwnsItsStorage) {
    std::string source = "TRD-1001";
    auto parsed = TradeId::parse(source);
    ASSERT_TRUE(parsed.has_value());

    source.assign("TRD-CHANGED");

    EXPECT_EQ(parsed.value().value(), "TRD-1001");
}

TEST(IdTest, OwnsStorageCreatedFromATemporary) {
    const auto parsed = TradeId::parse(std::string{"TRD-TEMP"});

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed.value().value(), "TRD-TEMP");
}

TEST(IdTest, SupportsSameTagEqualityAndDeterministicOrdering) {
    const auto first = TradeId::parse("TRD-1001");
    const auto first_copy = TradeId::parse("TRD-1001");
    const auto second = TradeId::parse("TRD-1002");
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(first_copy.has_value());
    ASSERT_TRUE(second.has_value());

    EXPECT_EQ(first.value(), first_copy.value());
    EXPECT_LT(first.value(), second.value());
    EXPECT_GT(second.value(), first.value());
}

}  // namespace
