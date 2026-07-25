#include "backbook/service/command_service.hpp"

#include "backbook/domain/ledger_totals.hpp"

#include <limits>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace backbook::service {
namespace {

template <typename> inline constexpr bool always_false = false;

[[nodiscard]] CommandServiceError
service_error(const CommandServiceErrorCode code) {
    return CommandServiceError(code);
}

[[nodiscard]] journal::Event make_event(const Command& command) {
    return std::visit(
        [](const auto& value) -> journal::Event {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, BookTradeCommand>) {
                return journal::TradeBookedEvent{value.trade_id,
                                                 value.book_id,
                                                 value.counterparty_id,
                                                 value.netting_set_id,
                                                 value.terms};
            } else if constexpr (std::is_same_v<Value, ConfirmTradeCommand>) {
                return journal::TradeConfirmedEvent{
                    value.trade_id, value.expected_version, value.posting_ids};
            } else if constexpr (std::is_same_v<Value, AmendTradeCommand>) {
                return journal::TradeAmendedEvent{
                    value.trade_id,
                    value.expected_version,
                    value.replacement_terms,
                    value.reversal_ids,
                    value.replacement_posting_ids};
            } else if constexpr (std::is_same_v<Value, CancelTradeCommand>) {
                return journal::TradeCancelledEvent{
                    value.trade_id, value.expected_version, value.reversal_ids};
            } else if constexpr (std::is_same_v<Value, RunEodCommand>) {
                return journal::EodRunEvent{value.as_of_date};
            } else {
                static_assert(always_false<Value>);
            }
        },
        command);
}

[[nodiscard]] domain::Outcome<domain::State, domain::StateError>
apply_command(const domain::State& state, const Command& command) {
    return std::visit(
        [&state](const auto& value)
            -> domain::Outcome<domain::State, domain::StateError> {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, BookTradeCommand>) {
                return domain::book_trade(state,
                                          value.trade_id,
                                          value.book_id,
                                          value.counterparty_id,
                                          value.netting_set_id,
                                          value.terms);
            } else if constexpr (std::is_same_v<Value, ConfirmTradeCommand>) {
                return domain::confirm_trade(state,
                                             value.trade_id,
                                             value.expected_version,
                                             value.posting_ids);
            } else if constexpr (std::is_same_v<Value, AmendTradeCommand>) {
                return domain::amend_trade(state,
                                           value.trade_id,
                                           value.expected_version,
                                           value.replacement_terms,
                                           value.reversal_ids,
                                           value.replacement_posting_ids);
            } else if constexpr (std::is_same_v<Value, CancelTradeCommand>) {
                return domain::cancel_trade(state,
                                            value.trade_id,
                                            value.expected_version,
                                            value.reversal_ids);
            } else if constexpr (std::is_same_v<Value, RunEodCommand>) {
                return domain::run_eod(state, value.as_of_date);
            } else {
                static_assert(always_false<Value>);
            }
        },
        command);
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

