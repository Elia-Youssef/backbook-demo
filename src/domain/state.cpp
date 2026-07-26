#include "backbook/domain/state.hpp"

#include <limits>
#include <utility>

namespace backbook::domain {
namespace {

[[nodiscard]] StateError state_error(const StateErrorCode code) {
    return StateError{code};
}

[[nodiscard]] StateError version_conflict(
    const std::uint32_t expected,
    const std::uint32_t actual) {
    auto error = state_error(StateErrorCode::VersionConflict);
    error.expected_version = expected;
    error.actual_version = actual;
    return error;
}

[[nodiscard]] StateError lifecycle_failure(const LifecycleError failure) {
    auto error = state_error(StateErrorCode::IllegalTransition);
    error.lifecycle_error = failure;
    return error;
}

[[nodiscard]] StateError amendment_failure(const TradeError failure) {
    auto error = state_error(StateErrorCode::AmendmentFailure);
    error.trade_error = failure;
    return error;
}

[[nodiscard]] StateError limit_failure(LimitError failure) {
    auto error = state_error(StateErrorCode::LimitFailure);
    error.limit_error = std::move(failure);
    return error;
}

[[nodiscard]] StateError posting_policy_failure(
    const PostingPolicyError failure) {
    auto error = state_error(StateErrorCode::PostingPolicyFailure);
    error.posting_policy_error = failure;
    return error;
}

[[nodiscard]] StateError ledger_totals_failure(
    const LedgerTotalsError failure) {
    auto error = state_error(StateErrorCode::LedgerTotalsFailure);
    error.ledger_totals_error = failure;
    return error;
}

[[nodiscard]] StateError settlement_failure(
    const SettlementError failure) {
    auto error = state_error(StateErrorCode::SettlementFailure);
    error.settlement_error = failure;
    return error;
}

[[nodiscard]] LimitPath limit_path(const Trade& trade) {
    return LimitPath(
        trade.counterparty_id(),
        trade.netting_set_id(),
        trade.book_id());
}

}  // namespace

State::State(LimitHierarchy limits) : limits_(std::move(limits)) {}

const Trade* State::find_trade(
    const TradeId& trade_id,
    const std::uint32_t version) const noexcept {
    const auto found = trades_.find(TradeVersionKey{trade_id, version});
    return found == trades_.end() ? nullptr : &found->second;
}

const Trade* State::current_trade(const TradeId& trade_id) const noexcept {
    const auto current = current_versions_.find(trade_id);
    if (current == current_versions_.end()) {
        return nullptr;
    }
    return find_trade(trade_id, current->second);
}

namespace detail {

class StateOperations final {
public:
    [[nodiscard]] static Outcome<State, StateError> book(
        const State& state,
        TradeId trade_id,
        BookId book_id,
        CounterpartyId counterparty_id,
        NettingSetId netting_set_id,
        FxTerms terms) {
        if (state.current_versions_.contains(trade_id)) {
            return Outcome<State, StateError>::failure(
                state_error(StateErrorCode::DuplicateTradeId));
        }

        const Trade captured = Trade::capture(
            trade_id,
            std::move(book_id),
            std::move(counterparty_id),
            std::move(netting_set_id),
            std::move(terms));

        auto next = state;
        next.trades_.emplace(
            TradeVersionKey{trade_id, captured.version()},
            captured);
        next.current_versions_.emplace(std::move(trade_id), captured.version());
        const auto version_result = advance_version(next);
        if (version_result.has_error()) {
            return Outcome<State, StateError>::failure(
                version_result.error());
        }
        return Outcome<State, StateError>::success(std::move(next));
    }

