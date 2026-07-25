#include "backbook/domain/settlement.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
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

[[nodiscard]] IsoDate date(const std::string_view text) {
    auto result = IsoDate::parse(text);
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

[[nodiscard]] Trade captured_trade(
    const std::string_view trade_id,
    const std::string_view book_id,
    const std::string_view counterparty_id,
    const std::string_view netting_set_id,
    const std::string_view value_date,
    const Currency pay_currency,
    const std::int64_t pay_minor_units,
    const Currency receive_currency,
    const std::int64_t receive_minor_units) {
    auto terms = FxTerms::create(
        InstrumentKind::FxSpot,
        date("2026-07-25"),
        date(value_date),
        money(pay_currency, pay_minor_units),
        money(receive_currency, receive_minor_units));
    EXPECT_TRUE(terms);
    return Trade::capture(
        make_id<TradeId>(trade_id),
        make_id<BookId>(book_id),
        make_id<CounterpartyId>(counterparty_id),
        make_id<NettingSetId>(netting_set_id),
        std::move(terms).value());
}

[[nodiscard]] Trade confirmed(Trade trade) {
    auto result = trade.apply(TradeAction::ConfirmTrade);
    EXPECT_TRUE(result);
    return std::move(result).value();
}

[[nodiscard]] Trade settled(Trade trade) {
    auto confirmed_trade = confirmed(std::move(trade));
    auto result = confirmed_trade.apply(TradeAction::RunEod);
    EXPECT_TRUE(result);
    return std::move(result).value();
}

[[nodiscard]] const SettlementObligation* find_obligation(
    const std::vector<SettlementObligation>& obligations,
    const std::string_view counterparty,
    const std::string_view netting_set,
    const std::string_view value_date,
    const Currency currency) {
    const auto found = std::find_if(
        obligations.begin(),
        obligations.end(),
        [=](const SettlementObligation& obligation) {
            return obligation.counterparty_id().value() == counterparty &&
                   obligation.netting_set_id().value() == netting_set &&
                   obligation.value_date().to_string() == value_date &&
                   obligation.amount().currency() == currency;
        });
    return found == obligations.end() ? nullptr : &*found;
}

TEST(SettlementTest, NetsSettledCashflowsAcrossBooksFromBookPerspective) {
    std::vector<Trade> trades;
    trades.push_back(settled(captured_trade(
        "TRD-1",
        "BOOK-FX-1",
        "CPTY-A",
        "NET-A",
        "2026-07-27",
        Currency::Usd,
        100,
        Currency::Jpy,
        150)));
    trades.push_back(settled(captured_trade(
        "TRD-2",
        "BOOK-FX-2",
        "CPTY-A",
        "NET-A",
        "2026-07-27",
        Currency::Jpy,
        100,
        Currency::Usd,
        70)));

    const auto result = derive_bilateral_settlements(trades);

    ASSERT_TRUE(result);
    ASSERT_EQ(result.value().size(), 2U);
    const auto* usd = find_obligation(
        result.value(),
        "CPTY-A",
        "NET-A",
        "2026-07-27",
        Currency::Usd);
    const auto* jpy = find_obligation(
        result.value(),
        "CPTY-A",
        "NET-A",
        "2026-07-27",
        Currency::Jpy);
    ASSERT_NE(usd, nullptr);
    ASSERT_NE(jpy, nullptr);
    EXPECT_EQ(usd->direction(), SettlementDirection::Outgoing);
    EXPECT_EQ(usd->amount().minor_units(), 30);
    EXPECT_EQ(jpy->direction(), SettlementDirection::Incoming);
    EXPECT_EQ(jpy->amount().minor_units(), 50);
}

TEST(SettlementTest, NeverNetsAcrossAnyFullGroupingKeyField) {
    std::vector<Trade> trades;
    trades.push_back(settled(captured_trade(
        "TRD-1",
        "BOOK-FX-1",
        "CPTY-A",
        "NET-A",
        "2026-07-27",
        Currency::Usd,
        100,
        Currency::Jpy,
        100)));
    trades.push_back(settled(captured_trade(
        "TRD-2",
        "BOOK-FX-1",
        "CPTY-B",
        "NET-A",
        "2026-07-27",
        Currency::Jpy,
        100,
        Currency::Usd,
        100)));
    trades.push_back(settled(captured_trade(
        "TRD-3",
        "BOOK-FX-1",
        "CPTY-A",
        "NET-B",
        "2026-07-27",
        Currency::Jpy,
        100,
        Currency::Usd,
        100)));
    trades.push_back(settled(captured_trade(
        "TRD-4",
        "BOOK-FX-1",
        "CPTY-A",
        "NET-A",
        "2026-07-28",
        Currency::Jpy,
        100,
        Currency::Usd,
        100)));

    const auto result = derive_bilateral_settlements(trades);

    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().size(), 8U);
    EXPECT_EQ(
        find_obligation(
            result.value(),
            "CPTY-A",
            "NET-A",
            "2026-07-27",
            Currency::Usd)
            ->direction(),
        SettlementDirection::Outgoing);
    EXPECT_EQ(
        find_obligation(
            result.value(),
            "CPTY-B",
            "NET-A",
            "2026-07-27",
            Currency::Usd)
            ->direction(),
        SettlementDirection::Incoming);
}

