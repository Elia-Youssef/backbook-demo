#include "backbook/domain/limits.hpp"

#include <utility>

namespace backbook::domain {
namespace {

constexpr auto group_path_component = "GROUP";

[[nodiscard]] LimitError error(
    const LimitErrorCode code,
    const LimitNode& node,
    const Currency currency,
    const Money::MinorUnits required = 0,
    const Money::MinorUnits remaining = 0) {
    return LimitError{code, node, currency, required, remaining};
}

}  // namespace

LimitNode::LimitNode(
    const LimitLevel level,
    std::optional<CounterpartyId> counterparty_id,
    std::optional<NettingSetId> netting_set_id,
    std::optional<BookId> book_id)
    : level_(level),
      counterparty_id_(std::move(counterparty_id)),
      netting_set_id_(std::move(netting_set_id)),
      book_id_(std::move(book_id)) {}

LimitNode LimitNode::group() {
    return LimitNode(
        LimitLevel::Group,
        std::nullopt,
        std::nullopt,
        std::nullopt);
}

LimitNode LimitNode::counterparty(CounterpartyId counterparty_id) {
    return LimitNode(
        LimitLevel::Counterparty,
        std::move(counterparty_id),
        std::nullopt,
        std::nullopt);
}

LimitNode LimitNode::netting_set(
    CounterpartyId counterparty_id,
    NettingSetId netting_set_id) {
    return LimitNode(
        LimitLevel::NettingSet,
        std::move(counterparty_id),
        std::move(netting_set_id),
        std::nullopt);
}

LimitNode LimitNode::book(
    CounterpartyId counterparty_id,
    NettingSetId netting_set_id,
    BookId book_id) {
    return LimitNode(
        LimitLevel::Book,
        std::move(counterparty_id),
        std::move(netting_set_id),
        std::move(book_id));
}

std::vector<std::string> LimitNode::path_components() const {
    std::vector<std::string> result;
    result.reserve(4U);
    result.emplace_back(group_path_component);
    if (counterparty_id_.has_value()) {
        result.emplace_back(counterparty_id_->value());
    }
    if (netting_set_id_.has_value()) {
        result.emplace_back(netting_set_id_->value());
    }
    if (book_id_.has_value()) {
        result.emplace_back(book_id_->value());
    }
    return result;
}

LimitPath::LimitPath(
    CounterpartyId counterparty_id,
    NettingSetId netting_set_id,
    BookId book_id)
    : counterparty_id_(std::move(counterparty_id)),
      netting_set_id_(std::move(netting_set_id)),
      book_id_(std::move(book_id)) {}

std::array<LimitNode, 4U> LimitPath::nodes() const {
    return {
        LimitNode::group(),
        LimitNode::counterparty(counterparty_id_),
        LimitNode::netting_set(counterparty_id_, netting_set_id_),
        LimitNode::book(counterparty_id_, netting_set_id_, book_id_)};
}

LimitHierarchy::LimitHierarchy(std::map<Key, Balance> balances)
    : balances_(std::move(balances)) {}

Outcome<LimitHierarchy, LimitError> LimitHierarchy::create(
    std::vector<LimitDefinition> definitions) {
    std::map<Key, Balance> balances;

    for (const LimitDefinition& definition : definitions) {
        const auto currency = definition.capacity.currency();
        if (!is_supported_currency(currency)) {
            return Outcome<LimitHierarchy, LimitError>::failure(error(
                LimitErrorCode::UnsupportedCurrency,
                definition.node,
                currency));
        }
        if (definition.capacity.minor_units() < 0) {
            return Outcome<LimitHierarchy, LimitError>::failure(error(
                LimitErrorCode::InvalidCapacity,
                definition.node,
                currency,
                definition.capacity.minor_units()));
        }

        const Key key{definition.node, currency};
        const auto [unused, inserted] = balances.emplace(
            key,
            Balance{definition.capacity.minor_units(), 0});
        static_cast<void>(unused);
        if (!inserted) {
            return Outcome<LimitHierarchy, LimitError>::failure(error(
                LimitErrorCode::DuplicateDefinition,
                definition.node,
                currency));
        }
    }

    return Outcome<LimitHierarchy, LimitError>::success(
        LimitHierarchy(std::move(balances)));
}

Outcome<Money, LimitError> LimitHierarchy::amount(
    const LimitNode& node,
    const Currency currency,
    const bool return_reserved) const {
    if (!is_supported_currency(currency)) {
        return Outcome<Money, LimitError>::failure(error(
            LimitErrorCode::UnsupportedCurrency,
            node,
            currency));
    }

    const auto found = balances_.find(Key{node, currency});
    if (found == balances_.end()) {
        return Outcome<Money, LimitError>::failure(error(
            LimitErrorCode::UnknownNode,
            node,
            currency));
    }

    const auto minor_units =
        return_reserved ? found->second.reserved : found->second.capacity;
    auto value = Money::from_minor_units(currency, minor_units);
    if (!value) {
        return Outcome<Money, LimitError>::failure(error(
            LimitErrorCode::UnsupportedCurrency,
            node,
            currency));
    }
    return Outcome<Money, LimitError>::success(std::move(value).value());
}

Outcome<Money, LimitError> LimitHierarchy::capacity(
    const LimitNode& node,
    const Currency currency) const {
    return amount(node, currency, false);
}

Outcome<Money, LimitError> LimitHierarchy::reserved(
    const LimitNode& node,
    const Currency currency) const {
    return amount(node, currency, true);
}

Outcome<Money, LimitError> LimitHierarchy::headroom(
    const LimitNode& node,
    const Currency currency) const {
    if (!is_supported_currency(currency)) {
        return Outcome<Money, LimitError>::failure(error(
            LimitErrorCode::UnsupportedCurrency,
            node,
            currency));
    }

    const auto found = balances_.find(Key{node, currency});
    if (found == balances_.end()) {
        return Outcome<Money, LimitError>::failure(error(
            LimitErrorCode::UnknownNode,
            node,
            currency));
    }

    auto value = Money::from_minor_units(
        currency,
        found->second.capacity - found->second.reserved);
    if (!value) {
        return Outcome<Money, LimitError>::failure(error(
            LimitErrorCode::UnsupportedCurrency,
            node,
            currency));
    }
    return Outcome<Money, LimitError>::success(std::move(value).value());
}

Outcome<LimitHierarchy, LimitError> LimitHierarchy::reserve(
    const LimitPath& path,
    const Money& outgoing) const {
    if (outgoing.minor_units() <= 0) {
        return Outcome<LimitHierarchy, LimitError>::failure(error(
            LimitErrorCode::NonPositiveReservation,
            LimitNode::group(),
            outgoing.currency(),
            outgoing.minor_units()));
    }

    const auto nodes = path.nodes();
    for (const LimitNode& node : nodes) {
        const auto found = balances_.find(Key{node, outgoing.currency()});
        if (found == balances_.end()) {
            return Outcome<LimitHierarchy, LimitError>::failure(error(
                LimitErrorCode::UnknownNode,
                node,
                outgoing.currency(),
                outgoing.minor_units()));
        }

        const auto remaining =
            found->second.capacity - found->second.reserved;
        if (outgoing.minor_units() > remaining) {
            return Outcome<LimitHierarchy, LimitError>::failure(error(
                LimitErrorCode::Breach,
                node,
                outgoing.currency(),
                outgoing.minor_units(),
                remaining));
        }
    }

    // Validate the complete root-to-leaf path before changing any balance.
    auto next = *this;
    for (const LimitNode& node : nodes) {
        auto& balance =
            next.balances_.at(Key{node, outgoing.currency()});
        balance.reserved += outgoing.minor_units();
    }
    return Outcome<LimitHierarchy, LimitError>::success(std::move(next));
}

Outcome<LimitHierarchy, LimitError> LimitHierarchy::release(
    const LimitPath& path,
    const Money& outgoing) const {
    if (outgoing.minor_units() <= 0) {
        return Outcome<LimitHierarchy, LimitError>::failure(error(
            LimitErrorCode::NonPositiveReservation,
            LimitNode::group(),
            outgoing.currency(),
            outgoing.minor_units()));
    }

    const auto nodes = path.nodes();
    for (const LimitNode& node : nodes) {
        const auto found = balances_.find(Key{node, outgoing.currency()});
        if (found == balances_.end()) {
            return Outcome<LimitHierarchy, LimitError>::failure(error(
                LimitErrorCode::UnknownNode,
                node,
                outgoing.currency(),
                outgoing.minor_units()));
        }
        if (outgoing.minor_units() > found->second.reserved) {
            return Outcome<LimitHierarchy, LimitError>::failure(error(
                LimitErrorCode::ReleaseExceedsReservation,
                node,
                outgoing.currency(),
                outgoing.minor_units(),
                found->second.reserved));
        }
    }

    // As with reserve, mutation happens only after every node has passed.
    auto next = *this;
    for (const LimitNode& node : nodes) {
        auto& balance =
            next.balances_.at(Key{node, outgoing.currency()});
        balance.reserved -= outgoing.minor_units();
    }
    return Outcome<LimitHierarchy, LimitError>::success(std::move(next));
}

std::vector<LimitBalanceSnapshot> LimitHierarchy::snapshots() const {
    std::vector<LimitBalanceSnapshot> result;
    result.reserve(balances_.size());
    for (const auto& [key, balance] : balances_) {
        result.push_back(LimitBalanceSnapshot{
            key.node,
            key.currency,
            balance.capacity,
            balance.reserved});
    }
    return result;
}

}  // namespace backbook::domain
