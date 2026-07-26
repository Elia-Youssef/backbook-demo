#pragma once

#include "backbook/domain/limits.hpp"
#include "backbook/domain/outcome.hpp"
#include "backbook/domain/state.hpp"
#include "backbook/journal/codec.hpp"
#include "backbook/journal/replay.hpp"
#include "backbook/service/command.hpp"
#include "backbook/storage/journal_store.hpp"

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>

namespace backbook::service {

enum class CommandServiceErrorCode : std::uint8_t {
    MissingStore,
    RecoveryFailure,
    ReplayFailure,
    DuplicateJournalCommandId,
    SequenceExhausted,
    CommandEncodingFailure,
    InvalidCanonicalRequest,
    IdempotencyConflict,
    DomainRejected,
    JournalEncodingFailure,
    JournalUnavailable,
    InvariantViolation,
};

struct CommandServiceError final {
    explicit CommandServiceError(CommandServiceErrorCode error_code)
        : code(error_code) {}

    CommandServiceErrorCode code;
    std::optional<storage::JournalRecoveryError> recovery_error{};
    std::optional<journal::ReplayError> replay_error{};
    std::optional<CommandEncodingError> command_encoding_error{};
    std::optional<CanonicalCommandRequestError> canonical_request_error{};
    std::optional<domain::StateError> state_error{};
    std::optional<journal::CodecError> codec_error{};
    std::optional<storage::JournalStoreError> store_error{};
};

struct CommandReceipt final {
    journal::CommandResult result;
    bool idempotent_replay;

    [[nodiscard]] friend bool operator==(const CommandReceipt&,
                                         const CommandReceipt&) = default;
};

class CommandService final {
public:
    [[nodiscard]] static domain::Outcome<std::unique_ptr<CommandService>,
                                         CommandServiceError>
    create(std::unique_ptr<storage::JournalStore> store,
           domain::LimitHierarchy initial_limits);

    [[nodiscard]] domain::Outcome<CommandReceipt, CommandServiceError>
    execute(const CommandEnvelope& envelope);

    [[nodiscard]] std::shared_ptr<const domain::State>
    snapshot() const noexcept;

    [[nodiscard]] bool recovered_torn_tail() const noexcept {
        return recovered_torn_tail_;
    }

private:
    struct IdempotencyRecord final {
        journal::Bytes canonical_request;
        journal::CommandResult result;
    };

    CommandService(std::unique_ptr<storage::JournalStore> store,
                   std::shared_ptr<const domain::State> initial_snapshot,
                   std::map<domain::CommandId, IdempotencyRecord> idempotency,
                   std::uint64_t next_sequence,
                   bool recovered_torn_tail);

    std::unique_ptr<storage::JournalStore> store_;
    mutable std::mutex command_mutex_;
    std::atomic<std::shared_ptr<const domain::State>> snapshot_;
    std::map<domain::CommandId, IdempotencyRecord> idempotency_;
    std::uint64_t next_sequence_;
    bool recovered_torn_tail_;
};

}  // namespace backbook::service
