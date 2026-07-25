#include "backbook/domain/state.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
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

[[nodiscard]] FxTerms terms(
    const std::int64_t pay_minor_units,
    const std::int64_t receive_minor_units,
    const std::string_view value_date = "2026-07-27") {
    auto result = FxTerms::create(
        InstrumentKind::FxSpot,
        date("2026-07-25"),
        date(value_date),
        money(Currency::Usd, pay_minor_units),
        money(Currency::Jpy, receive_minor_units));
    EXPECT_TRUE(result);
    return std::move(result).value();
}

[[nodiscard]] LimitPath path_a() {
    return LimitPath(
        make_id<CounterpartyId>("CPTY-A"),
        make_id<NettingSetId>("NET-A"),
        make_id<BookId>("BOOK-FX-1"));
}

[[nodiscard]] LimitPath path_b() {
    return LimitPath(
        make_id<CounterpartyId>("CPTY-B"),
        make_id<NettingSetId>("NET-B"),
        make_id<BookId>("BOOK-FX-2"));
}

void add_branch(
    std::vector<LimitDefinition>& definitions,
    const LimitPath& path,
    const Currency currency,
    const std::int64_t branch_capacity,
    const std::int64_t book_capacity) {
    const auto nodes = path.nodes();
    definitions.push_back(
        LimitDefinition{nodes[1U], money(currency, branch_capacity)});
    definitions.push_back(
        LimitDefinition{nodes[2U], money(currency, branch_capacity)});
    definitions.push_back(
        LimitDefinition{nodes[3U], money(currency, book_capacity)});
}

