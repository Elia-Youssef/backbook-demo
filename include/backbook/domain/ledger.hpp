#pragma once

#include "backbook/domain/id.hpp"
#include "backbook/domain/money.hpp"
#include "backbook/domain/outcome.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace backbook::domain {

enum class PostingSide : std::uint8_t {
    Debit = 0,
    Credit = 1,
};

enum class PostingError : std::uint8_t {
    NonPositiveAmount,
    InvalidTradeVersion,
    EmptyAccount,
    InvalidSide,
    UnsupportedCurrency,
    SelfReversal,
};

class Posting final {
public:
    [[nodiscard]] static Outcome<Posting, PostingError> create(
        PostingId id,
        TradeId trade_id,
        std::uint32_t trade_version,
        std::string account,
        PostingSide side,
        Money amount);

    [[nodiscard]] static Outcome<Posting, PostingError> reverse(
        PostingId id,
        const Posting& original);

    [[nodiscard]] const PostingId& id() const noexcept {
        return id_;
    }

    [[nodiscard]] const TradeId& trade_id() const noexcept {
        return trade_id_;
    }

    [[nodiscard]] std::uint32_t trade_version() const noexcept {
        return trade_version_;
    }

    [[nodiscard]] const std::string& account() const noexcept {
        return account_;
    }

    [[nodiscard]] PostingSide side() const noexcept {
        return side_;
    }

    [[nodiscard]] const Money& amount() const noexcept {
        return amount_;
    }

    [[nodiscard]] const std::optional<PostingId>& reversal_of() const noexcept {
        return reversal_of_;
    }

private:
    Posting(
        PostingId id,
        TradeId trade_id,
        std::uint32_t trade_version,
        std::string account,
        PostingSide side,
        Money amount,
        std::optional<PostingId> reversal_of);

    PostingId id_;
    TradeId trade_id_;
    std::uint32_t trade_version_;
    std::string account_;
    PostingSide side_;
    Money amount_;
    std::optional<PostingId> reversal_of_;
};

enum class LedgerError : std::uint8_t {
    EmptyPostings,
    DuplicatePostingId,
    InvalidPostingSide,
    UnsupportedCurrency,
    BalanceOverflow,
    Unbalanced,
};

class LedgerEntry final {
public:
    [[nodiscard]] static Outcome<LedgerEntry, LedgerError> create(
        std::vector<Posting> postings);

    [[nodiscard]] const std::vector<Posting>& postings() const noexcept {
        return postings_;
    }

private:
    explicit LedgerEntry(std::vector<Posting> postings);

    std::vector<Posting> postings_;
};

}  // namespace backbook::domain
