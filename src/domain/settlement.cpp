#include "backbook/domain/settlement.hpp"

#include <algorithm>
#include <compare>
#include <limits>
#include <map>
#include <utility>

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

[[nodiscard]] bool checked_add(
    Money::MinorUnits& total,
    const Money::MinorUnits delta) noexcept {
    constexpr auto minimum = std::numeric_limits<Money::MinorUnits>::min();
    constexpr auto maximum = std::numeric_limits<Money::MinorUnits>::max();
    if ((delta > 0 && total > maximum - delta) ||
        (delta < 0 && total < minimum - delta)) {
        return false;
    }
    total += delta;
    return true;
}

[[nodiscard]] Outcome<Money::MinorUnits, SettlementError> absolute_net(
    const Money::MinorUnits net) noexcept {
    if (net == std::numeric_limits<Money::MinorUnits>::min()) {
        return Outcome<Money::MinorUnits, SettlementError>::failure(
            SettlementError::ArithmeticOverflow);
    }
    return Outcome<Money::MinorUnits, SettlementError>::success(
        net < 0 ? -net : net);
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
    std::map<SettlementKey, Money::MinorUnits> net_by_key;

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

        auto& pay_total = net_by_key[pay_key];
        if (!checked_add(pay_total, -pay.minor_units())) {
            return Outcome<
                std::vector<SettlementObligation>,
                SettlementError>::failure(
                SettlementError::ArithmeticOverflow);
        }

        auto& receive_total = net_by_key[receive_key];
        if (!checked_add(receive_total, receive.minor_units())) {
            return Outcome<
                std::vector<SettlementObligation>,
                SettlementError>::failure(
                SettlementError::ArithmeticOverflow);
        }
    }

    std::vector<SettlementObligation> obligations;
    obligations.reserve(net_by_key.size());
    for (const auto& [key, net] : net_by_key) {
        if (net == 0) {
            continue;
        }

        const auto magnitude = absolute_net(net);
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
            key.counterparty_id,
            key.netting_set_id,
            key.value_date,
            net < 0 ? SettlementDirection::Outgoing
                    : SettlementDirection::Incoming,
            std::move(amount).value()));
    }

    std::sort(obligations.begin(), obligations.end(), settlement_less);
    return Outcome<
        std::vector<SettlementObligation>,
        SettlementError>::success(std::move(obligations));
}

}  // namespace backbook::domain
