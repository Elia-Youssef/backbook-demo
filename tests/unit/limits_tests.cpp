#include "backbook/domain/limits.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace backbook::domain {
namespace {

template <typename IdType>
[[nodiscard]] IdType make_id(const std::string_view text) {
    auto result = IdType::parse(text);
    EXPECT_TRUE(result);
    return std::move(result).value();
}

[[nodiscard]] Money money(
    const Currency currency,
    const std::int64_t minor_units) {
    auto result = Money::from_minor_units(currency, minor_units);
    EXPECT_TRUE(result);
    return std::move(result).value();
}

[[nodiscard]] LimitPath path(
    const std::string_view counterparty = "CPTY-A",
    const std::string_view netting_set = "NET-A",
    const std::string_view book = "BOOK-FX-1") {
    return LimitPath(
        make_id<CounterpartyId>(counterparty),
        make_id<NettingSetId>(netting_set),
        make_id<BookId>(book));
}

void add_path_definitions(
    std::vector<LimitDefinition>& definitions,
    const LimitPath& limit_path,
    const Currency currency,
    const std::int64_t group_capacity,
    const std::int64_t counterparty_capacity,
    const std::int64_t netting_set_capacity,
    const std::int64_t book_capacity) {
    const auto nodes = limit_path.nodes();
    definitions.push_back(
        LimitDefinition{nodes[0U], money(currency, group_capacity)});
    definitions.push_back(
        LimitDefinition{nodes[1U], money(currency, counterparty_capacity)});
    definitions.push_back(
        LimitDefinition{nodes[2U], money(currency, netting_set_capacity)});
    definitions.push_back(
        LimitDefinition{nodes[3U], money(currency, book_capacity)});
}

[[nodiscard]] LimitHierarchy hierarchy(
    const std::int64_t group_capacity = 20'000'000,
    const std::int64_t counterparty_capacity = 20'000'000,
    const std::int64_t netting_set_capacity = 20'000'000,
    const std::int64_t book_capacity = 15'000'000) {
    std::vector<LimitDefinition> definitions;
    add_path_definitions(
        definitions,
        path(),
        Currency::Usd,
        group_capacity,
        counterparty_capacity,
        netting_set_capacity,
        book_capacity);
    add_path_definitions(
        definitions,
        path(),
        Currency::Jpy,
        2'000'000'000,
        2'000'000'000,
        2'000'000'000,
        2'000'000'000);

    auto result = LimitHierarchy::create(std::move(definitions));
    EXPECT_TRUE(result);
    return std::move(result).value();
}

[[nodiscard]] std::int64_t minor_units(
    const Outcome<Money, LimitError>& result) {
    EXPECT_TRUE(result);
    return result.value().minor_units();
}

TEST(LimitNodeTest, ExposesCompleteRootToLeafPathComponents) {
    const auto nodes = path().nodes();

    EXPECT_EQ(nodes[0U].path_components(), std::vector<std::string>{"GROUP"});
    EXPECT_EQ(
        nodes[1U].path_components(),
        (std::vector<std::string>{"GROUP", "CPTY-A"}));
    EXPECT_EQ(
        nodes[2U].path_components(),
        (std::vector<std::string>{"GROUP", "CPTY-A", "NET-A"}));
    EXPECT_EQ(
        nodes[3U].path_components(),
        (std::vector<std::string>{
            "GROUP",
            "CPTY-A",
            "NET-A",
            "BOOK-FX-1"}));
}

TEST(LimitHierarchyTest, RejectsNegativeCapacityAndDuplicateDefinition) {
    const auto group = LimitNode::group();
    auto negative = LimitHierarchy::create(
        {LimitDefinition{group, money(Currency::Usd, -1)}});

    ASSERT_TRUE(negative.has_error());
    EXPECT_EQ(negative.error().code, LimitErrorCode::InvalidCapacity);

    auto duplicate = LimitHierarchy::create(
        {LimitDefinition{group, money(Currency::Usd, 100)},
         LimitDefinition{group, money(Currency::Usd, 200)}});

    ASSERT_TRUE(duplicate.has_error());
    EXPECT_EQ(duplicate.error().code, LimitErrorCode::DuplicateDefinition);
}

TEST(LimitHierarchyTest, ReservesOutgoingAmountAtEveryNode) {
    const auto original = hierarchy();
    const auto reserved =
        original.reserve(path(), money(Currency::Usd, 10'125'000));

    ASSERT_TRUE(reserved);
    for (const LimitNode& node : path().nodes()) {
        EXPECT_EQ(
            minor_units(reserved.value().reserved(node, Currency::Usd)),
            10'125'000);
    }
    EXPECT_EQ(
        minor_units(
            reserved.value().headroom(path().nodes()[3U], Currency::Usd)),
        4'875'000);
    EXPECT_EQ(
        minor_units(original.reserved(path().nodes()[3U], Currency::Usd)),
        0);
}

TEST(LimitHierarchyTest, ReservationDoesNotChangeOtherCurrencies) {
    const auto reserved =
        hierarchy().reserve(path(), money(Currency::Usd, 10'125'000));

    ASSERT_TRUE(reserved);
    for (const LimitNode& node : path().nodes()) {
        EXPECT_EQ(
            minor_units(reserved.value().reserved(node, Currency::Jpy)),
            0);
        EXPECT_EQ(
            minor_units(reserved.value().headroom(node, Currency::Jpy)),
            2'000'000'000);
    }
}

TEST(LimitHierarchyTest, ReportsFirstBreachInRootToLeafOrder) {
    const auto result =
        hierarchy(50, 40, 30, 20).reserve(path(), money(Currency::Usd, 60));

    ASSERT_TRUE(result.has_error());
    EXPECT_EQ(result.error().code, LimitErrorCode::Breach);
    EXPECT_EQ(result.error().node.level(), LimitLevel::Group);
    EXPECT_EQ(
        result.error().node.path_components(),
        std::vector<std::string>{"GROUP"});
    EXPECT_EQ(result.error().currency, Currency::Usd);
    EXPECT_EQ(result.error().required_minor_units, 60);
    EXPECT_EQ(result.error().remaining_minor_units, 50);
}

TEST(LimitHierarchyTest, ReportsBookBreachWithExactRemainingHeadroom) {
    const auto first =
        hierarchy().reserve(path(), money(Currency::Usd, 10'125'000));
    ASSERT_TRUE(first);

    const auto breach =
        first.value().reserve(path(), money(Currency::Usd, 6'000'000));

    ASSERT_TRUE(breach.has_error());
    EXPECT_EQ(breach.error().code, LimitErrorCode::Breach);
    EXPECT_EQ(breach.error().node.level(), LimitLevel::Book);
    EXPECT_EQ(
        breach.error().node.path_components(),
        (std::vector<std::string>{
            "GROUP",
            "CPTY-A",
            "NET-A",
            "BOOK-FX-1"}));
    EXPECT_EQ(breach.error().required_minor_units, 6'000'000);
    EXPECT_EQ(breach.error().remaining_minor_units, 4'875'000);

    EXPECT_EQ(
        minor_units(
            first.value().headroom(path().nodes()[3U], Currency::Usd)),
        4'875'000);
}

TEST(LimitHierarchyTest, UnknownHierarchyNodeIsTypedAndNeverCreated) {
    const auto unknown_path = path("CPTY-B", "NET-B", "BOOK-FX-2");
    const auto result =
        hierarchy().reserve(unknown_path, money(Currency::Usd, 1));

    ASSERT_TRUE(result.has_error());
    EXPECT_EQ(result.error().code, LimitErrorCode::UnknownNode);
    EXPECT_EQ(result.error().node.level(), LimitLevel::Counterparty);

    const auto lookup =
        hierarchy().headroom(unknown_path.nodes()[1U], Currency::Usd);
    ASSERT_TRUE(lookup.has_error());
    EXPECT_EQ(lookup.error().code, LimitErrorCode::UnknownNode);
}

TEST(LimitHierarchyTest, ReleasesExactReservationAtEveryNode) {
    const auto original = hierarchy();
    const auto reserved =
        original.reserve(path(), money(Currency::Usd, 10'125'000));
    ASSERT_TRUE(reserved);

    const auto released =
        reserved.value().release(path(), money(Currency::Usd, 10'125'000));

    ASSERT_TRUE(released);
    EXPECT_EQ(released.value(), original);
}

TEST(LimitHierarchyTest, FailedReleaseLeavesPriorHierarchyUnchanged) {
    const auto reserved =
        hierarchy().reserve(path(), money(Currency::Usd, 100));
    ASSERT_TRUE(reserved);
    const auto snapshot = reserved.value();

    const auto result =
        reserved.value().release(path(), money(Currency::Usd, 101));

    ASSERT_TRUE(result.has_error());
    EXPECT_EQ(
        result.error().code,
        LimitErrorCode::ReleaseExceedsReservation);
    EXPECT_EQ(reserved.value(), snapshot);
    EXPECT_EQ(
        minor_units(
            reserved.value().reserved(path().nodes()[0U], Currency::Usd)),
        100);
}

TEST(LimitHierarchyTest, RejectsZeroAndNegativeReservationAmounts) {
    const auto zero = hierarchy().reserve(path(), money(Currency::Usd, 0));
    const auto negative =
        hierarchy().reserve(path(), money(Currency::Usd, -1));

    ASSERT_TRUE(zero.has_error());
    EXPECT_EQ(zero.error().code, LimitErrorCode::NonPositiveReservation);
    ASSERT_TRUE(negative.has_error());
    EXPECT_EQ(
        negative.error().code,
        LimitErrorCode::NonPositiveReservation);
}

TEST(LimitHierarchyTest, RejectsUnsupportedCurrencyLookup) {
    const auto result = hierarchy().headroom(
        LimitNode::group(),
        static_cast<Currency>(0xffU));

    ASSERT_TRUE(result.has_error());
    EXPECT_EQ(result.error().code, LimitErrorCode::UnsupportedCurrency);
}

}  // namespace
}  // namespace backbook::domain
