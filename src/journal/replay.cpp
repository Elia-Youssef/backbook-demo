#include "backbook/journal/replay.hpp"

#include "backbook/domain/ledger_totals.hpp"

#include <type_traits>
#include <utility>
#include <variant>

namespace backbook::journal {
namespace {

template <typename> inline constexpr bool always_false = false;

[[nodiscard]] ReplayError replay_error(const ReplayErrorCode code,
                                       const std::uint64_t batch_sequence,
                                       const std::uint64_t expected_sequence,
                                       const std::size_t event_index) {
    return ReplayError{code, batch_sequence, expected_sequence, event_index};
}

[[nodiscard]] domain::Outcome<domain::State, domain::StateError>
apply_event(const domain::State& state, const Event& event) {
    return std::visit(
        [&state](const auto& value)
            -> domain::Outcome<domain::State, domain::StateError> {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, TradeBookedEvent>) {
                return domain::book_trade(state,
                                          value.trade_id,
                                          value.book_id,
                                          value.counterparty_id,
                                          value.netting_set_id,
                                          value.terms);
            } else if constexpr (std::is_same_v<Value, TradeConfirmedEvent>) {
                return domain::confirm_trade(state,
                                             value.trade_id,
                                             value.expected_version,
                                             value.posting_ids);
            } else if constexpr (std::is_same_v<Value, TradeAmendedEvent>) {
                return domain::amend_trade(state,
                                           value.trade_id,
                                           value.expected_version,
                                           value.replacement_terms,
                                           value.reversal_ids,
                                           value.replacement_posting_ids);
            } else if constexpr (std::is_same_v<Value, TradeCancelledEvent>) {
                return domain::cancel_trade(state,
                                            value.trade_id,
                                            value.expected_version,
                                            value.reversal_ids);
            } else if constexpr (std::is_same_v<Value, EodRunEvent>) {
                return domain::run_eod(state, value.as_of_date);
            } else {
                static_assert(always_false<Value>);
            }
        },
        event);
}

[[nodiscard]] std::uint64_t settled_trade_count(const domain::State& state) {
    std::uint64_t count = 0U;
    for (const auto& [unused, trade] : state.trade_versions()) {
        static_cast<void>(unused);
        if (trade.state() == domain::TradeState::Settled) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] bool result_matches(const CommandResult& result,
                                  const Event& event,
                                  const domain::State& before,
                                  const domain::State& after) {
    if (result_state_version(result) != after.version()) {
        return false;
    }

    return std::visit(
        [&event, &before, &after](const auto& value) {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, TradeBookedResult>) {
                const auto* applied = std::get_if<TradeBookedEvent>(&event);
                const auto* trade =
                    after.find_trade(value.trade_id, value.version);
                return applied != nullptr &&
                       applied->trade_id == value.trade_id &&
                       value.version == 1U && trade != nullptr &&
                       trade->state() == domain::TradeState::Captured;
            } else if constexpr (std::is_same_v<Value, TradeConfirmedResult>) {
                const auto* applied = std::get_if<TradeConfirmedEvent>(&event);
                const auto* trade =
                    after.find_trade(value.trade_id, value.version);
                return applied != nullptr &&
                       applied->trade_id == value.trade_id &&
                       value.version == applied->expected_version &&
                       trade != nullptr &&
                       trade->state() == domain::TradeState::Confirmed;
            } else if constexpr (std::is_same_v<Value, TradeAmendedResult>) {
                const auto* applied = std::get_if<TradeAmendedEvent>(&event);
                const auto* superseded =
                    after.find_trade(value.trade_id, value.superseded_version);
                const auto* replacement =
                    after.find_trade(value.trade_id, value.replacement_version);
                return applied != nullptr &&
                       applied->trade_id == value.trade_id &&
                       value.superseded_version == applied->expected_version &&
                       value.replacement_version ==
                           applied->expected_version + 1U &&
                       superseded != nullptr &&
                       superseded->state() == domain::TradeState::Superseded &&
                       replacement != nullptr &&
                       replacement->state() == domain::TradeState::Confirmed;
            } else if constexpr (std::is_same_v<Value, TradeCancelledResult>) {
                const auto* applied = std::get_if<TradeCancelledEvent>(&event);
                const auto* trade =
                    after.find_trade(value.trade_id, value.version);
                return applied != nullptr &&
                       applied->trade_id == value.trade_id &&
                       value.version == applied->expected_version &&
                       trade != nullptr &&
                       trade->state() == domain::TradeState::Cancelled;
            } else if constexpr (std::is_same_v<Value, EodRunResult>) {
                const auto* applied = std::get_if<EodRunEvent>(&event);
                return applied != nullptr &&
                       applied->as_of_date == value.as_of_date &&
                       static_cast<std::uint64_t>(
                           value.settled_trade_count) ==
                           settled_trade_count(after) -
                               settled_trade_count(before);
            } else {
                static_assert(always_false<Value>);
            }
        },
        result);
}

}  // namespace

domain::Outcome<domain::State, ReplayError>
replay(domain::LimitHierarchy initial_limits,
       const std::span<const CommandBatch> batches) {
    domain::State state(std::move(initial_limits));
    std::uint64_t expected_sequence = 1U;

    for (const CommandBatch& batch : batches) {
        if (batch.sequence() != expected_sequence) {
            return domain::Outcome<domain::State, ReplayError>::failure(
                replay_error(ReplayErrorCode::SequenceMismatch,
                             batch.sequence(),
                             expected_sequence,
                             0U));
        }
        if (batch.events().size() != 1U) {
            return domain::Outcome<domain::State, ReplayError>::failure(
                replay_error(ReplayErrorCode::InvalidBatchShape,
                             batch.sequence(),
                             expected_sequence,
                             batch.events().size()));
        }

        const domain::State before = state;
        auto applied = apply_event(state, batch.events().front());
        if (!applied) {
            auto failure = replay_error(ReplayErrorCode::EventApplicationFailed,
                                        batch.sequence(),
                                        expected_sequence,
                                        0U);
            failure.state_error = applied.error();
            return domain::Outcome<domain::State, ReplayError>::failure(
                std::move(failure));
        }
        state = std::move(applied).value();

        if (!result_matches(
                batch.result(), batch.events().front(), before, state)) {
            return domain::Outcome<domain::State, ReplayError>::failure(
                replay_error(ReplayErrorCode::ResultMismatch,
                             batch.sequence(),
                             expected_sequence,
                             0U));
        }
        ++expected_sequence;
    }

    const auto recomputed =
        domain::recompute_ledger_totals(state.ledger_entries());
    if (!recomputed) {
        auto failure =
            replay_error(ReplayErrorCode::LedgerTotalsMismatch,
                         batches.empty() ? 0U : batches.back().sequence(),
                         expected_sequence,
                         0U);
        failure.ledger_totals_error = recomputed.error();
        return domain::Outcome<domain::State, ReplayError>::failure(
            std::move(failure));
    }
    if (recomputed.value() != state.ledger_totals()) {
        return domain::Outcome<domain::State, ReplayError>::failure(
            replay_error(ReplayErrorCode::LedgerTotalsMismatch,
                         batches.empty() ? 0U : batches.back().sequence(),
                         expected_sequence,
                         0U));
    }

    return domain::Outcome<domain::State, ReplayError>::success(
        std::move(state));
}

}  // namespace backbook::journal
