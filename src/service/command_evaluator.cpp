#include "backbook/service/command_evaluator.hpp"

#include "backbook/domain/ledger_totals.hpp"

#include <limits>
#include <type_traits>
#include <utility>
#include <variant>

namespace backbook::service {
namespace {

template <typename> inline constexpr bool always_false = false;

using EvaluationOutcome =
    domain::Outcome<CommandEvaluation, CommandEvaluationError>;
using ResultOutcome =
    domain::Outcome<journal::CommandResult, CommandEvaluationError>;

[[nodiscard]] CommandEvaluationError
evaluation_error(const CommandEvaluationErrorCode code) {
    return CommandEvaluationError{code};
}

[[nodiscard]] CommandEvaluationError
domain_rejection(domain::StateError state_error) {
    auto failure = evaluation_error(CommandEvaluationErrorCode::DomainRejected);
    failure.state_error = std::move(state_error);
    return failure;
}

[[nodiscard]] bool invariants_hold(const domain::State& state) {
    const auto recomputed =
        domain::recompute_ledger_totals(state.ledger_entries());
    if (!recomputed || recomputed.value() != state.ledger_totals()) {
        return false;
    }
    for (const auto& balance : state.limits().snapshots()) {
        if (balance.capacity_minor_units < 0 ||
            balance.reserved_minor_units < 0 ||
            balance.reserved_minor_units > balance.capacity_minor_units) {
            return false;
        }
    }
    return true;
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

template <typename Result>
[[nodiscard]] ResultOutcome successful_result(Result result) {
    return ResultOutcome::success(journal::CommandResult{std::move(result)});
}

[[nodiscard]] ResultOutcome invariant_failure() {
    return ResultOutcome::failure(
        evaluation_error(CommandEvaluationErrorCode::InvariantViolation));
}

template <typename ResultFactory>
[[nodiscard]] EvaluationOutcome complete_transition(
    domain::Outcome<domain::State, domain::StateError> prospective,
    journal::Event event, ResultFactory&& result_factory) {
    if (!prospective) {
        return EvaluationOutcome::failure(
            domain_rejection(std::move(prospective).error()));
    }
    if (!invariants_hold(prospective.value())) {
        return EvaluationOutcome::failure(
            evaluation_error(CommandEvaluationErrorCode::InvariantViolation));
    }

    auto result = result_factory(prospective.value());
    if (!result) {
        return EvaluationOutcome::failure(std::move(result).error());
    }

    return EvaluationOutcome::success(
        CommandEvaluation{std::move(prospective).value(), std::move(event),
                          std::move(result).value()});
}

} // namespace

domain::Outcome<CommandEvaluation, CommandEvaluationError>
evaluate_command(const domain::State& current, const Command& command) {
    return std::visit(
        [&current](const auto& value) -> EvaluationOutcome {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, BookTradeCommand>) {
                auto prospective = domain::book_trade(
                    current, value.trade_id, value.book_id,
                    value.counterparty_id, value.netting_set_id, value.terms);
                return complete_transition(
                    std::move(prospective),
                    journal::Event{journal::TradeBookedEvent{
                        value.trade_id, value.book_id, value.counterparty_id,
                        value.netting_set_id, value.terms}},
                    [&value](const domain::State& after) {
                        return successful_result(journal::TradeBookedResult{
                            value.trade_id, 1U, after.version()});
                    });
            } else if constexpr (std::is_same_v<Value, ConfirmTradeCommand>) {
                auto prospective = domain::confirm_trade(
                    current, value.trade_id, value.expected_version,
                    value.posting_ids);
                return complete_transition(
                    std::move(prospective),
                    journal::Event{journal::TradeConfirmedEvent{
                        value.trade_id, value.expected_version,
                        value.posting_ids}},
                    [&value](const domain::State& after) {
                        return successful_result(journal::TradeConfirmedResult{
                            value.trade_id, value.expected_version,
                            after.version()});
                    });
            } else if constexpr (std::is_same_v<Value, AmendTradeCommand>) {
                auto prospective = domain::amend_trade(
                    current, value.trade_id, value.expected_version,
                    value.replacement_terms, value.reversal_ids,
                    value.replacement_posting_ids);
                return complete_transition(
                    std::move(prospective),
                    journal::Event{journal::TradeAmendedEvent{
                        value.trade_id, value.expected_version,
                        value.replacement_terms, value.reversal_ids,
                        value.replacement_posting_ids}},
                    [&value](const domain::State& after) {
                        if (value.expected_version ==
                            std::numeric_limits<std::uint32_t>::max()) {
                            return invariant_failure();
                        }
                        return successful_result(journal::TradeAmendedResult{
                            value.trade_id, value.expected_version,
                            value.expected_version + 1U, after.version()});
                    });
            } else if constexpr (std::is_same_v<Value, CancelTradeCommand>) {
                auto prospective = domain::cancel_trade(current, value.trade_id,
                                                        value.expected_version,
                                                        value.reversal_ids);
                return complete_transition(
                    std::move(prospective),
                    journal::Event{journal::TradeCancelledEvent{
                        value.trade_id, value.expected_version,
                        value.reversal_ids}},
                    [&value](const domain::State& after) {
                        return successful_result(journal::TradeCancelledResult{
                            value.trade_id, value.expected_version,
                            after.version()});
                    });
            } else if constexpr (std::is_same_v<Value, RunEodCommand>) {
                auto prospective = domain::run_eod(current, value.as_of_date);
                return complete_transition(
                    std::move(prospective),
                    journal::Event{journal::EodRunEvent{value.as_of_date}},
                    [&current, &value](const domain::State& after) {
                        const auto before_count = settled_trade_count(current);
                        const auto after_count = settled_trade_count(after);
                        if (after_count < before_count ||
                            after_count - before_count >
                                std::numeric_limits<std::uint32_t>::max()) {
                            return invariant_failure();
                        }
                        return successful_result(journal::EodRunResult{
                            value.as_of_date,
                            static_cast<std::uint32_t>(after_count -
                                                       before_count),
                            after.version()});
                    });
            } else {
                static_assert(always_false<Value>);
            }
        },
        command);
}

} // namespace backbook::service
