#include "backbook/domain/fx_terms.hpp"

namespace backbook::domain {
namespace {

[[nodiscard]] constexpr bool is_supported_kind(const InstrumentKind kind) noexcept {
    switch (kind) {
    case InstrumentKind::FxSpot:
    case InstrumentKind::FxForward:
        return true;
    }
    return false;
}

}  // namespace

Outcome<FxTerms, FxTermsError> FxTerms::create(
    const InstrumentKind kind,
    const IsoDate trade_date,
    const IsoDate value_date,
    const Money pay,
    const Money receive) {
    if (!is_supported_kind(kind)) {
        return Outcome<FxTerms, FxTermsError>::failure(
            FxTermsError::InvalidInstrumentKind);
    }
    if (pay.minor_units() <= 0) {
        return Outcome<FxTerms, FxTermsError>::failure(
            FxTermsError::NonPositivePayAmount);
    }
    if (receive.minor_units() <= 0) {
        return Outcome<FxTerms, FxTermsError>::failure(
            FxTermsError::NonPositiveReceiveAmount);
    }
    if (pay.currency() == receive.currency()) {
        return Outcome<FxTerms, FxTermsError>::failure(FxTermsError::SameCurrency);
    }

    return Outcome<FxTerms, FxTermsError>::success(
        FxTerms(kind, trade_date, value_date, pay, receive));
}

}  // namespace backbook::domain