    [[nodiscard]] static Outcome<State, StateError> confirm(
        const State& state,
        const TradeId& trade_id,
        const std::uint32_t expected_version,
        const ConfirmationPostingIds& posting_ids) {
        const auto current = require_current(
            state,
            trade_id,
            expected_version);
        if (current.has_error()) {
            return Outcome<State, StateError>::failure(current.error());
        }

        const auto confirmed =
            current.value()->apply(TradeAction::ConfirmTrade);
        if (confirmed.has_error()) {
            return Outcome<State, StateError>::failure(
                lifecycle_failure(confirmed.error()));
        }

        // Limits and postings are prepared before the state copy is changed.
        const auto reserved = state.limits_.reserve(
            limit_path(*current.value()),
            current.value()->terms().pay());
        if (reserved.has_error()) {
            return Outcome<State, StateError>::failure(
                limit_failure(reserved.error()));
        }

        const auto entry =
            build_confirmation_entry(confirmed.value(), posting_ids);
        if (entry.has_error()) {
            return Outcome<State, StateError>::failure(
                posting_policy_failure(entry.error()));
        }

        auto next = state;
        next.limits_ = std::move(reserved).value();
        const TradeVersionKey key{trade_id, expected_version};
        next.trades_.at(key) = confirmed.value();
        const auto entry_index = next.ledger_entries_.size();
        const auto append_result =
            append_entry(next, entry.value());
        if (append_result.has_error()) {
            return Outcome<State, StateError>::failure(
                append_result.error());
        }
        next.confirmation_entries_.emplace(key, entry_index);

        const auto version_result = advance_version(next);
        if (version_result.has_error()) {
            return Outcome<State, StateError>::failure(
                version_result.error());
        }
        return Outcome<State, StateError>::success(std::move(next));
    }

    [[nodiscard]] static Outcome<State, StateError> amend(
        const State& state,
        const TradeId& trade_id,
        const std::uint32_t expected_version,
        FxTerms replacement_terms,
        const ReversalPostingIds& reversal_ids,
        const ConfirmationPostingIds& replacement_posting_ids) {
        const auto current = require_current(
            state,
            trade_id,
            expected_version);
        if (current.has_error()) {
            return Outcome<State, StateError>::failure(current.error());
        }
        if (current.value()->state() != TradeState::Confirmed) {
            return Outcome<State, StateError>::failure(
                lifecycle_failure(LifecycleError::IllegalTransition));
        }

        const TradeVersionKey current_key{trade_id, expected_version};
        const auto original_index =
            state.confirmation_entries_.find(current_key);
        if (original_index == state.confirmation_entries_.end()) {
            return Outcome<State, StateError>::failure(
                state_error(StateErrorCode::MissingConfirmationEntry));
        }

        // Amendment tests replacement headroom after releasing the old
        // reservation in a prospective hierarchy.
        const auto released = state.limits_.release(
            limit_path(*current.value()),
            current.value()->terms().pay());
        if (released.has_error()) {
            return Outcome<State, StateError>::failure(
                limit_failure(released.error()));
        }

        const auto amendment = backbook::domain::amend_trade(
            *current.value(),
            std::move(replacement_terms));
        if (amendment.has_error()) {
            return Outcome<State, StateError>::failure(
                amendment_failure(amendment.error()));
        }

        const auto reserved = released.value().reserve(
            limit_path(amendment.value().replacement()),
            amendment.value().replacement().terms().pay());
        if (reserved.has_error()) {
            return Outcome<State, StateError>::failure(
                limit_failure(reserved.error()));
        }

        const LedgerEntry& original =
            state.ledger_entries_[original_index->second];
        const auto reversal = build_reversal_entry(original, reversal_ids);
        if (reversal.has_error()) {
            return Outcome<State, StateError>::failure(
                posting_policy_failure(reversal.error()));
        }
        const auto replacement = build_confirmation_entry(
            amendment.value().replacement(),
            replacement_posting_ids);
        if (replacement.has_error()) {
            return Outcome<State, StateError>::failure(
                posting_policy_failure(replacement.error()));
        }

        // Only after every check succeeds do reversal and rebook enter the
        // returned state together.
        auto next = state;
        next.limits_ = std::move(reserved).value();
        next.trades_.at(current_key) = amendment.value().superseded();
        const TradeVersionKey replacement_key{
            trade_id,
            amendment.value().replacement().version()};
        next.trades_.emplace(
            replacement_key,
            amendment.value().replacement());
        next.current_versions_.at(trade_id) = replacement_key.version;

        const auto reversal_append =
            append_entry(next, reversal.value());
        if (reversal_append.has_error()) {
            return Outcome<State, StateError>::failure(
                reversal_append.error());
        }
        const auto replacement_index = next.ledger_entries_.size();
        const auto replacement_append =
            append_entry(next, replacement.value());
        if (replacement_append.has_error()) {
            return Outcome<State, StateError>::failure(
                replacement_append.error());
        }
        next.confirmation_entries_.emplace(
            replacement_key,
            replacement_index);

        const auto version_result = advance_version(next);
        if (version_result.has_error()) {
            return Outcome<State, StateError>::failure(
                version_result.error());
        }
        return Outcome<State, StateError>::success(std::move(next));
    }