[[nodiscard]] domain::Outcome<journal::CommandResult, CommandServiceError>
make_result(const Command& command,
            const domain::State& before,
            const domain::State& after) {
    return std::visit(
        [&before, &after](const auto& value)
            -> domain::Outcome<journal::CommandResult, CommandServiceError> {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, BookTradeCommand>) {
                return domain::Outcome<
                    journal::CommandResult,
                    CommandServiceError>::success(journal::TradeBookedResult{
                    value.trade_id, 1U, after.version()});
            } else if constexpr (std::is_same_v<Value, ConfirmTradeCommand>) {
                return domain::Outcome<
                    journal::CommandResult,
                    CommandServiceError>::success(journal::TradeConfirmedResult{
                    value.trade_id, value.expected_version, after.version()});
            } else if constexpr (std::is_same_v<Value, AmendTradeCommand>) {
                if (value.expected_version ==
                    std::numeric_limits<std::uint32_t>::max()) {
                    return domain::Outcome<journal::CommandResult,
                                           CommandServiceError>::
                        failure(service_error(
                            CommandServiceErrorCode::InvariantViolation));
                }
                return domain::Outcome<
                    journal::CommandResult,
                    CommandServiceError>::success(journal::TradeAmendedResult{
                    value.trade_id,
                    value.expected_version,
                    value.expected_version + 1U,
                    after.version()});
            } else if constexpr (std::is_same_v<Value, CancelTradeCommand>) {
                return domain::Outcome<
                    journal::CommandResult,
                    CommandServiceError>::success(journal::TradeCancelledResult{
                    value.trade_id, value.expected_version, after.version()});
            } else if constexpr (std::is_same_v<Value, RunEodCommand>) {
                const auto before_count = settled_trade_count(before);
                const auto after_count = settled_trade_count(after);
                if (after_count < before_count ||
                    after_count - before_count >
                        std::numeric_limits<std::uint32_t>::max()) {
                    return domain::Outcome<journal::CommandResult,
                                           CommandServiceError>::
                        failure(service_error(
                            CommandServiceErrorCode::InvariantViolation));
                }
                return domain::Outcome<
                    journal::CommandResult,
                    CommandServiceError>::success(journal::EodRunResult{
                    value.as_of_date,
                    static_cast<std::uint32_t>(after_count - before_count),
                    after.version()});
            } else {
                static_assert(always_false<Value>);
            }
        },
        command);
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

}  // namespace

CommandService::CommandService(
    std::unique_ptr<storage::JournalStore> store,
    std::shared_ptr<const domain::State> initial_snapshot,
    std::map<domain::CommandId, IdempotencyRecord> idempotency,
    const std::uint64_t next_sequence,
    const bool recovered_torn_tail)
    : store_(std::move(store)), snapshot_(std::move(initial_snapshot)),
      idempotency_(std::move(idempotency)), next_sequence_(next_sequence),
      recovered_torn_tail_(recovered_torn_tail) {}

domain::Outcome<std::unique_ptr<CommandService>, CommandServiceError>
CommandService::create(std::unique_ptr<storage::JournalStore> store,
                       domain::LimitHierarchy initial_limits) {
    if (!store) {
        return domain::Outcome<std::unique_ptr<CommandService>,
                               CommandServiceError>::
            failure(service_error(CommandServiceErrorCode::MissingStore));
    }

    auto recovered = storage::recover_journal(*store);
    if (!recovered) {
        auto failure = service_error(CommandServiceErrorCode::RecoveryFailure);
        failure.recovery_error = recovered.error();
        return domain::Outcome<
            std::unique_ptr<CommandService>,
            CommandServiceError>::failure(std::move(failure));
    }

    auto replayed =
        journal::replay(std::move(initial_limits), recovered.value().batches);
    if (!replayed) {
        auto failure = service_error(CommandServiceErrorCode::ReplayFailure);
        failure.replay_error = replayed.error();
        return domain::Outcome<
            std::unique_ptr<CommandService>,
            CommandServiceError>::failure(std::move(failure));
    }

    std::map<domain::CommandId, IdempotencyRecord> idempotency;
    for (const auto& batch : recovered.value().batches) {
        const auto [unused, inserted] = idempotency.emplace(
            batch.command_id(),
            IdempotencyRecord{batch.canonical_request(), batch.result()});
        static_cast<void>(unused);
        if (!inserted) {
            return domain::Outcome<std::unique_ptr<CommandService>,
                                   CommandServiceError>::
                failure(service_error(
                    CommandServiceErrorCode::DuplicateJournalCommandId));
        }
    }

    std::uint64_t next_sequence = 1U;
    if (!recovered.value().batches.empty()) {
        const auto last_sequence = recovered.value().batches.back().sequence();
        next_sequence =
            last_sequence == std::numeric_limits<std::uint64_t>::max()
                ? 0U
                : last_sequence + 1U;
    }

    auto initial_snapshot =
        std::make_shared<const domain::State>(std::move(replayed).value());
    return domain::
        Outcome<std::unique_ptr<CommandService>, CommandServiceError>::success(
            std::unique_ptr<CommandService>(
                new CommandService(std::move(store),
                                   std::move(initial_snapshot),
                                   std::move(idempotency),
                                   next_sequence,
                                   recovered.value().truncated_tail)));
}

