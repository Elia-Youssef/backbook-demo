#pragma once

#include "backbook/domain/date.hpp"
#include "backbook/domain/id.hpp"
#include "backbook/domain/money.hpp"
#include "backbook/domain/outcome.hpp"
#include "backbook/domain/trade.hpp"

#include <cstdint>
#include <vector>

namespace backbook::domain {

namespace detail {

class SettlementOperations;

}  // namespace detail

enum class SettlementDirection : std::uint8_t {
    Outgoing = 0,
    Incoming = 1,
};

class SettlementObligation final {
public:
    [[nodiscard]] const CounterpartyId& counterparty_id() const noexcept {
        return counterparty_id_;
    }

    [[nodiscard]] const NettingSetId& netting_set_id() const noexcept {
        return netting_set_id_;
    }

    [[nodiscard]] const IsoDate& value_date() const noexcept {
        return value_date_;
    }

    [[nodiscard]] SettlementDirection direction() const noexcept {
        return direction_;
    }

    [[nodiscard]] const Money& amount() const noexcept {
        return amount_;
    }

    [[nodiscard]] friend bool operator==(
        const SettlementObligation&,
        const SettlementObligation&) = default;

private:
    friend class detail::SettlementOperations;

    SettlementObligation(
        CounterpartyId counterparty_id,
        NettingSetId netting_set_id,
        IsoDate value_date,
        SettlementDirection direction,
        Money amount);

    CounterpartyId counterparty_id_;
    NettingSetId netting_set_id_;
    IsoDate value_date_;
    SettlementDirection direction_;
    Money amount_;
};

enum class SettlementError : std::uint8_t {
    ArithmeticOverflow,
    UnsupportedCurrency,
};

// Netting is bilateral and keeps value date, currency, counterparty, and
// netting set as hard grouping boundaries.
[[nodiscard]] Outcome<std::vector<SettlementObligation>, SettlementError>
derive_bilateral_settlements(const std::vector<Trade>& trades);

}  // namespace backbook::domain