    [[nodiscard]] static Outcome<State, StateError> cancel(
        const State& state,
        const TradeId& trade_id,
        const std::uint32_t expected_version,
        std::optional<ReversalPostingIds> reversal_ids) {
        const auto current = require_current(
            state,
            trade_id,
            expected_version);
        if (current.has_error()) {
            return Outcome<State, StateError>::failure(current.error());
        }

        const auto cancelled =
            current.value()->apply(TradeAction::CancelTrade);
        if (cancelled.has_error()) {
            return Outcome<State, StateError>::failure(
                lifecycle_failure(cancelled.error()));
        }

        auto next = state;
        const TradeVersionKey key{trade_id, expected_version};
        if (current.value()->state() == TradeState::Captured) {
            // A captured trade has never posted, so cancellation needs no
            // reversal or headroom release.
            if (reversal_ids.has_value()) {
                return Outcome<State, StateError>::failure(
                    state_error(StateErrorCode::UnexpectedReversalIds));
            }
            next.trades_.at(key) = cancelled.value();
        } else {
            if (!reversal_ids.has_value()) {
                return Outcome<State, StateError>::failure(
                    state_error(StateErrorCode::MissingReversalIds));
            }
            const auto original_index =
                state.confirmation_entries_.find(key);
            if (original_index == state.confirmation_entries_.end()) {
                return Outcome<State, StateError>::failure(
                    state_error(StateErrorCode::MissingConfirmationEntry));
            }

            const auto released = state.limits_.release(
                limit_path(*current.value()),
                current.value()->terms().pay());
            if (released.has_error()) {
                return Outcome<State, StateError>::failure(
                    limit_failure(released.error()));
            }
            const auto reversal = build_reversal_entry(
                state.ledger_entries_[original_index->second],
                *reversal_ids);
            if (reversal.has_error()) {
                return Outcome<State, StateError>::failure(
                    posting_policy_failure(reversal.error()));
            }

            next.limits_ = std::move(released).value();
            next.trades_.at(key) = cancelled.value();
            const auto append_result =
                append_entry(next, reversal.value());
            if (append_result.has_error()) {
                return Outcome<State, StateError>::failure(
                    append_result.error());
            }
        }

        const auto version_result = advance_version(next);
        if (version_result.has_error()) {
            return Outcome<State, StateError>::failure(
                version_result.error());
        }
        return Outcome<State, StateError>::success(std::move(next));
    }

    [[nodiscard]] static Outcome<State, StateError> eod(
        const State& state,
        const IsoDate& as_of_date) {
        std::vector<TradeVersionKey> eligible;
        for (const auto& [key, trade] : state.trades_) {
            if (trade.state() == TradeState::Confirmed &&
                trade.terms().value_date() <= as_of_date) {
                eligible.push_back(key);
            }
        }
        if (eligible.empty()) {
            return Outcome<State, StateError>::success(state);
        }

        auto next = state;
        for (const TradeVersionKey& key : eligible) {
            const Trade& trade = next.trades_.at(key);
            const auto released = next.limits_.release(
                limit_path(trade),
                trade.terms().pay());
            if (released.has_error()) {
                return Outcome<State, StateError>::failure(
                    limit_failure(released.error()));
            }
            const auto settled = trade.apply(TradeAction::RunEod);
            if (settled.has_error()) {
                return Outcome<State, StateError>::failure(
                    lifecycle_failure(settled.error()));
            }
            next.limits_ = std::move(released).value();
            next.trades_.at(key) = settled.value();
        }

        // Settlement obligations are rebuilt from all settled versions so the
        // result is deterministic after every EOD run.
        std::vector<Trade> settled_trades;
        settled_trades.reserve(next.trades_.size());
        for (const auto& [unused, trade] : next.trades_) {
            static_cast<void>(unused);
            if (trade.state() == TradeState::Settled) {
                settled_trades.push_back(trade);
            }
        }
        const auto obligations =
            derive_bilateral_settlements(settled_trades);
        if (obligations.has_error()) {
            return Outcome<State, StateError>::failure(
                settlement_failure(obligations.error()));
        }
        next.settlements_ = obligations.value();

        const auto version_result = advance_version(next);
        if (version_result.has_error()) {
            return Outcome<State, StateError>::failure(
                version_result.error());
        }
        return Outcome<State, StateError>::success(std::move(next));
    }

private:
    [[nodiscard]] static Outcome<const Trade*, StateError> require_current(
        const State& state,
        const TradeId& trade_id,
        const std::uint32_t expected_version) {
        const Trade* current = state.current_trade(trade_id);
        if (current == nullptr) {
            return Outcome<const Trade*, StateError>::failure(
                state_error(StateErrorCode::TradeNotFound));
        }
        if (current->version() != expected_version) {
            return Outcome<const Trade*, StateError>::failure(
                version_conflict(expected_version, current->version()));
        }
        return Outcome<const Trade*, StateError>::success(current);
    }

