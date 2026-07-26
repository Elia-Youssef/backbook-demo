#include "backbook/domain/ledger_totals.hpp"

#include <array>
#include <cstddef>
#include <limits>
#include <utility>

namespace backbook::domain {
namespace {

[[nodiscard]] Outcome<std::size_t, LedgerTotalsError> currency_index(
    const Currency currency) {
    switch (currency) {
    case Currency::Usd:
        return Outcome<std::size_t, LedgerTotalsError>::success(0U);
    case Currency::Jpy:
        return Outcome<std::size_t, LedgerTotalsError>::success(1U);
    case Currency::Kwd:
        return Outcome<std::size_t, LedgerTotalsError>::success(2U);
    }
    return Outcome<std::size_t, LedgerTotalsError>::failure(
        LedgerTotalsError::UnsupportedCurrency);
}

[[nodiscard]] constexpr bool checked_add(
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

[[nodiscard]] Outcome<Money::MinorUnits, LedgerTotalsError>
signed_amount(const Posting& posting) {
    switch (posting.side()) {
    case PostingSide::Debit:
        return Outcome<Money::MinorUnits, LedgerTotalsError>::success(
            posting.amount().minor_units());
    case PostingSide::Credit:
        return Outcome<Money::MinorUnits, LedgerTotalsError>::success(
            -posting.amount().minor_units());
    }
    return Outcome<Money::MinorUnits, LedgerTotalsError>::failure(
        LedgerTotalsError::InvalidPostingSide);
}

}  // namespace

Outcome<Money, LedgerTotalsError> LedgerTotals::total(
    const Currency currency) const {
    const auto index = currency_index(currency);
    if (!index) {
        return Outcome<Money, LedgerTotalsError>::failure(index.error());
    }

    auto money = Money::from_minor_units(currency, totals_[index.value()]);
    if (!money) {
        return Outcome<Money, LedgerTotalsError>::failure(
            LedgerTotalsError::UnsupportedCurrency);
    }

    return Outcome<Money, LedgerTotalsError>::success(
        std::move(money).value());
}

Outcome<LedgerTotals, LedgerTotalsError> LedgerTotals::with_posting(
    const Posting& posting) const {
    const auto index = currency_index(posting.amount().currency());
    if (!index) {
        return Outcome<LedgerTotals, LedgerTotalsError>::failure(index.error());
    }

    const auto delta = signed_amount(posting);
    if (!delta) {
        return Outcome<LedgerTotals, LedgerTotalsError>::failure(delta.error());
    }

    auto next = totals_;
    if (!checked_add(next[index.value()], delta.value())) {
        return Outcome<LedgerTotals, LedgerTotalsError>::failure(
            LedgerTotalsError::Overflow);
    }

    return Outcome<LedgerTotals, LedgerTotalsError>::success(
        LedgerTotals(next));
}

Outcome<LedgerTotals, LedgerTotalsError> LedgerTotals::with_entry(
    const LedgerEntry& entry) const {
    auto next = *this;
    for (const auto& posting : entry.postings()) {
        auto updated = next.with_posting(posting);
        if (!updated) {
            return Outcome<LedgerTotals, LedgerTotalsError>::failure(
                updated.error());
        }
        next = std::move(updated).value();
    }
    return Outcome<LedgerTotals, LedgerTotalsError>::success(std::move(next));
}

Outcome<LedgerTotals, LedgerTotalsError> recompute_ledger_totals(
    const std::span<const Posting> postings) {
    // This independent fold is used to verify the incrementally maintained
    // totals rather than trusting the same update path.
    std::array<Money::MinorUnits, 3U> totals{};

    for (const auto& posting : postings) {
        const auto index = currency_index(posting.amount().currency());
        if (!index) {
            return Outcome<LedgerTotals, LedgerTotalsError>::failure(
                index.error());
        }

        const auto delta = signed_amount(posting);
        if (!delta) {
            return Outcome<LedgerTotals, LedgerTotalsError>::failure(
                delta.error());
        }

        if (!checked_add(totals[index.value()], delta.value())) {
            return Outcome<LedgerTotals, LedgerTotalsError>::failure(
                LedgerTotalsError::Overflow);
        }
    }

    return Outcome<LedgerTotals, LedgerTotalsError>::success(
        LedgerTotals(totals));
}

Outcome<LedgerTotals, LedgerTotalsError> recompute_ledger_totals(
    const std::span<const LedgerEntry> entries) {
    std::array<Money::MinorUnits, 3U> totals{};

    for (const auto& entry : entries) {
        for (const auto& posting : entry.postings()) {
            const auto index = currency_index(posting.amount().currency());
            if (!index) {
                return Outcome<LedgerTotals, LedgerTotalsError>::failure(
                    index.error());
            }

            const auto delta = signed_amount(posting);
            if (!delta) {
                return Outcome<LedgerTotals, LedgerTotalsError>::failure(
                    delta.error());
            }

            if (!checked_add(totals[index.value()], delta.value())) {
                return Outcome<LedgerTotals, LedgerTotalsError>::failure(
                    LedgerTotalsError::Overflow);
            }
        }
    }

    return Outcome<LedgerTotals, LedgerTotalsError>::success(
        LedgerTotals(totals));
}

}  // namespace backbook::domain