domain::Outcome<CommandReceipt, CommandServiceError>
CommandService::execute(const CommandEnvelope& envelope) {
    const std::scoped_lock lock(command_mutex_);

    auto canonical = canonical_command_bytes(envelope);
    if (!canonical) {
        auto failure =
            service_error(CommandServiceErrorCode::CommandEncodingFailure);
        failure.command_encoding_error = canonical.error();
        return domain::Outcome<CommandReceipt, CommandServiceError>::failure(
            std::move(failure));
    }

    const auto existing = idempotency_.find(envelope.command_id);
    if (existing != idempotency_.end()) {
        if (existing->second.canonical_request == canonical.value()) {
            return domain::Outcome<CommandReceipt,
                                   CommandServiceError>::success(CommandReceipt{
                existing->second.result, true});
        }
        return domain::Outcome<CommandReceipt, CommandServiceError>::failure(
            service_error(CommandServiceErrorCode::IdempotencyConflict));
    }

    if (!store_->writable()) {
        return domain::Outcome<CommandReceipt, CommandServiceError>::failure(
            service_error(CommandServiceErrorCode::JournalUnavailable));
    }
    if (next_sequence_ == 0U) {
        return domain::Outcome<CommandReceipt, CommandServiceError>::failure(
            service_error(CommandServiceErrorCode::SequenceExhausted));
    }

    const auto current = snapshot_.load(std::memory_order_acquire);
    auto prospective = apply_command(*current, envelope.command);
    if (!prospective) {
        auto failure = service_error(CommandServiceErrorCode::DomainRejected);
        failure.state_error = prospective.error();
        return domain::Outcome<CommandReceipt, CommandServiceError>::failure(
            std::move(failure));
    }
    if (!invariants_hold(prospective.value())) {
        return domain::Outcome<CommandReceipt, CommandServiceError>::failure(
            service_error(CommandServiceErrorCode::InvariantViolation));
    }

    auto result = make_result(envelope.command, *current, prospective.value());
    if (!result) {
        return domain::Outcome<CommandReceipt, CommandServiceError>::failure(
            result.error());
    }

    auto batch = journal::CommandBatch::create(
        next_sequence_,
        envelope.command_id,
        canonical.value(),
        std::vector<journal::Event>{make_event(envelope.command)},
        result.value());
    if (!batch) {
        return domain::Outcome<CommandReceipt, CommandServiceError>::failure(
            service_error(CommandServiceErrorCode::InvariantViolation));
    }

    auto frame = journal::encode_frame(batch.value());
    if (!frame) {
        auto failure =
            service_error(CommandServiceErrorCode::JournalEncodingFailure);
        failure.codec_error = frame.error();
        return domain::Outcome<CommandReceipt, CommandServiceError>::failure(
            std::move(failure));
    }

    auto published_snapshot =
        std::make_shared<const domain::State>(std::move(prospective).value());
    CommandReceipt receipt{result.value(), false};

    std::map<domain::CommandId, IdempotencyRecord> pending;
    pending.emplace(envelope.command_id,
                    IdempotencyRecord{canonical.value(), result.value()});
    auto pending_record = pending.extract(pending.begin());

    const auto following_sequence =
        next_sequence_ == std::numeric_limits<std::uint64_t>::max()
            ? 0U
            : next_sequence_ + 1U;

    auto appended = store_->append_and_flush(frame.value());
    if (!appended) {
        auto failure =
            service_error(CommandServiceErrorCode::JournalUnavailable);
        failure.store_error = appended.error();
        return domain::Outcome<CommandReceipt, CommandServiceError>::failure(
            std::move(failure));
    }

    idempotency_.insert(std::move(pending_record));
    next_sequence_ = following_sequence;
    snapshot_.store(std::move(published_snapshot), std::memory_order_release);
    return domain::Outcome<CommandReceipt, CommandServiceError>::success(
        std::move(receipt));
}

std::shared_ptr<const domain::State> CommandService::snapshot() const noexcept {
    return snapshot_.load(std::memory_order_acquire);
}

}  // namespace backbook::service
