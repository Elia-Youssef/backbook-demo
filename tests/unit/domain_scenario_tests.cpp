#include "backbook/domain/ledger_totals.hpp"
#include "backbook/domain/posting_policy.hpp"
#include "backbook/domain/trade.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace backbook::domain {
namespace {

template <typename IdType>
[[nodiscard]] IdType make_id(const std::string_view text) {
    auto parsed = IdType::parse(text);
    EXPECT_TRUE(parsed);
    return std::move(parsed).value();
}

[[nodiscard]] IsoDate make_date(const std::string_view text) {
    auto parsed = IsoDate::parse(text);
    EXPECT_TRUE(parsed);
    return std::move(parsed).value();
}

[[nodiscard]] Money make_money(
    const Currency currency,
    const std::int64_t minor_units) {
    auto result = Money::from_minor_units(currency, minor_units);
    EXPECT_TRUE(result);
    return std::move(result).value();
}

[[nodiscard]] FxTerms make_terms(
    const std::int64_t pay_minor_units,
    const std::int64_t receive_minor_units) {
    auto result = FxTerms::create(
        InstrumentKind::FxSpot,
        make_date("2026-07-25"),
        make_date("2026-07-27"),
        make_money(Currency::Usd, pay_minor_units),
        make_money(Currency::Jpy, receive_minor_units));
    EXPECT_TRUE(result);
    return std::move(result).value();
}

[[nodiscard]] ConfirmationPostingIds confirmation_ids(
    const std::string_view pay_control,
    const std::string_view pay_payable,
    const std::string_view receive_receivable,
    const std::string_view receive_control) {
    return ConfirmationPostingIds{
        make_id<PostingId>(pay_control),
        make_id<PostingId>(pay_payable),
        make_id<PostingId>(receive_receivable),
        make_id<PostingId>(receive_control)};
}

[[nodiscard]] ReversalPostingIds reversal_ids() {
    return ReversalPostingIds{
        make_id<PostingId>("PST-V1-REV-PAY-CONTROL"),
        make_id<PostingId>("PST-V1-REV-PAY-PAYABLE"),
        make_id<PostingId>("PST-V1-REV-RECEIVE-RECEIVABLE"),
        make_id<PostingId>("PST-V1-REV-RECEIVE-CONTROL")};
}

[[nodiscard]] PostingSide opposite(const PostingSide side) {
    switch (side) {
    case PostingSide::Debit:
        return PostingSide::Credit;
    case PostingSide::Credit:
        return PostingSide::Debit;
    }
    return side;
}

TEST(
    DomainScenarioTest,
    CaptureConfirmAmendReverseAndRebookKeepsLedgerTotalsAtZero) {
    const Trade captured = Trade::capture(
        make_id<TradeId>("TRD-1001"),
        make_id<BookId>("BOOK-FX-1"),
        make_id<CounterpartyId>("CPTY-A"),
        make_id<NettingSetId>("NET-A"),
        make_terms(10'000'000, 1'500'000'000));
    ASSERT_EQ(captured.state(), TradeState::Captured);
    ASSERT_EQ(captured.version(), 1U);

    const auto confirmation = captured.apply(TradeAction::ConfirmTrade);
    ASSERT_TRUE(confirmation);
    const Trade& confirmed = confirmation.value();
    EXPECT_EQ(confirmed.state(), TradeState::Confirmed);
    EXPECT_EQ(captured.state(), TradeState::Captured);

    const auto original_entry = build_confirmation_entry(
        confirmed,
        confirmation_ids(
            "PST-V1-PAY-CONTROL",
            "PST-V1-PAY-PAYABLE",
            "PST-V1-RECEIVE-RECEIVABLE",
            "PST-V1-RECEIVE-CONTROL"));
    ASSERT_TRUE(original_entry);
    ASSERT_EQ(original_entry.value().postings().size(), 4U);

    const auto amendment =
        amend_trade(confirmed, make_terms(10'125'000, 1'518'750'000));
    ASSERT_TRUE(amendment);
    const Trade& superseded = amendment.value().superseded();
    const Trade& replacement = amendment.value().replacement();
    EXPECT_EQ(superseded.state(), TradeState::Superseded);
    EXPECT_EQ(superseded.version(), 1U);
    ASSERT_TRUE(superseded.superseded_by());
    EXPECT_EQ(*superseded.superseded_by(), 2U);
    EXPECT_EQ(replacement.state(), TradeState::Confirmed);
    EXPECT_EQ(replacement.version(), 2U);
    ASSERT_TRUE(replacement.supersedes());
    EXPECT_EQ(*replacement.supersedes(), 1U);
    EXPECT_EQ(confirmed.state(), TradeState::Confirmed);
    EXPECT_FALSE(confirmed.superseded_by());

    const auto reversal_entry =
        build_reversal_entry(original_entry.value(), reversal_ids());
    ASSERT_TRUE(reversal_entry);
    ASSERT_EQ(reversal_entry.value().postings().size(), 4U);

    for (std::size_t index = 0U; index < 4U; ++index) {
        const Posting& original = original_entry.value().postings()[index];
        const Posting& reversal = reversal_entry.value().postings()[index];
        EXPECT_EQ(reversal.trade_id(), original.trade_id());
        EXPECT_EQ(reversal.trade_version(), original.trade_version());
        EXPECT_EQ(reversal.account(), original.account());
        EXPECT_EQ(reversal.side(), opposite(original.side()));
        EXPECT_EQ(reversal.amount(), original.amount());
        ASSERT_TRUE(reversal.reversal_of());
        EXPECT_EQ(*reversal.reversal_of(), original.id());
    }

    const auto replacement_entry = build_confirmation_entry(
        replacement,
        confirmation_ids(
            "PST-V2-PAY-CONTROL",
            "PST-V2-PAY-PAYABLE",
            "PST-V2-RECEIVE-RECEIVABLE",
            "PST-V2-RECEIVE-CONTROL"));
    ASSERT_TRUE(replacement_entry);
    ASSERT_EQ(replacement_entry.value().postings().size(), 4U);
    for (const Posting& posting : replacement_entry.value().postings()) {
        EXPECT_EQ(posting.trade_id(), replacement.id());
        EXPECT_EQ(posting.trade_version(), 2U);
        EXPECT_FALSE(posting.reversal_of());
    }

    const std::array entries{
        original_entry.value(),
        reversal_entry.value(),
        replacement_entry.value()};
    auto running = LedgerTotals::zero();
    for (const LedgerEntry& entry : entries) {
        auto updated = running.with_entry(entry);
        ASSERT_TRUE(updated);
        running = std::move(updated).value();
    }

    const auto recomputed = recompute_ledger_totals(entries);
    ASSERT_TRUE(recomputed);
    EXPECT_EQ(running, recomputed.value());
    EXPECT_EQ(running, LedgerTotals::zero());

    const auto usd = running.total(Currency::Usd);
    const auto jpy = running.total(Currency::Jpy);
    const auto kwd = running.total(Currency::Kwd);
    ASSERT_TRUE(usd);
    ASSERT_TRUE(jpy);
    ASSERT_TRUE(kwd);
    EXPECT_EQ(usd.value().format(), "0.00");
    EXPECT_EQ(jpy.value().format(), "0");
    EXPECT_EQ(kwd.value().format(), "0.000");
}

}  // namespace
}  // namespace backbook::domain
