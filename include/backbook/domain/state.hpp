#pragma once

#include "backbook/domain/ledger.hpp"
#include "backbook/domain/ledger_totals.hpp"
#include "backbook/domain/limits.hpp"
#include "backbook/domain/posting_policy.hpp"
#include "backbook/domain/settlement.hpp"
#include "backbook/domain/trade.hpp"

#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace backbook::domain {

namespace detail {

class StateOperations;

}  // namespace detail

struct TradeVersionKey final {
    TradeId trade_id;
    std::uint32_t version;

    [[nodiscard]] friend bool operator==(
        const TradeVersionKey&,
        const TradeVersionKey&) = default;
    [[nodiscard]] friend auto operator<=>(
        const TradeVersionKey&,
        const TradeVersionKey&) = default;
};

enum class StateErrorCode : std::uint8_t {
    DuplicateTradeId,
    TradeNotFound,
    VersionConflict,
    IllegalTransition,
    AmendmentFailure,
    LimitFailure,
    PostingPolicyFailure,
    DuplicatePostingId,
    MissingConfirmationEntry,
    MissingReversalIds,
    UnexpectedReversalIds,
    LedgerTotalsFailure,
    SettlementFailure,
    StateVersionOverflow,
};

struct StateError final {
    StateErrorCode code;
    std::optional<std::uint32_t> expected_version{};
    std::optional<std::uint32_t> actual_version{};
    std::optional<LifecycleError> lifecycle_error{};
    std::optional<TradeError> trade_error{};
    std::optional<LimitError> limit_error{};
    std::optional<PostingPolicyError> posting_policy_error{};
    std::optional<LedgerTotalsError> ledger_totals_error{};
    std::optional<SettlementError> settlement_error{};

    [[nodiscard]] friend bool operator==(
        const StateError&,
        const StateError&) = default;
};

// State-changing functions copy this value and return a complete prospective
// state. The service publishes that copy only after durable append succeeds.
class State final {
public:
    explicit State(LimitHierarchy limits);

    [[nodiscard]] std::uint64_t version() const noexcept {
        return version_;
    }

    [[nodiscard]] const Trade* find_trade(
        const TradeId& trade_id,
        std::uint32_t version) const noexcept;
    [[nodiscard]] const Trade* current_trade(
        const TradeId& trade_id) const noexcept;

    [[nodiscard]] const std::map<TradeVersionKey, Trade>& trade_versions()
        const noexcept {
        return trades_;
    }

    [[nodiscard]] const std::map<TradeId, std::uint32_t>& current_versions()
        const noexcept {
        return current_versions_;
    }

    [[nodiscard]] const std::vector<LedgerEntry>& ledger_entries()
        const noexcept {
        return ledger_entries_;
    }

    [[nodiscard]] const LedgerTotals& ledger_totals() const noexcept {
        return ledger_totals_;
    }

    [[nodiscard]] const LimitHierarchy& limits() const noexcept {
        return limits_;
    }

    [[nodiscard]] const std::vector<SettlementObligation>& settlements()
        const noexcept {
        return settlements_;
    }

    [[nodiscard]] std::size_t posting_count() const noexcept {
        return posting_ids_.size();
    }

    [[nodiscard]] bool has_posting_id(const PostingId& posting_id)
        const noexcept {
        return posting_ids_.contains(posting_id);
    }

private:
    friend class detail::StateOperations;

    std::uint64_t version_{0};
    std::map<TradeVersionKey, Trade> trades_;
    std::map<TradeId, std::uint32_t> current_versions_;
    std::vector<LedgerEntry> ledger_entries_;
    std::map<TradeVersionKey, std::size_t> confirmation_entries_;
    std::set<PostingId> posting_ids_;
    LedgerTotals ledger_totals_{LedgerTotals::zero()};
    LimitHierarchy limits_;
    std::vector<SettlementObligation> settlements_;
};

[[nodiscard]] Outcome<State, StateError> book_trade(
    const State& state,
    TradeId trade_id,
    BookId book_id,
    CounterpartyId counterparty_id,
    NettingSetId netting_set_id,
    FxTerms terms);

[[nodiscard]] Outcome<State, StateError> confirm_trade(
    const State& state,
    const TradeId& trade_id,
    std::uint32_t expected_version,
    const ConfirmationPostingIds& posting_ids);

[[nodiscard]] Outcome<State, StateError> amend_trade(
    const State& state,
    const TradeId& trade_id,
    std::uint32_t expected_version,
    FxTerms replacement_terms,
    const ReversalPostingIds& reversal_ids,
    const ConfirmationPostingIds& replacement_posting_ids);

[[nodiscard]] Outcome<State, StateError> cancel_trade(
    const State& state,
    const TradeId& trade_id,
    std::uint32_t expected_version,
    std::optional<ReversalPostingIds> reversal_ids);

[[nodiscard]] Outcome<State, StateError> run_eod(
    const State& state,
    const IsoDate& as_of_date);

}  // namespace backbook::domain
