#pragma once

#include "backbook/domain/fx_terms.hpp"
#include "backbook/domain/id.hpp"
#include "backbook/domain/lifecycle.hpp"
#include "backbook/domain/outcome.hpp"

#include <cstdint>
#include <optional>

namespace backbook::domain {

enum class TradeError : std::uint8_t {
    InvalidVersion,
    AmendmentRequiresConfirmed,
    VersionOverflow,
};

class TradeAmendment;

namespace detail {

class TradeOperations;

}  // namespace detail

// Trade versions are immutable records. Amendment creates a linked replacement
// rather than rewriting the confirmed contract in place.
class Trade final {
public:
    [[nodiscard]] static Trade capture(
        TradeId trade_id,
        BookId book_id,
        CounterpartyId counterparty_id,
        NettingSetId netting_set_id,
        FxTerms terms);

    [[nodiscard]] Outcome<Trade, LifecycleError> apply(TradeAction action) const;

    [[nodiscard]] const TradeId& id() const noexcept {
        return id_;
    }

    [[nodiscard]] std::uint32_t version() const noexcept {
        return version_;
    }

    [[nodiscard]] InstrumentKind kind() const noexcept {
        return terms_.kind();
    }

    [[nodiscard]] const FxTerms& terms() const noexcept {
        return terms_;
    }

    [[nodiscard]] const BookId& book_id() const noexcept {
        return book_id_;
    }

    [[nodiscard]] const CounterpartyId& counterparty_id() const noexcept {
        return counterparty_id_;
    }

    [[nodiscard]] const NettingSetId& netting_set_id() const noexcept {
        return netting_set_id_;
    }

    [[nodiscard]] TradeState state() const noexcept {
        return state_;
    }

    [[nodiscard]] std::optional<std::uint32_t> supersedes() const noexcept {
        return supersedes_;
    }

    [[nodiscard]] std::optional<std::uint32_t> superseded_by() const noexcept {
        return superseded_by_;
    }

    [[nodiscard]] friend bool operator==(const Trade&, const Trade&) = default;

private:
    friend class detail::TradeOperations;

    Trade(
        TradeId trade_id,
        std::uint32_t version,
        BookId book_id,
        CounterpartyId counterparty_id,
        NettingSetId netting_set_id,
        FxTerms terms,
        TradeState state,
        std::optional<std::uint32_t> supersedes,
        std::optional<std::uint32_t> superseded_by);

    TradeId id_;
    std::uint32_t version_;
    BookId book_id_;
    CounterpartyId counterparty_id_;
    NettingSetId netting_set_id_;
    FxTerms terms_;
    TradeState state_;
    std::optional<std::uint32_t> supersedes_;
    std::optional<std::uint32_t> superseded_by_;
};

class TradeAmendment final {
public:
    [[nodiscard]] const Trade& superseded() const noexcept {
        return superseded_;
    }

    [[nodiscard]] const Trade& replacement() const noexcept {
        return replacement_;
    }

private:
    friend class detail::TradeOperations;

    TradeAmendment(Trade superseded, Trade replacement);

    Trade superseded_;
    Trade replacement_;
};

[[nodiscard]] Outcome<std::uint32_t, TradeError> next_trade_version(
    std::uint32_t current_version) noexcept;

[[nodiscard]] Outcome<TradeAmendment, TradeError> amend_trade(
    const Trade& current,
    FxTerms replacement_terms);

}  // namespace backbook::domain
