#include "backbook/journal/command_batch.hpp"

#include <utility>

namespace backbook::journal {

CommandBatch::CommandBatch(const std::uint64_t sequence,
                           domain::CommandId command_id,
                           Bytes canonical_request,
                           std::vector<Event> events,
                           CommandResult result)
    : sequence_(sequence), command_id_(std::move(command_id)),
      canonical_request_(std::move(canonical_request)),
      events_(std::move(events)), result_(std::move(result)) {}

domain::Outcome<CommandBatch, CommandBatchError>
CommandBatch::create(const std::uint64_t sequence,
                     domain::CommandId command_id,
                     Bytes canonical_request,
                     std::vector<Event> events,
                     CommandResult result) {
    if (sequence == 0U) {
        return domain::Outcome<CommandBatch, CommandBatchError>::failure(
            CommandBatchError::ZeroSequence);
    }
    if (result_state_version(result) > sequence) {
        return domain::Outcome<CommandBatch, CommandBatchError>::failure(
            CommandBatchError::ResultStateVersionExceedsSequence);
    }
    return domain::Outcome<CommandBatch, CommandBatchError>::success(
        CommandBatch(sequence,
                     std::move(command_id),
                     std::move(canonical_request),
                     std::move(events),
                     std::move(result)));
}

std::uint64_t result_state_version(const CommandResult& result) noexcept {
    return std::visit(
        [](const auto& value) noexcept { return value.state_version; }, result);
}

}  // namespace backbook::journal