TEST(SettlementTest, OmitsZeroNetObligations) {
    std::vector<Trade> trades;
    trades.push_back(settled(captured_trade(
        "TRD-1",
        "BOOK-FX-1",
        "CPTY-A",
        "NET-A",
        "2026-07-27",
        Currency::Usd,
        100,
        Currency::Jpy,
        100)));
    trades.push_back(settled(captured_trade(
        "TRD-2",
        "BOOK-FX-2",
        "CPTY-A",
        "NET-A",
        "2026-07-27",
        Currency::Jpy,
        100,
        Currency::Usd,
        100)));

    const auto result = derive_bilateral_settlements(trades);

    ASSERT_TRUE(result);
    EXPECT_TRUE(result.value().empty());
}

TEST(SettlementTest, IgnoresTradesThatAreNotSettled) {
    const Trade captured = captured_trade(
        "TRD-1",
        "BOOK-FX-1",
        "CPTY-A",
        "NET-A",
        "2026-07-27",
        Currency::Usd,
        100,
        Currency::Jpy,
        100);
    const Trade confirmed_trade = confirmed(captured_trade(
        "TRD-2",
        "BOOK-FX-1",
        "CPTY-A",
        "NET-A",
        "2026-07-27",
        Currency::Usd,
        100,
        Currency::Jpy,
        100));
    const Trade settled_trade = settled(captured_trade(
        "TRD-3",
        "BOOK-FX-1",
        "CPTY-A",
        "NET-A",
        "2026-07-27",
        Currency::Usd,
        25,
        Currency::Jpy,
        50));

    const auto result = derive_bilateral_settlements(
        {captured, confirmed_trade, settled_trade});

    ASSERT_TRUE(result);
    ASSERT_EQ(result.value().size(), 2U);
    EXPECT_EQ(
        find_obligation(
            result.value(),
            "CPTY-A",
            "NET-A",
            "2026-07-27",
            Currency::Usd)
            ->amount()
            .minor_units(),
        25);
}

TEST(SettlementTest, OutputIsStableSortedAndIndependentOfInputOrder) {
    std::vector<Trade> trades;
    trades.push_back(settled(captured_trade(
        "TRD-3",
        "BOOK-FX-1",
        "CPTY-B",
        "NET-A",
        "2026-07-28",
        Currency::Kwd,
        100,
        Currency::Usd,
        100)));
    trades.push_back(settled(captured_trade(
        "TRD-1",
        "BOOK-FX-1",
        "CPTY-B",
        "NET-A",
        "2026-07-27",
        Currency::Usd,
        100,
        Currency::Jpy,
        100)));
    trades.push_back(settled(captured_trade(
        "TRD-2",
        "BOOK-FX-1",
        "CPTY-A",
        "NET-B",
        "2026-07-27",
        Currency::Usd,
        100,
        Currency::Jpy,
        100)));

    const auto forward = derive_bilateral_settlements(trades);
    std::reverse(trades.begin(), trades.end());
    const auto reverse = derive_bilateral_settlements(trades);

    ASSERT_TRUE(forward);
    ASSERT_TRUE(reverse);
    EXPECT_EQ(forward.value(), reverse.value());
    ASSERT_EQ(forward.value().size(), 6U);
    EXPECT_EQ(forward.value()[0U].value_date().to_string(), "2026-07-27");
    EXPECT_EQ(forward.value()[0U].amount().currency(), Currency::Usd);
    EXPECT_EQ(forward.value()[0U].counterparty_id().value(), "CPTY-A");
    EXPECT_EQ(forward.value()[1U].counterparty_id().value(), "CPTY-B");
    EXPECT_EQ(forward.value()[2U].amount().currency(), Currency::Jpy);
    EXPECT_EQ(forward.value()[4U].value_date().to_string(), "2026-07-28");
    EXPECT_EQ(forward.value()[4U].amount().currency(), Currency::Usd);
    EXPECT_EQ(forward.value()[5U].amount().currency(), Currency::Kwd);
}

TEST(SettlementTest, ReportsPositiveAggregationOverflow) {
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    std::vector<Trade> trades;
    trades.push_back(settled(captured_trade(
        "TRD-1",
        "BOOK-FX-1",
        "CPTY-A",
        "NET-A",
        "2026-07-27",
        Currency::Jpy,
        1,
        Currency::Usd,
        maximum)));
    trades.push_back(settled(captured_trade(
        "TRD-2",
        "BOOK-FX-2",
        "CPTY-A",
        "NET-A",
        "2026-07-27",
        Currency::Jpy,
        1,
        Currency::Usd,
        1)));

    const auto result = derive_bilateral_settlements(trades);

    ASSERT_TRUE(result.has_error());
    EXPECT_EQ(result.error(), SettlementError::ArithmeticOverflow);
}

TEST(SettlementTest, ReportsUnrepresentableOutgoingMagnitude) {
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    std::vector<Trade> trades;
    trades.push_back(settled(captured_trade(
        "TRD-1",
        "BOOK-FX-1",
        "CPTY-A",
        "NET-A",
        "2026-07-27",
        Currency::Usd,
        maximum,
        Currency::Jpy,
        1)));
    trades.push_back(settled(captured_trade(
        "TRD-2",
        "BOOK-FX-2",
        "CPTY-A",
        "NET-A",
        "2026-07-27",
        Currency::Usd,
        1,
        Currency::Jpy,
        1)));

    const auto result = derive_bilateral_settlements(trades);

    ASSERT_TRUE(result.has_error());
    EXPECT_EQ(result.error(), SettlementError::ArithmeticOverflow);
}

}  // namespace
}  // namespace backbook::domain
