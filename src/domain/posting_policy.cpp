#include "backbook/domain/posting_policy.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace backbook::domain {
namespace {

constexpr std::string_view counterparty_control_prefix =
    "COUNTERPARTY_CONTROL:";
constexpr std::string_view settlement_payable_prefix =
    "SETTLEMENT_PAYABLE:";
constexpr std::string_view settlement_receivable_prefix =
    "SETTLEMENT_RECEIVABLE:";

[[nodiscard]] PostingPolicyError map_posting_error(
    const PostingError error) noexcept {
    switch (error) {
    case PostingError::NonPositiveAmount:
        return PostingPolicyError::PostingNonPositiveAmount;
    case PostingError::InvalidTradeVersion:
        return PostingPolicyError::PostingInvalidTradeVersion;
    case PostingError::EmptyAccount:
        return PostingPolicyError::PostingEmptyAccount;
    case PostingError::InvalidSide:
        return PostingPolicyError::PostingInvalidSide;
    case PostingError::UnsupportedCurrency:
        return PostingPolicyError::PostingUnsupportedCurrency;
    case PostingError::SelfReversal:
        return PostingPolicyError::PostingSelfReversal;
    }
    return PostingPolicyError::PostingInvalidSide;
}

[[nodiscard]] PostingPolicyError map_ledger_error(
    const LedgerError error) noexcept {
    switch (error) {
    case LedgerError::EmptyPostings:
        return PostingPolicyError::LedgerEmptyPostings;
    case LedgerError::DuplicatePostingId:
        return PostingPolicyError::LedgerDuplicatePostingId;
    case LedgerError::InvalidPostingSide:
        return PostingPolicyError::LedgerInvalidPostingSide;
    case LedgerError::UnsupportedCurrency:
        return PostingPolicyError::LedgerUnsupportedCurrency;
    case LedgerError::BalanceOverflow:
        return PostingPolicyError::LedgerBalanceOverflow;
    case LedgerError::Unbalanced:
        return PostingPolicyError::LedgerUnbalanced;
    }
    return PostingPolicyError::LedgerUnbalanced;
}

[[nodiscard]] std::string account(
    const std::string_view prefix,
    const std::string_view identifier) {
    std::string result;
    result.reserve(prefix.size() + identifier.size());
    result.append(prefix);
    result.append(identifier);
    return result;
}

[[nodiscard]] bool has_prefix(
    const std::string_view value,
    const std::string_view prefix) noexcept {
    return value.starts_with(prefix);
}

[[nodiscard]] bool is_confirmation_shape(
    const std::vector<Posting>& postings) noexcept {
    if (postings.size() != 4U) {
        return false;
    }

    const Posting& pay_control = postings[0U];
    const Posting& pay_payable = postings[1U];
    const Posting& receive_receivable = postings[2U];
    const Posting& receive_control = postings[3U];

    if (pay_control.side() != PostingSide::Debit ||
        pay_payable.side() != PostingSide::Credit ||
        receive_receivable.side() != PostingSide::Debit ||
        receive_control.side() != PostingSide::Credit) {
        return false;
    }

    if (!has_prefix(pay_control.account(), counterparty_control_prefix) ||
        !has_prefix(pay_payable.account(), settlement_payable_prefix) ||
        !has_prefix(
            receive_receivable.account(),
            settlement_receivable_prefix) ||
        receive_control.account() != pay_control.account()) {
        return false;
    }

    const std::string_view counterparty =
        std::string_view{pay_control.account()}.substr(
            counterparty_control_prefix.size());
    const std::string_view payable_book =
        std::string_view{pay_payable.account()}.substr(
            settlement_payable_prefix.size());
    const std::string_view receivable_book =
        std::string_view{receive_receivable.account()}.substr(
            settlement_receivable_prefix.size());
    if (counterparty.empty() || payable_book.empty() ||
        payable_book != receivable_book) {
        return false;
    }

    if (pay_control.amount() != pay_payable.amount() ||
        receive_receivable.amount() != receive_control.amount() ||
        pay_control.amount().currency() ==
            receive_receivable.amount().currency()) {
        return false;
    }

    for (const Posting& posting : postings) {
        if (posting.trade_id() != pay_control.trade_id() ||
            posting.trade_version() != pay_control.trade_version() ||
            posting.reversal_of().has_value()) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] Outcome<LedgerEntry, PostingPolicyError> make_ledger_entry(
    std::vector<Posting> postings) {
    auto result = LedgerEntry::create(std::move(postings));
    if (result.has_error()) {
        return Outcome<LedgerEntry, PostingPolicyError>::failure(
            map_ledger_error(result.error()));
    }
    return Outcome<LedgerEntry, PostingPolicyError>::success(
        std::move(result).value());
}

}  // namespace

Outcome<LedgerEntry, PostingPolicyError> build_confirmation_entry(
    const Trade& confirmed,
    const ConfirmationPostingIds& posting_ids) {
    if (confirmed.state() != TradeState::Confirmed) {
        return Outcome<LedgerEntry, PostingPolicyError>::failure(
            PostingPolicyError::TradeNotConfirmed);
    }

    const std::string control_account = account(
        counterparty_control_prefix,
        confirmed.counterparty_id().value());
    const std::string payable_account =
        account(settlement_payable_prefix, confirmed.book_id().value());
    const std::string receivable_account =
        account(settlement_receivable_prefix, confirmed.book_id().value());

    std::array<Outcome<Posting, PostingError>, 4U> posting_results{
        Posting::create(
            posting_ids.pay_control_debit,
            confirmed.id(),
            confirmed.version(),
            control_account,
            PostingSide::Debit,
            confirmed.terms().pay()),
        Posting::create(
            posting_ids.pay_payable_credit,
            confirmed.id(),
            confirmed.version(),
            payable_account,
            PostingSide::Credit,
            confirmed.terms().pay()),
        Posting::create(
            posting_ids.receive_receivable_debit,
            confirmed.id(),
            confirmed.version(),
            receivable_account,
            PostingSide::Debit,
            confirmed.terms().receive()),
        Posting::create(
            posting_ids.receive_control_credit,
            confirmed.id(),
            confirmed.version(),
            control_account,
            PostingSide::Credit,
            confirmed.terms().receive())};

    std::vector<Posting> postings;
    postings.reserve(posting_results.size());
    for (auto& posting_result : posting_results) {
        if (posting_result.has_error()) {
            return Outcome<LedgerEntry, PostingPolicyError>::failure(
                map_posting_error(posting_result.error()));
        }
        postings.push_back(std::move(posting_result).value());
    }

    return make_ledger_entry(std::move(postings));
}

Outcome<LedgerEntry, PostingPolicyError> build_reversal_entry(
    const LedgerEntry& original,
    const ReversalPostingIds& reversal_ids) {
    const auto& original_postings = original.postings();
    if (!is_confirmation_shape(original_postings)) {
        return Outcome<LedgerEntry, PostingPolicyError>::failure(
            PostingPolicyError::InvalidOriginalShape);
    }

    const std::array<PostingId, 4U> ids{
        reversal_ids.pay_control_credit,
        reversal_ids.pay_payable_debit,
        reversal_ids.receive_receivable_credit,
        reversal_ids.receive_control_debit};

    std::vector<Posting> reversals;
    reversals.reserve(ids.size());
    for (std::size_t index = 0U; index < ids.size(); ++index) {
        auto result = Posting::reverse(ids[index], original_postings[index]);
        if (result.has_error()) {
            return Outcome<LedgerEntry, PostingPolicyError>::failure(
                map_posting_error(result.error()));
        }
        reversals.push_back(std::move(result).value());
    }

    return make_ledger_entry(std::move(reversals));
}

}  // namespace backbook::domain