    [[nodiscard]] static Outcome<std::uint8_t, StateError> append_entry(
        State& state,
        const LedgerEntry& entry) {
        // Check global posting-ID uniqueness and totals before mutating either
        // index.
        for (const Posting& posting : entry.postings()) {
            if (state.posting_ids_.contains(posting.id())) {
                return Outcome<std::uint8_t, StateError>::failure(
                    state_error(StateErrorCode::DuplicatePostingId));
            }
        }

        const auto totals = state.ledger_totals_.with_entry(entry);
        if (totals.has_error()) {
            return Outcome<std::uint8_t, StateError>::failure(
                ledger_totals_failure(totals.error()));
        }

        for (const Posting& posting : entry.postings()) {
            state.posting_ids_.insert(posting.id());
        }
        state.ledger_totals_ = totals.value();
        state.ledger_entries_.push_back(entry);
        return Outcome<std::uint8_t, StateError>::success(0U);
    }

    [[nodiscard]] static Outcome<std::uint8_t, StateError> advance_version(
        State& state) {
        if (state.version_ == std::numeric_limits<std::uint64_t>::max()) {
            return Outcome<std::uint8_t, StateError>::failure(
                state_error(StateErrorCode::StateVersionOverflow));
        }
        ++state.version_;
        return Outcome<std::uint8_t, StateError>::success(0U);
    }
};

}  // namespace detail

Outcome<State, StateError> book_trade(
    const State& state,
    TradeId trade_id,
    BookId book_id,
    CounterpartyId counterparty_id,
    NettingSetId netting_set_id,
    FxTerms terms) {
    return detail::StateOperations::book(
        state,
        std::move(trade_id),
        std::move(book_id),
        std::move(counterparty_id),
        std::move(netting_set_id),
        std::move(terms));
}

Outcome<State, StateError> confirm_trade(
    const State& state,
    const TradeId& trade_id,
    const std::uint32_t expected_version,
    const ConfirmationPostingIds& posting_ids) {
    return detail::StateOperations::confirm(
        state,
        trade_id,
        expected_version,
        posting_ids);
}

Outcome<State, StateError> amend_trade(
    const State& state,
    const TradeId& trade_id,
    const std::uint32_t expected_version,
    FxTerms replacement_terms,
    const ReversalPostingIds& reversal_ids,
    const ConfirmationPostingIds& replacement_posting_ids) {
    return detail::StateOperations::amend(
        state,
        trade_id,
        expected_version,
        std::move(replacement_terms),
        reversal_ids,
        replacement_posting_ids);
}

Outcome<State, StateError> cancel_trade(
    const State& state,
    const TradeId& trade_id,
    const std::uint32_t expected_version,
    std::optional<ReversalPostingIds> reversal_ids) {
    return detail::StateOperations::cancel(
        state,
        trade_id,
        expected_version,
        std::move(reversal_ids));
}

Outcome<State, StateError> run_eod(
    const State& state,
    const IsoDate& as_of_date) {
    return detail::StateOperations::eod(state, as_of_date);
}

}  // namespace backbook::domain
