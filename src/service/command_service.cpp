#include "backbook/service/command_service.hpp"

#include "backbook/service/command_evaluator.hpp"

#include <limits>
#include <utility>
#include <vector>

namespace backbook::service {
namespace {

[[nodiscard]] CommandServiceError
service_error(const CommandServiceErrorCode code) {
    return CommandServiceError(code);
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
    auto evaluated = evaluate_command(*current, envelope.command);
    if (!evaluated) {
        if (evaluated.error().code ==
            CommandEvaluationErrorCode::DomainRejected) {
            auto failure =
                service_error(CommandServiceErrorCode::DomainRejected);
            failure.state_error = evaluated.error().state_error;
            return domain::Outcome<
                CommandReceipt,
                CommandServiceError>::failure(std::move(failure));
        }
        return domain::Outcome<CommandReceipt, CommandServiceError>::failure(
            service_error(CommandServiceErrorCode::InvariantViolation));
    }
    auto evaluation = std::move(evaluated).value();

    auto batch = journal::CommandBatch::create(
        next_sequence_,
        envelope.command_id,
        canonical.value(),
        std::vector<journal::Event>{evaluation.event},
        evaluation.result);
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
        std::make_shared<const domain::State>(
            std::move(evaluation.prospective_state));
    CommandReceipt receipt{evaluation.result, false};

    std::map<domain::CommandId, IdempotencyRecord> pending;
    pending.emplace(envelope.command_id,
                    IdempotencyRecord{canonical.value(), evaluation.result});
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
