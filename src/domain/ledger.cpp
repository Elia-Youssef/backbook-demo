#include "backbook/domain/ledger.hpp"

#include <array>
#include <cstddef>
#include <limits>
#include <set>
#include <utility>

namespace backbook::domain {
namespace {

[[nodiscard]] bool is_valid_side(const PostingSide side) noexcept {
    switch (side) {
    case PostingSide::Debit:
    case PostingSide::Credit:
        return true;
    }
    return false;
}

[[nodiscard]] std::optional<std::size_t> currency_index(
    const Currency currency) noexcept {
    switch (currency) {
    case Currency::Usd:
        return 0U;
    case Currency::Jpy:
        return 1U;
    case Currency::Kwd:
        return 2U;
    }
    return std::nullopt;
}

[[nodiscard]] bool checked_add_positive(
    std::int64_t& total,
    const std::int64_t amount) noexcept {
    if (total > std::numeric_limits<std::int64_t>::max() - amount) {
        return false;
    }
    total += amount;
    return true;
}

[[nodiscard]] bool checked_subtract_positive(
    std::int64_t& total,
    const std::int64_t amount) noexcept {
    if (total < std::numeric_limits<std::int64_t>::min() + amount) {
        return false;
    }
    total -= amount;
    return true;
}

}  // namespace

Posting::Posting(
    PostingId id,
    TradeId trade_id,
    const std::uint32_t trade_version,
    std::string account,
    const PostingSide side,
    Money amount,
    std::optional<PostingId> reversal_of)
    : id_(std::move(id)),
      trade_id_(std::move(trade_id)),
      trade_version_(trade_version),
      account_(std::move(account)),
      side_(side),
      amount_(std::move(amount)),
      reversal_of_(std::move(reversal_of)) {}

Outcome<Posting, PostingError> Posting::create(
    PostingId id,
    TradeId trade_id,
    const std::uint32_t trade_version,
    std::string account,
    const PostingSide side,
    Money amount) {
    if (trade_version == 0U) {
        return Outcome<Posting, PostingError>::failure(
            PostingError::InvalidTradeVersion);
    }
    if (account.empty()) {
        return Outcome<Posting, PostingError>::failure(PostingError::EmptyAccount);
    }
    if (!is_valid_side(side)) {
        return Outcome<Posting, PostingError>::failure(PostingError::InvalidSide);
    }
    if (!is_supported_currency(amount.currency())) {
        return Outcome<Posting, PostingError>::failure(
            PostingError::UnsupportedCurrency);
    }
    if (amount.minor_units() <= 0) {
        return Outcome<Posting, PostingError>::failure(
            PostingError::NonPositiveAmount);
    }
    return Outcome<Posting, PostingError>::success(Posting(
        std::move(id),
        std::move(trade_id),
        trade_version,
        std::move(account),
        side,
        std::move(amount),
        std::nullopt));
}

Outcome<Posting, PostingError> Posting::reverse(
    PostingId id,
    const Posting& original) {
    if (id == original.id()) {
        return Outcome<Posting, PostingError>::failure(
            PostingError::SelfReversal);
    }

    PostingSide reversed_side;
    switch (original.side()) {
    case PostingSide::Debit:
        reversed_side = PostingSide::Credit;
        break;
    case PostingSide::Credit:
        reversed_side = PostingSide::Debit;
        break;
    default:
        return Outcome<Posting, PostingError>::failure(
            PostingError::InvalidSide);
    }

    return Outcome<Posting, PostingError>::success(Posting(
        std::move(id),
        original.trade_id(),
        original.trade_version(),
        original.account(),
        reversed_side,
        original.amount(),
        original.id()));
}

LedgerEntry::LedgerEntry(std::vector<Posting> postings)
    : postings_(std::move(postings)) {}

Outcome<LedgerEntry, LedgerError> LedgerEntry::create(
    std::vector<Posting> postings) {
    if (postings.empty()) {
        return Outcome<LedgerEntry, LedgerError>::failure(
            LedgerError::EmptyPostings);
    }

    std::set<PostingId> posting_ids;
    std::array<std::int64_t, 3U> balances{};

    // Debit adds and credit subtracts from the bucket for that currency.
    for (const auto& posting : postings) {
        if (!posting_ids.insert(posting.id()).second) {
            return Outcome<LedgerEntry, LedgerError>::failure(
                LedgerError::DuplicatePostingId);
        }
        if (!is_valid_side(posting.side())) {
            return Outcome<LedgerEntry, LedgerError>::failure(
                LedgerError::InvalidPostingSide);
        }

        const auto index = currency_index(posting.amount().currency());
        if (!index.has_value()) {
            return Outcome<LedgerEntry, LedgerError>::failure(
                LedgerError::UnsupportedCurrency);
        }

        const auto amount = posting.amount().minor_units();
        const bool updated =
            posting.side() == PostingSide::Debit
                ? checked_add_positive(balances[index.value()], amount)
                : checked_subtract_positive(balances[index.value()], amount);
        if (!updated) {
            return Outcome<LedgerEntry, LedgerError>::failure(
                LedgerError::BalanceOverflow);
        }
    }

    // Cross-currency offsets are deliberately impossible: every bucket must be
    // zero on its own.
    for (const auto balance : balances) {
        if (balance != 0) {
            return Outcome<LedgerEntry, LedgerError>::failure(
                LedgerError::Unbalanced);
        }
    }

    return Outcome<LedgerEntry, LedgerError>::success(
        LedgerEntry(std::move(postings)));
}

}  // namespace backbook::domain
