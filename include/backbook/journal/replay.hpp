#pragma once

#include "backbook/domain/limits.hpp"
#include "backbook/domain/outcome.hpp"
#include "backbook/domain/state.hpp"
#include "backbook/journal/command_batch.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace backbook::journal {

enum class ReplayErrorCode : std::uint8_t {
    SequenceMismatch,
    InvalidBatchShape,
    EventApplicationFailed,
    ResultMismatch,
    LedgerTotalsMismatch,
};

struct ReplayError final {
    ReplayErrorCode code;
    std::uint64_t batch_sequence;
    std::uint64_t expected_sequence;
    std::size_t event_index;
    std::optional<domain::StateError> state_error{};
    std::optional<domain::LedgerTotalsError> ledger_totals_error{};
};

[[nodiscard]] domain::Outcome<domain::State, ReplayError>
replay(domain::LimitHierarchy initial_limits,
       std::span<const CommandBatch> batches);

}  // namespace backbook::journal