[[nodiscard]] LimitHierarchy limits() {
    std::vector<LimitDefinition> definitions;
    for (const Currency currency :
         {Currency::Usd, Currency::Jpy, Currency::Kwd}) {
        const std::int64_t group_capacity =
            currency == Currency::Usd ? 100'000'000 : 2'000'000'000;
        definitions.push_back(LimitDefinition{
            LimitNode::group(),
            money(currency, group_capacity)});
        add_branch(
            definitions,
            path_a(),
            currency,
            group_capacity,
            currency == Currency::Usd ? 15'000'000 : 2'000'000'000);
        add_branch(
            definitions,
            path_b(),
            currency,
            group_capacity,
            group_capacity);
    }

    auto result = LimitHierarchy::create(std::move(definitions));
    EXPECT_TRUE(result);
    return std::move(result).value();
}

[[nodiscard]] State initial_state() {
    return State(limits());
}

[[nodiscard]] ConfirmationPostingIds confirmation_ids(
    const std::string& prefix) {
    return ConfirmationPostingIds{
        make_id<PostingId>(prefix + "-PAY-CONTROL-D"),
        make_id<PostingId>(prefix + "-PAY-PAYABLE-C"),
        make_id<PostingId>(prefix + "-RECEIVE-RECEIVABLE-D"),
        make_id<PostingId>(prefix + "-RECEIVE-CONTROL-C")};
}

[[nodiscard]] ReversalPostingIds reversal_ids(const std::string& prefix) {
    return ReversalPostingIds{
        make_id<PostingId>(prefix + "-PAY-CONTROL-C"),
        make_id<PostingId>(prefix + "-PAY-PAYABLE-D"),
        make_id<PostingId>(prefix + "-RECEIVE-RECEIVABLE-C"),
        make_id<PostingId>(prefix + "-RECEIVE-CONTROL-D")};
}

[[nodiscard]] Outcome<State, StateError> book(
    const State& state,
    const std::string_view trade_id,
    FxTerms trade_terms,
    const LimitPath& path = path_a()) {
    return book_trade(
        state,
        make_id<TradeId>(trade_id),
        path.book_id(),
        path.counterparty_id(),
        path.netting_set_id(),
        std::move(trade_terms));
}

[[nodiscard]] std::int64_t headroom(
    const State& state,
    const LimitNode& node,
    const Currency currency) {
    const auto result = state.limits().headroom(node, currency);
    EXPECT_TRUE(result);
    return result.value().minor_units();
}

TEST(StateTest, BooksVersionOneWithoutMutatingPriorSnapshot) {
    const State empty = initial_state();

    const auto result = book(
        empty,
        "TRD-1001",
        terms(10'000'000, 1'500'000'000));

    ASSERT_TRUE(result);
    EXPECT_EQ(empty.version(), 0U);
    EXPECT_TRUE(empty.trade_versions().empty());
    EXPECT_EQ(result.value().version(), 1U);
    ASSERT_NE(
        result.value().current_trade(make_id<TradeId>("TRD-1001")),
        nullptr);
    EXPECT_EQ(
        result.value()
            .current_trade(make_id<TradeId>("TRD-1001"))
            ->state(),
        TradeState::Captured);
}

TEST(StateTest, RejectsDuplicateTradeIdAndPreservesBookedSnapshot) {
    const auto booked = book(
        initial_state(),
        "TRD-1001",
        terms(10'000'000, 1'500'000'000));
    ASSERT_TRUE(booked);

    const auto duplicate = book(
        booked.value(),
        "TRD-1001",
        terms(1, 1));

    ASSERT_TRUE(duplicate.has_error());
    EXPECT_EQ(duplicate.error().code, StateErrorCode::DuplicateTradeId);
    EXPECT_EQ(booked.value().version(), 1U);
    EXPECT_EQ(booked.value().trade_versions().size(), 1U);
}

TEST(StateTest, ConfirmationReservesPayCurrencyAndPostsExactlyOnce) {
    const auto booked = book(
        initial_state(),
        "TRD-1001",
        terms(10'000'000, 1'500'000'000));
    ASSERT_TRUE(booked);
    const auto ids = confirmation_ids("PST-V1");

    const auto confirmed = confirm_trade(
        booked.value(),
        make_id<TradeId>("TRD-1001"),
        1U,
        ids);

    ASSERT_TRUE(confirmed);
    EXPECT_EQ(confirmed.value().version(), 2U);
    EXPECT_EQ(
        confirmed.value()
            .current_trade(make_id<TradeId>("TRD-1001"))
            ->state(),
        TradeState::Confirmed);
    EXPECT_EQ(confirmed.value().ledger_entries().size(), 1U);
    EXPECT_EQ(confirmed.value().posting_count(), 4U);
    EXPECT_EQ(confirmed.value().ledger_totals(), LedgerTotals::zero());
    for (const LimitNode& node : path_a().nodes()) {
        const auto expected_usd =
            node.level() == LimitLevel::Book ? 5'000'000 : 90'000'000;
        EXPECT_EQ(
            headroom(confirmed.value(), node, Currency::Usd),
            expected_usd);
        EXPECT_EQ(
            headroom(confirmed.value(), node, Currency::Jpy),
            node.level() == LimitLevel::Group
                ? 2'000'000'000
                : 2'000'000'000);
    }
    EXPECT_EQ(booked.value().ledger_entries().size(), 0U);
    EXPECT_EQ(
        headroom(
            booked.value(),
            path_a().nodes()[3U],
            Currency::Usd),
        15'000'000);
}

TEST(StateTest, ConfirmationReportsNotFoundVersionAndLifecycleFailures) {
    const auto missing = confirm_trade(
        initial_state(),
        make_id<TradeId>("TRD-MISSING"),
        1U,
        confirmation_ids("PST-MISSING"));
    ASSERT_TRUE(missing.has_error());
    EXPECT_EQ(missing.error().code, StateErrorCode::TradeNotFound);

    const auto booked = book(
        initial_state(),
        "TRD-1001",
        terms(100, 100));
    ASSERT_TRUE(booked);
    const auto stale = confirm_trade(
        booked.value(),
        make_id<TradeId>("TRD-1001"),
        2U,
        confirmation_ids("PST-STALE"));
    ASSERT_TRUE(stale.has_error());
    EXPECT_EQ(stale.error().code, StateErrorCode::VersionConflict);
    EXPECT_EQ(stale.error().expected_version, 2U);
    EXPECT_EQ(stale.error().actual_version, 1U);

    const auto confirmed = confirm_trade(
        booked.value(),
        make_id<TradeId>("TRD-1001"),
        1U,
        confirmation_ids("PST-V1"));
    ASSERT_TRUE(confirmed);
    const auto repeated = confirm_trade(
        confirmed.value(),
        make_id<TradeId>("TRD-1001"),
        1U,
        confirmation_ids("PST-REPEAT"));
    ASSERT_TRUE(repeated.has_error());
    EXPECT_EQ(repeated.error().code, StateErrorCode::IllegalTransition);
}

TEST(StateTest, LimitBreachLeavesTradeLedgerAndHeadroomUnchanged) {
    const auto first_booked = book(
        initial_state(),
        "TRD-1001",
        terms(10'125'000, 1'518'750'000));
    ASSERT_TRUE(first_booked);
    const auto first_confirmed = confirm_trade(
        first_booked.value(),
        make_id<TradeId>("TRD-1001"),
        1U,
        confirmation_ids("PST-TRD1"));
    ASSERT_TRUE(first_confirmed);
    const auto second_booked = book(
        first_confirmed.value(),
        "TRD-1002",
        terms(6'000'000, 900'000'000));
    ASSERT_TRUE(second_booked);

    const auto before_version = second_booked.value().version();
    const auto before_postings = second_booked.value().posting_count();
    const auto before_entries =
        second_booked.value().ledger_entries().size();
    const auto before_headroom = headroom(
        second_booked.value(),
        path_a().nodes()[3U],
        Currency::Usd);

    const auto breach = confirm_trade(
        second_booked.value(),
        make_id<TradeId>("TRD-1002"),
        1U,
        confirmation_ids("PST-TRD2"));

    ASSERT_TRUE(breach.has_error());
    EXPECT_EQ(breach.error().code, StateErrorCode::LimitFailure);
    ASSERT_TRUE(breach.error().limit_error.has_value());
    EXPECT_EQ(
        breach.error().limit_error->code,
        LimitErrorCode::Breach);
    EXPECT_EQ(
        breach.error().limit_error->node.level(),
        LimitLevel::Book);
    EXPECT_EQ(
        breach.error().limit_error->required_minor_units,
        6'000'000);
    EXPECT_EQ(
        breach.error().limit_error->remaining_minor_units,
        4'875'000);
    EXPECT_EQ(second_booked.value().version(), before_version);
    EXPECT_EQ(second_booked.value().posting_count(), before_postings);
    EXPECT_EQ(
        second_booked.value().ledger_entries().size(),
        before_entries);
    EXPECT_EQ(
        headroom(
            second_booked.value(),
            path_a().nodes()[3U],
            Currency::Usd),
        before_headroom);
    EXPECT_EQ(
        second_booked.value()
            .current_trade(make_id<TradeId>("TRD-1002"))
            ->state(),
        TradeState::Captured);
}

TEST(StateTest, PostingIdsAreUniqueAcrossSeparateLedgerEntries) {
    const auto first_booked = book(
        initial_state(),
        "TRD-1001",
        terms(100, 100));
    ASSERT_TRUE(first_booked);
    const auto first_ids = confirmation_ids("PST-FIRST");
    const auto first_confirmed = confirm_trade(
        first_booked.value(),
        make_id<TradeId>("TRD-1001"),
        1U,
        first_ids);
    ASSERT_TRUE(first_confirmed);
    const auto second_booked = book(
        first_confirmed.value(),
        "TRD-1002",
        terms(100, 100));
    ASSERT_TRUE(second_booked);

    auto second_ids = confirmation_ids("PST-SECOND");
    second_ids.receive_control_credit = first_ids.pay_control_debit;
    const auto duplicate = confirm_trade(
        second_booked.value(),
        make_id<TradeId>("TRD-1002"),
        1U,
        second_ids);

    ASSERT_TRUE(duplicate.has_error());
    EXPECT_EQ(
        duplicate.error().code,
        StateErrorCode::DuplicatePostingId);
    EXPECT_EQ(second_booked.value().posting_count(), 4U);
    EXPECT_EQ(second_booked.value().ledger_entries().size(), 1U);
    EXPECT_EQ(
        second_booked.value()
            .current_trade(make_id<TradeId>("TRD-1002"))
            ->state(),
        TradeState::Captured);
}

TEST(StateTest, AmendmentAtomicallyReversesRebooksAndReplacesReservation) {
    const auto booked = book(
        initial_state(),
        "TRD-1001",
        terms(10'000'000, 1'500'000'000));
    ASSERT_TRUE(booked);
    const auto confirmed = confirm_trade(
        booked.value(),
        make_id<TradeId>("TRD-1001"),
        1U,
        confirmation_ids("PST-V1"));
    ASSERT_TRUE(confirmed);

    const auto amended = amend_trade(
        confirmed.value(),
        make_id<TradeId>("TRD-1001"),
        1U,
        terms(10'125'000, 1'518'750'000),
        reversal_ids("PST-V1-REV"),
        confirmation_ids("PST-V2"));

    ASSERT_TRUE(amended);
    EXPECT_EQ(amended.value().version(), 3U);
    const Trade* version_one = amended.value().find_trade(
        make_id<TradeId>("TRD-1001"),
        1U);
    const Trade* version_two = amended.value().find_trade(
        make_id<TradeId>("TRD-1001"),
        2U);
    ASSERT_NE(version_one, nullptr);
    ASSERT_NE(version_two, nullptr);
    EXPECT_EQ(version_one->state(), TradeState::Superseded);
    EXPECT_EQ(version_one->superseded_by(), 2U);
    EXPECT_EQ(version_two->state(), TradeState::Confirmed);
    EXPECT_EQ(version_two->supersedes(), 1U);
    EXPECT_EQ(
        amended.value()
            .current_trade(make_id<TradeId>("TRD-1001"))
            ->version(),
        2U);
    ASSERT_EQ(amended.value().ledger_entries().size(), 3U);
    EXPECT_EQ(amended.value().posting_count(), 12U);
    EXPECT_EQ(amended.value().ledger_totals(), LedgerTotals::zero());
    EXPECT_EQ(
        headroom(
            amended.value(),
            path_a().nodes()[3U],
            Currency::Usd),
        4'875'000);

    const auto& original = amended.value().ledger_entries()[0U].postings();
    const auto& reversal = amended.value().ledger_entries()[1U].postings();
    for (std::size_t index = 0U; index < original.size(); ++index) {
        ASSERT_TRUE(reversal[index].reversal_of().has_value());
        EXPECT_EQ(*reversal[index].reversal_of(), original[index].id());
    }
}

TEST(StateTest, FailedAmendmentKeepsOldVersionAndReservationConfirmed) {
    const auto booked = book(
        initial_state(),
        "TRD-1001",
        terms(10'000'000, 1'500'000'000));
    ASSERT_TRUE(booked);
    const auto confirmed = confirm_trade(
        booked.value(),
        make_id<TradeId>("TRD-1001"),
        1U,
        confirmation_ids("PST-V1"));
    ASSERT_TRUE(confirmed);

    const auto failed = amend_trade(
        confirmed.value(),
        make_id<TradeId>("TRD-1001"),
        1U,
        terms(16'000'000, 2'400'000'000),
        reversal_ids("PST-V1-REV"),
        confirmation_ids("PST-V2"));

    ASSERT_TRUE(failed.has_error());
    EXPECT_EQ(failed.error().code, StateErrorCode::LimitFailure);
    ASSERT_TRUE(failed.error().limit_error.has_value());
    EXPECT_EQ(failed.error().limit_error->node.level(), LimitLevel::Book);
    EXPECT_EQ(
        failed.error().limit_error->remaining_minor_units,
        15'000'000);
    EXPECT_EQ(confirmed.value().version(), 2U);
    EXPECT_EQ(confirmed.value().trade_versions().size(), 1U);
    EXPECT_EQ(
        confirmed.value()
            .current_trade(make_id<TradeId>("TRD-1001"))
            ->state(),
        TradeState::Confirmed);
    EXPECT_EQ(confirmed.value().ledger_entries().size(), 1U);
    EXPECT_EQ(confirmed.value().posting_count(), 4U);
    EXPECT_EQ(
        headroom(
            confirmed.value(),
            path_a().nodes()[3U],
            Currency::Usd),
        5'000'000);
}

TEST(StateTest, AmendmentPostingConflictDiscardsWholeProspectiveState) {
    const auto booked = book(
        initial_state(),
        "TRD-1001",
        terms(10'000'000, 1'500'000'000));
    ASSERT_TRUE(booked);
    const auto confirmed = confirm_trade(
        booked.value(),
        make_id<TradeId>("TRD-1001"),
        1U,
        confirmation_ids("PST-V1"));
    ASSERT_TRUE(confirmed);

    const auto reversal = reversal_ids("PST-V1-REV");
    auto replacement = confirmation_ids("PST-V2");
    replacement.pay_control_debit = reversal.receive_control_debit;
    const auto failed = amend_trade(
        confirmed.value(),
        make_id<TradeId>("TRD-1001"),
        1U,
        terms(10'125'000, 1'518'750'000),
        reversal,
        replacement);

    ASSERT_TRUE(failed.has_error());
    EXPECT_EQ(failed.error().code, StateErrorCode::DuplicatePostingId);
    EXPECT_EQ(confirmed.value().version(), 2U);
    EXPECT_EQ(confirmed.value().trade_versions().size(), 1U);
    EXPECT_EQ(
        confirmed.value()
            .current_trade(make_id<TradeId>("TRD-1001"))
            ->state(),
        TradeState::Confirmed);
    EXPECT_EQ(confirmed.value().ledger_entries().size(), 1U);
    EXPECT_EQ(confirmed.value().posting_count(), 4U);
    EXPECT_EQ(
        headroom(
            confirmed.value(),
            path_a().nodes()[3U],
            Currency::Usd),
        5'000'000);
}

TEST(StateTest, CancellingCapturedTradeCreatesNoLedgerReversal) {
    const auto booked = book(
        initial_state(),
        "TRD-1001",
        terms(100, 100));
    ASSERT_TRUE(booked);

    const auto cancelled = cancel_trade(
        booked.value(),
        make_id<TradeId>("TRD-1001"),
        1U,
        std::nullopt);

    ASSERT_TRUE(cancelled);
    EXPECT_EQ(cancelled.value().version(), 2U);
    EXPECT_EQ(
        cancelled.value()
            .current_trade(make_id<TradeId>("TRD-1001"))
            ->state(),
        TradeState::Cancelled);
    EXPECT_TRUE(cancelled.value().ledger_entries().empty());
    EXPECT_EQ(cancelled.value().posting_count(), 0U);
}

TEST(StateTest, CancellingConfirmedTradeReversesAndReleasesExactlyOnce) {
    const auto booked = book(
        initial_state(),
        "TRD-1001",
        terms(10'000'000, 1'500'000'000));
    ASSERT_TRUE(booked);
    const auto confirmed = confirm_trade(
        booked.value(),
        make_id<TradeId>("TRD-1001"),
        1U,
        confirmation_ids("PST-V1"));
    ASSERT_TRUE(confirmed);

    const auto cancelled = cancel_trade(
        confirmed.value(),
        make_id<TradeId>("TRD-1001"),
        1U,
        reversal_ids("PST-V1-REV"));

    ASSERT_TRUE(cancelled);
    EXPECT_EQ(
        cancelled.value()
            .current_trade(make_id<TradeId>("TRD-1001"))
            ->state(),
        TradeState::Cancelled);
    EXPECT_EQ(cancelled.value().ledger_entries().size(), 2U);
    EXPECT_EQ(cancelled.value().posting_count(), 8U);
    EXPECT_EQ(cancelled.value().ledger_totals(), LedgerTotals::zero());
    EXPECT_EQ(
        headroom(
            cancelled.value(),
            path_a().nodes()[3U],
            Currency::Usd),
        15'000'000);

    const auto repeated = cancel_trade(
        cancelled.value(),
        make_id<TradeId>("TRD-1001"),
        1U,
        reversal_ids("PST-SECOND-REV"));
    ASSERT_TRUE(repeated.has_error());
    EXPECT_EQ(repeated.error().code, StateErrorCode::IllegalTransition);
}

TEST(StateTest, CancellationRequiresIdsOnlyForConfirmedTrade) {
    const auto booked = book(
        initial_state(),
        "TRD-1001",
        terms(100, 100));
    ASSERT_TRUE(booked);
    const auto unexpected = cancel_trade(
        booked.value(),
        make_id<TradeId>("TRD-1001"),
        1U,
        reversal_ids("PST-UNEXPECTED"));
    ASSERT_TRUE(unexpected.has_error());
    EXPECT_EQ(
        unexpected.error().code,
        StateErrorCode::UnexpectedReversalIds);

    const auto confirmed = confirm_trade(
        booked.value(),
        make_id<TradeId>("TRD-1001"),
        1U,
        confirmation_ids("PST-V1"));
    ASSERT_TRUE(confirmed);
    const auto missing = cancel_trade(
        confirmed.value(),
        make_id<TradeId>("TRD-1001"),
        1U,
        std::nullopt);
    ASSERT_TRUE(missing.has_error());
    EXPECT_EQ(
        missing.error().code,
        StateErrorCode::MissingReversalIds);
    EXPECT_EQ(confirmed.value().ledger_entries().size(), 1U);
}

TEST(StateTest, EodSettlesOnlyEligibleTradesReleasesAndDoesNotDuplicate) {
    const auto first_booked = book(
        initial_state(),
        "TRD-1001",
        terms(100, 150, "2026-07-27"));
    ASSERT_TRUE(first_booked);
    const auto first_confirmed = confirm_trade(
        first_booked.value(),
        make_id<TradeId>("TRD-1001"),
        1U,
        confirmation_ids("PST-TRD1"));
    ASSERT_TRUE(first_confirmed);
    const auto second_booked = book(
        first_confirmed.value(),
        "TRD-1002",
        terms(70, 100, "2026-07-29"));
    ASSERT_TRUE(second_booked);
    const auto both_confirmed = confirm_trade(
        second_booked.value(),
        make_id<TradeId>("TRD-1002"),
        1U,
        confirmation_ids("PST-TRD2"));
    ASSERT_TRUE(both_confirmed);

    const auto first_eod = run_eod(
        both_confirmed.value(),
        date("2026-07-27"));

    ASSERT_TRUE(first_eod);
    EXPECT_EQ(first_eod.value().version(), 5U);
    EXPECT_EQ(
        first_eod.value()
            .current_trade(make_id<TradeId>("TRD-1001"))
            ->state(),
        TradeState::Settled);
    EXPECT_EQ(
        first_eod.value()
            .current_trade(make_id<TradeId>("TRD-1002"))
            ->state(),
        TradeState::Confirmed);
    EXPECT_EQ(first_eod.value().settlements().size(), 2U);
    EXPECT_EQ(
        headroom(
            first_eod.value(),
            path_a().nodes()[3U],
            Currency::Usd),
        15'000'000 - 70);

    const auto repeated = run_eod(
        first_eod.value(),
        date("2026-07-27"));
    ASSERT_TRUE(repeated);
    EXPECT_EQ(repeated.value().version(), first_eod.value().version());
    EXPECT_EQ(
        repeated.value().settlements(),
        first_eod.value().settlements());

    const auto second_eod = run_eod(
        repeated.value(),
        date("2026-07-29"));
    ASSERT_TRUE(second_eod);
    EXPECT_EQ(second_eod.value().version(), 6U);
    EXPECT_EQ(
        second_eod.value()
            .current_trade(make_id<TradeId>("TRD-1002"))
            ->state(),
        TradeState::Settled);
    EXPECT_EQ(
        headroom(
            second_eod.value(),
            path_a().nodes()[3U],
            Currency::Usd),
        15'000'000);
    EXPECT_EQ(second_eod.value().settlements().size(), 4U);
    EXPECT_EQ(second_eod.value().ledger_entries().size(), 2U);
    EXPECT_EQ(second_eod.value().posting_count(), 8U);
}

}  // namespace
}  // namespace backbook::domain
