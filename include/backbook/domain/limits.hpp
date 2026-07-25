#pragma once

#include "backbook/domain/id.hpp"
#include "backbook/domain/money.hpp"
#include "backbook/domain/outcome.hpp"

#include <array>
#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace backbook::domain {

enum class LimitLevel : std::uint8_t {
    Group = 0,
    Counterparty = 1,
    NettingSet = 2,
    Book = 3,
};

class LimitNode final {
public:
    [[nodiscard]] static LimitNode group();
    [[nodiscard]] static LimitNode counterparty(CounterpartyId counterparty_id);
    [[nodiscard]] static LimitNode netting_set(
        CounterpartyId counterparty_id,
        NettingSetId netting_set_id);
    [[nodiscard]] static LimitNode book(
        CounterpartyId counterparty_id,
        NettingSetId netting_set_id,
        BookId book_id);

    [[nodiscard]] LimitLevel level() const noexcept {
        return level_;
    }

    [[nodiscard]] const std::optional<CounterpartyId>& counterparty_id()
        const noexcept {
        return counterparty_id_;
    }

    [[nodiscard]] const std::optional<NettingSetId>& netting_set_id()
        const noexcept {
        return netting_set_id_;
    }

    [[nodiscard]] const std::optional<BookId>& book_id() const noexcept {
        return book_id_;
    }

    [[nodiscard]] std::vector<std::string> path_components() const;

    [[nodiscard]] friend bool operator==(
        const LimitNode&,
        const LimitNode&) = default;
    [[nodiscard]] friend auto operator<=>(
        const LimitNode&,
        const LimitNode&) = default;

private:
    LimitNode(
        LimitLevel level,
        std::optional<CounterpartyId> counterparty_id,
        std::optional<NettingSetId> netting_set_id,
        std::optional<BookId> book_id);

    LimitLevel level_;
    std::optional<CounterpartyId> counterparty_id_;
    std::optional<NettingSetId> netting_set_id_;
    std::optional<BookId> book_id_;
};

class LimitPath final {
public:
    LimitPath(
        CounterpartyId counterparty_id,
        NettingSetId netting_set_id,
        BookId book_id);

    [[nodiscard]] const CounterpartyId& counterparty_id() const noexcept {
        return counterparty_id_;
    }

    [[nodiscard]] const NettingSetId& netting_set_id() const noexcept {
        return netting_set_id_;
    }

    [[nodiscard]] const BookId& book_id() const noexcept {
        return book_id_;
    }

    [[nodiscard]] std::array<LimitNode, 4U> nodes() const;

    [[nodiscard]] friend bool operator==(
        const LimitPath&,
        const LimitPath&) = default;

private:
    CounterpartyId counterparty_id_;
    NettingSetId netting_set_id_;
    BookId book_id_;
};

struct LimitDefinition final {
    LimitNode node;
    Money capacity;

    [[nodiscard]] friend bool operator==(
        const LimitDefinition&,
        const LimitDefinition&) = default;
};

struct LimitBalanceSnapshot final {
    LimitNode node;
    Currency currency;
    Money::MinorUnits capacity_minor_units;
    Money::MinorUnits reserved_minor_units;

    [[nodiscard]] friend bool operator==(
        const LimitBalanceSnapshot&,
        const LimitBalanceSnapshot&) = default;
};

enum class LimitErrorCode : std::uint8_t {
    InvalidCapacity,
    DuplicateDefinition,
    UnsupportedCurrency,
    UnknownNode,
    NonPositiveReservation,
    Breach,
    ReleaseExceedsReservation,
};

struct LimitError final {
    LimitErrorCode code;
    LimitNode node;
    Currency currency;
    Money::MinorUnits required_minor_units;
    Money::MinorUnits remaining_minor_units;

    [[nodiscard]] friend bool operator==(
        const LimitError&,
        const LimitError&) = default;
};

class LimitHierarchy final {
public:
    [[nodiscard]] static Outcome<LimitHierarchy, LimitError> create(
        std::vector<LimitDefinition> definitions);

    [[nodiscard]] Outcome<Money, LimitError> capacity(
        const LimitNode& node,
        Currency currency) const;
    [[nodiscard]] Outcome<Money, LimitError> reserved(
        const LimitNode& node,
        Currency currency) const;
    [[nodiscard]] Outcome<Money, LimitError> headroom(
        const LimitNode& node,
        Currency currency) const;

    [[nodiscard]] Outcome<LimitHierarchy, LimitError> reserve(
        const LimitPath& path,
        const Money& outgoing) const;
    [[nodiscard]] Outcome<LimitHierarchy, LimitError> release(
        const LimitPath& path,
        const Money& outgoing) const;

    [[nodiscard]] std::vector<LimitBalanceSnapshot> snapshots() const;

    [[nodiscard]] friend bool operator==(
        const LimitHierarchy&,
        const LimitHierarchy&) = default;

private:
    struct Key final {
        LimitNode node;
        Currency currency;

        [[nodiscard]] friend bool operator==(
            const Key&,
            const Key&) = default;
        [[nodiscard]] friend auto operator<=>(const Key&, const Key&) = default;
    };

    struct Balance final {
        Money::MinorUnits capacity;
        Money::MinorUnits reserved;

        [[nodiscard]] friend bool operator==(
            const Balance&,
            const Balance&) = default;
    };

    explicit LimitHierarchy(std::map<Key, Balance> balances);

    [[nodiscard]] Outcome<Money, LimitError> amount(
        const LimitNode& node,
        Currency currency,
        bool return_reserved) const;

    std::map<Key, Balance> balances_;
};

}  // namespace backbook::domain
