#pragma once

#include "backbook/domain/outcome.hpp"
#include "backbook/domain/state.hpp"
#include "backbook/journal/command_batch.hpp"
#include "backbook/service/command.hpp"

#include <cstdint>
#include <optional>

namespace backbook::service {

enum class CommandEvaluationErrorCode : std::uint8_t {
    DomainRejected,
    InvariantViolation,
};

struct CommandEvaluationError final {
    CommandEvaluationErrorCode code;
    std::optional<domain::StateError> state_error{};
};

struct CommandEvaluation final {
    domain::State prospective_state;
    journal::Event event;
    journal::CommandResult result;
};

// Evaluation is pure: it computes the next state and durable record without
// touching storage or publishing a snapshot.
[[nodiscard]] domain::Outcome<CommandEvaluation, CommandEvaluationError>
evaluate_command(const domain::State& current, const Command& command);

} // namespace backbook::service
