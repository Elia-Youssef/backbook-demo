#pragma once

#include "backbook/domain/date.hpp"
#include "backbook/domain/fx_terms.hpp"
#include "backbook/domain/id.hpp"
#include "backbook/domain/outcome.hpp"
#include "backbook/domain/posting_policy.hpp"

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace backbook::journal {

using Bytes = std::vector<std::uint8_t>;

struct TradeBookedEvent final {
    domain::TradeId trade_id;
    domain::BookId book_id;
    domain::CounterpartyId counterparty_id;
    domain::NettingSetId netting_set_id;
    domain::FxTerms terms;

    [[nodiscard]] friend bool operator==(const TradeBookedEvent&,
                                         const TradeBookedEvent&) = default;
};

struct TradeConfirmedEvent final {
    domain::TradeId trade_id;
    std::uint32_t expected_version;
    domain::ConfirmationPostingIds posting_ids;

    [[nodiscard]] friend bool operator==(const TradeConfirmedEvent&,
                                         const TradeConfirmedEvent&) = default;
};

struct TradeAmendedEvent final {
    domain::TradeId trade_id;
    std::uint32_t expected_version;
    domain::FxTerms replacement_terms;
    domain::ReversalPostingIds reversal_ids;
    domain::ConfirmationPostingIds replacement_posting_ids;

    [[nodiscard]] friend bool operator==(const TradeAmendedEvent&,
                                         const TradeAmendedEvent&) = default;
};

struct TradeCancelledEvent final {
    domain::TradeId trade_id;
    std::uint32_t expected_version;
    std::optional<domain::ReversalPostingIds> reversal_ids;

    [[nodiscard]] friend bool operator==(const TradeCancelledEvent&,
                                         const TradeCancelledEvent&) = default;
};

struct EodRunEvent final {
    domain::IsoDate as_of_date;

    [[nodiscard]] friend bool operator==(const EodRunEvent&,
                                         const EodRunEvent&) = default;
};

using Event = std::variant<TradeBookedEvent,
                           TradeConfirmedEvent,
                           TradeAmendedEvent,
                           TradeCancelledEvent,
                           EodRunEvent>;

struct TradeBookedResult final {
    domain::TradeId trade_id;
    std::uint32_t version;
    std::uint64_t state_version;

    [[nodiscard]] friend bool operator==(const TradeBookedResult&,
                                         const TradeBookedResult&) = default;
};

struct TradeConfirmedResult final {
    domain::TradeId trade_id;
    std::uint32_t version;
    std::uint64_t state_version;

    [[nodiscard]] friend bool operator==(const TradeConfirmedResult&,
                                         const TradeConfirmedResult&) = default;
};

struct TradeAmendedResult final {
    domain::TradeId trade_id;
    std::uint32_t superseded_version;
    std::uint32_t replacement_version;
    std::uint64_t state_version;

    [[nodiscard]] friend bool operator==(const TradeAmendedResult&,
                                         const TradeAmendedResult&) = default;
};

struct TradeCancelledResult final {
    domain::TradeId trade_id;
    std::uint32_t version;
    std::uint64_t state_version;

    [[nodiscard]] friend bool operator==(const TradeCancelledResult&,
                                         const TradeCancelledResult&) = default;
};

struct EodRunResult final {
    domain::IsoDate as_of_date;
    std::uint32_t settled_trade_count;
    std::uint64_t state_version;

    [[nodiscard]] friend bool operator==(const EodRunResult&,
                                         const EodRunResult&) = default;
};

using CommandResult = std::variant<TradeBookedResult,
                                   TradeConfirmedResult,
                                   TradeAmendedResult,
                                   TradeCancelledResult,
                                   EodRunResult>;

enum class CommandBatchError : std::uint8_t {
    ZeroSequence,
    ResultStateVersionExceedsSequence,
};

// One accepted command becomes one batch, which keeps its request, event, and
// logical response together for replay and idempotency.
class CommandBatch final {
public:
    [[nodiscard]] static domain::Outcome<CommandBatch, CommandBatchError>
    create(std::uint64_t sequence,
           domain::CommandId command_id,
           Bytes canonical_request,
           std::vector<Event> events,
           CommandResult result);

    [[nodiscard]] std::uint64_t sequence() const noexcept {
        return sequence_;
    }

    [[nodiscard]] const domain::CommandId& command_id() const noexcept {
        return command_id_;
    }

    [[nodiscard]] const Bytes& canonical_request() const noexcept {
        return canonical_request_;
    }

    [[nodiscard]] const std::vector<Event>& events() const noexcept {
        return events_;
    }

    [[nodiscard]] const CommandResult& result() const noexcept {
        return result_;
    }

    [[nodiscard]] friend bool operator==(const CommandBatch&,
                                         const CommandBatch&) = default;

private:
    CommandBatch(std::uint64_t sequence,
                 domain::CommandId command_id,
                 Bytes canonical_request,
                 std::vector<Event> events,
                 CommandResult result);

    std::uint64_t sequence_;
    domain::CommandId command_id_;
    Bytes canonical_request_;
    std::vector<Event> events_;
    CommandResult result_;
};

[[nodiscard]] std::uint64_t
result_state_version(const CommandResult& result) noexcept;

}  // namespace backbook::journal
