#pragma once

#include "backbook/domain/date.hpp"
#include "backbook/domain/fx_terms.hpp"
#include "backbook/domain/id.hpp"
#include "backbook/domain/outcome.hpp"
#include "backbook/domain/posting_policy.hpp"
#include "backbook/journal/command_batch.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <variant>

namespace backbook::service {

inline constexpr std::uint8_t command_request_format_version = 1U;

struct BookTradeCommand final {
    domain::TradeId trade_id;
    domain::BookId book_id;
    domain::CounterpartyId counterparty_id;
    domain::NettingSetId netting_set_id;
    domain::FxTerms terms;

    [[nodiscard]] friend bool operator==(const BookTradeCommand&,
                                         const BookTradeCommand&) = default;
};

struct ConfirmTradeCommand final {
    domain::TradeId trade_id;
    std::uint32_t expected_version;
    domain::ConfirmationPostingIds posting_ids;

    [[nodiscard]] friend bool operator==(const ConfirmTradeCommand&,
                                         const ConfirmTradeCommand&) = default;
};

struct AmendTradeCommand final {
    domain::TradeId trade_id;
    std::uint32_t expected_version;
    domain::FxTerms replacement_terms;
    domain::ReversalPostingIds reversal_ids;
    domain::ConfirmationPostingIds replacement_posting_ids;

    [[nodiscard]] friend bool operator==(const AmendTradeCommand&,
                                         const AmendTradeCommand&) = default;
};

struct CancelTradeCommand final {
    domain::TradeId trade_id;
    std::uint32_t expected_version;
    std::optional<domain::ReversalPostingIds> reversal_ids;

    [[nodiscard]] friend bool operator==(const CancelTradeCommand&,
                                         const CancelTradeCommand&) = default;
};

struct RunEodCommand final {
    domain::IsoDate as_of_date;

    [[nodiscard]] friend bool operator==(const RunEodCommand&,
                                         const RunEodCommand&) = default;
};

using Command = std::variant<BookTradeCommand,
                             ConfirmTradeCommand,
                             AmendTradeCommand,
                             CancelTradeCommand,
                             RunEodCommand>;

// The command ID covers the complete request. Reusing it with different bytes
// is an idempotency conflict.
struct CommandEnvelope final {
    domain::CommandId command_id;
    Command command;

    [[nodiscard]] friend bool operator==(const CommandEnvelope&,
                                         const CommandEnvelope&) = default;
};

enum class CommandEncodingError : std::uint8_t {
    SizeOverflow,
};

enum class CanonicalCommandRequestError : std::uint8_t {
    Empty,
    UnsupportedVersion,
    Malformed,
    CommandIdMismatch,
    CommandEventMismatch,
};

[[nodiscard]] domain::Outcome<journal::Bytes, CommandEncodingError>
canonical_command_bytes(const CommandEnvelope& envelope);

// Recovery validates stored request bytes before rebuilding the idempotency
// index, preventing unsupported or malformed requests from becoming trusted.
[[nodiscard]] domain::Outcome<std::uint8_t, CanonicalCommandRequestError>
validate_canonical_command_request(
    std::span<const std::uint8_t> bytes,
    const domain::CommandId& expected_command_id);

}  // namespace backbook::service
