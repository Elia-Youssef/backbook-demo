#pragma once

#include "backbook/domain/date.hpp"
#include "backbook/domain/money.hpp"
#include "backbook/domain/outcome.hpp"

#include <cstdint>

namespace backbook::domain {

enum class InstrumentKind : std::uint8_t {
    FxSpot = 0,
    FxForward = 1,
};

enum class FxTermsError : std::uint8_t {
    InvalidInstrumentKind,
    NonPositivePayAmount,
    NonPositiveReceiveAmount,
    SameCurrency,
};

// These are agreed pay and receive cashflows, not values derived from a price
// or exchange-rate calculation.
class FxTerms final {
public:
    [[nodiscard]] static Outcome<FxTerms, FxTermsError> create(
        InstrumentKind kind,
        IsoDate trade_date,
        IsoDate value_date,
        Money pay,
        Money receive);

    [[nodiscard]] InstrumentKind kind() const noexcept {
        return kind_;
    }

    [[nodiscard]] const IsoDate& trade_date() const noexcept {
        return trade_date_;
    }

    [[nodiscard]] const IsoDate& value_date() const noexcept {
        return value_date_;
    }

    [[nodiscard]] const Money& pay() const noexcept {
        return pay_;
    }

    [[nodiscard]] const Money& receive() const noexcept {
        return receive_;
    }

    [[nodiscard]] friend bool operator==(const FxTerms&, const FxTerms&) = default;

private:
    FxTerms(
        InstrumentKind kind,
        IsoDate trade_date,
        IsoDate value_date,
        Money pay,
        Money receive) noexcept
        : kind_(kind),
          trade_date_(trade_date),
          value_date_(value_date),
          pay_(pay),
          receive_(receive) {}

    InstrumentKind kind_;
    IsoDate trade_date_;
    IsoDate value_date_;
    Money pay_;
    Money receive_;
};

}  // namespace backbook::domain
