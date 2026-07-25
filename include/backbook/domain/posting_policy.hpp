#pragma once

#include "backbook/domain/id.hpp"
#include "backbook/domain/ledger.hpp"
#include "backbook/domain/outcome.hpp"
#include "backbook/domain/trade.hpp"

#include <cstdint>

namespace backbook::domain {

struct ConfirmationPostingIds final {
    PostingId pay_control_debit;
    PostingId pay_payable_credit;
    PostingId receive_receivable_debit;
    PostingId receive_control_credit;

    [[nodiscard]] friend bool operator==(
        const ConfirmationPostingIds&,
        const ConfirmationPostingIds&) = default;
};

struct ReversalPostingIds final {
    PostingId pay_control_credit;
    PostingId pay_payable_debit;
    PostingId receive_receivable_credit;
    PostingId receive_control_debit;

    [[nodiscard]] friend bool operator==(
        const ReversalPostingIds&,
        const ReversalPostingIds&) = default;
};

enum class PostingPolicyError : std::uint8_t {
    TradeNotConfirmed,
    InvalidOriginalShape,
    PostingNonPositiveAmount,
    PostingInvalidTradeVersion,
    PostingEmptyAccount,
    PostingInvalidSide,
    PostingUnsupportedCurrency,
    PostingSelfReversal,
    LedgerEmptyPostings,
    LedgerDuplicatePostingId,
    LedgerInvalidPostingSide,
    LedgerUnsupportedCurrency,
    LedgerBalanceOverflow,
    LedgerUnbalanced,
};

[[nodiscard]] Outcome<LedgerEntry, PostingPolicyError>
build_confirmation_entry(
    const Trade& confirmed,
    const ConfirmationPostingIds& posting_ids);

[[nodiscard]] Outcome<LedgerEntry, PostingPolicyError> build_reversal_entry(
    const LedgerEntry& original,
    const ReversalPostingIds& reversal_ids);

}  // namespace backbook::domain
