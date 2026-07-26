#pragma once

#include "backbook/domain/ledger.hpp"
#include "backbook/domain/money.hpp"
#include "backbook/domain/outcome.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace backbook::domain {

enum class LedgerTotalsError : std::uint8_t {
    Overflow,
    UnsupportedCurrency,
    InvalidPostingSide,
};

// Totals use debit minus credit for each supported currency. A balanced ledger
// therefore reports zero without converting between currencies.
class LedgerTotals final {
public:
    [[nodiscard]] static constexpr LedgerTotals zero() noexcept {
        return LedgerTotals({});
    }

    [[nodiscard]] Outcome<Money, LedgerTotalsError> total(
        Currency currency) const;

    [[nodiscard]] Outcome<LedgerTotals, LedgerTotalsError> with_posting(
        const Posting& posting) const;

    [[nodiscard]] Outcome<LedgerTotals, LedgerTotalsError> with_entry(
        const LedgerEntry& entry) const;

    [[nodiscard]] friend constexpr bool operator==(
        const LedgerTotals&,
        const LedgerTotals&) noexcept = default;

private:
    explicit constexpr LedgerTotals(
        std::array<Money::MinorUnits, 3U> totals) noexcept
        : totals_(totals) {}

    friend Outcome<LedgerTotals, LedgerTotalsError> recompute_ledger_totals(
        std::span<const Posting> postings);
    friend Outcome<LedgerTotals, LedgerTotalsError> recompute_ledger_totals(
        std::span<const LedgerEntry> entries);

    std::array<Money::MinorUnits, 3U> totals_;
};

[[nodiscard]] Outcome<LedgerTotals, LedgerTotalsError> recompute_ledger_totals(
    std::span<const Posting> postings);

[[nodiscard]] Outcome<LedgerTotals, LedgerTotalsError> recompute_ledger_totals(
    std::span<const LedgerEntry> entries);

}  // namespace backbook::domain
