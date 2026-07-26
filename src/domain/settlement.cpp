#include "backbook/domain/settlement.hpp"

#include <algorithm>
#include <compare>
#include <limits>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace backbook::domain {
namespace {

struct SettlementKey final {
    CounterpartyId counterparty_id;
    NettingSetId netting_set_id;
    IsoDate value_date;
    Currency currency;

    [[nodiscard]] friend auto operator<=>(
        const SettlementKey&,
        const SettlementKey&) = default;
};

struct NetAccumulator final {
    std::optional<SettlementDirection> direction;
    std::vector<Money::MinorUnits> unmatched_amounts;
};

void add_cashflow(NetAccumulator& accumulator,
                  const SettlementDirection direction,
                  Money::MinorUnits amount) {
    if (!accumulator.direction.has_value() ||
        accumulator.direction == direction) {
        accumulator.direction = direction;
        accumulator.unmatched_amounts.push_back(amount);
        return;
    }

    while (amount > 0 && !accumulator.unmatched_amounts.empty()) {
        auto& opposite = accumulator.unmatched_amounts.back();
        if (opposite > amount) {
            opposite -= amount;
            amount = 0;
        } else {
            amount -= opposite;
            accumulator.unmatched_amounts.pop_back();
        }
    }

    if (amount > 0) {
        accumulator.direction = direction;
        accumulator.unmatched_amounts.push_back(amount);
    } else if (accumulator.unmatched_amounts.empty()) {
        accumulator.direction.reset();
    }
}

[[nodiscard]] Outcome<Money::MinorUnits, SettlementError>
total_magnitude(const NetAccumulator& accumulator) noexcept {
    constexpr auto maximum = std::numeric_limits<Money::MinorUnits>::max();
    Money::MinorUnits total = 0;
    for (const auto amount : accumulator.unmatched_amounts) {
        if (total > maximum - amount) {
            return Outcome<Money::MinorUnits, SettlementError>::failure(
                SettlementError::ArithmeticOverflow);
        }
        total += amount;
    }
    return Outcome<Money::MinorUnits, SettlementError>::success(total);
}

[[nodiscard]] bool settlement_less(
    const SettlementObligation& lhs,
    const SettlementObligation& rhs) {
    if (lhs.value_date() != rhs.value_date()) {
        return lhs.value_date() < rhs.value_date();
    }
    if (lhs.amount().currency() != rhs.amount().currency()) {
        return lhs.amount().currency() < rhs.amount().currency();
    }
    if (lhs.counterparty_id() != rhs.counterparty_id()) {
        return lhs.counterparty_id() < rhs.counterparty_id();
    }
    if (lhs.netting_set_id() != rhs.netting_set_id()) {
        return lhs.netting_set_id() < rhs.netting_set_id();
    }
    return lhs.direction() < rhs.direction();
}

}  // namespace

namespace detail {

class SettlementOperations final {
public:
    [[nodiscard]] static SettlementObligation make(
        CounterpartyId counterparty_id,
        NettingSetId netting_set_id,
        IsoDate value_date,
        const SettlementDirection direction,
        Money amount) {
        return SettlementObligation(
            std::move(counterparty_id),
            std::move(netting_set_id),
            value_date,
            direction,
            std::move(amount));
    }
};

}  // namespace detail

SettlementObligation::SettlementObligation(
    CounterpartyId counterparty_id,
    NettingSetId netting_set_id,
    IsoDate value_date,
    const SettlementDirection direction,
    Money amount)
    : counterparty_id_(std::move(counterparty_id)),
      netting_set_id_(std::move(netting_set_id)),
      value_date_(value_date),
      direction_(direction),
      amount_(std::move(amount)) {}

Outcome<std::vector<SettlementObligation>, SettlementError>
derive_bilateral_settlements(const std::vector<Trade>& trades) {
    std::map<SettlementKey, NetAccumulator> net_by_key;

    for (const Trade& trade : trades) {
        if (trade.state() != TradeState::Settled) {
            continue;
        }

        const auto& pay = trade.terms().pay();
        const auto& receive = trade.terms().receive();
        if (!is_supported_currency(pay.currency()) ||
            !is_supported_currency(receive.currency())) {
            return Outcome<
                std::vector<SettlementObligation>,
                SettlementError>::failure(
                SettlementError::UnsupportedCurrency);
        }

        const SettlementKey pay_key{
            trade.counterparty_id(),
            trade.netting_set_id(),
            trade.terms().value_date(),
            pay.currency()};
        const SettlementKey receive_key{
            trade.counterparty_id(),
            trade.netting_set_id(),
            trade.terms().value_date(),
            receive.currency()};

        add_cashflow(net_by_key[pay_key], SettlementDirection::Outgoing,
                     pay.minor_units());
        add_cashflow(net_by_key[receive_key], SettlementDirection::Incoming,
                     receive.minor_units());
    }

    std::vector<SettlementObligation> obligations;
    obligations.reserve(net_by_key.size());
    for (const auto& [key, accumulator] : net_by_key) {
        if (!accumulator.direction.has_value()) {
            continue;
        }

        const auto magnitude = total_magnitude(accumulator);
        if (!magnitude) {
            return Outcome<
                std::vector<SettlementObligation>,
                SettlementError>::failure(magnitude.error());
        }
        auto amount = Money::from_minor_units(key.currency, magnitude.value());
        if (!amount) {
            return Outcome<
                std::vector<SettlementObligation>,
                SettlementError>::failure(
                SettlementError::UnsupportedCurrency);
        }

        obligations.push_back(detail::SettlementOperations::make(
            key.counterparty_id, key.netting_set_id, key.value_date,
            *accumulator.direction, std::move(amount).value()));
    }

    std::sort(obligations.begin(), obligations.end(), settlement_less);
    return Outcome<
        std::vector<SettlementObligation>,
        SettlementError>::success(std::move(obligations));
}

}  // namespace backbook::domain
