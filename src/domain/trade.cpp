#include "backbook/domain/trade.hpp"

#include <limits>
#include <utility>

namespace backbook::domain {

Trade::Trade(
    TradeId trade_id,
    const std::uint32_t version,
    BookId book_id,
    CounterpartyId counterparty_id,
    NettingSetId netting_set_id,
    FxTerms terms,
    const TradeState state,
    std::optional<std::uint32_t> supersedes,
    std::optional<std::uint32_t> superseded_by)
    : id_(std::move(trade_id)),
      version_(version),
      book_id_(std::move(book_id)),
      counterparty_id_(std::move(counterparty_id)),
      netting_set_id_(std::move(netting_set_id)),
      terms_(std::move(terms)),
      state_(state),
      supersedes_(supersedes),
      superseded_by_(superseded_by) {}

Trade Trade::capture(
    TradeId trade_id,
    BookId book_id,
    CounterpartyId counterparty_id,
    NettingSetId netting_set_id,
    FxTerms terms) {
    return Trade(
        std::move(trade_id),
        1U,
        std::move(book_id),
        std::move(counterparty_id),
        std::move(netting_set_id),
        std::move(terms),
        TradeState::Captured,
        std::nullopt,
        std::nullopt);
}

Outcome<Trade, LifecycleError> Trade::apply(const TradeAction action) const {
    const auto next_state = transition(state_, action);
    if (!next_state) {
        return Outcome<Trade, LifecycleError>::failure(next_state.error());
    }

    if (action == TradeAction::AmendTrade) {
        return Outcome<Trade, LifecycleError>::failure(
            LifecycleError::IllegalTransition);
    }

    return Outcome<Trade, LifecycleError>::success(Trade(
        id_,
        version_,
        book_id_,
        counterparty_id_,
        netting_set_id_,
        terms_,
        next_state.value(),
        supersedes_,
        superseded_by_));
}

TradeAmendment::TradeAmendment(Trade superseded, Trade replacement)
    : superseded_(std::move(superseded)),
      replacement_(std::move(replacement)) {}

Outcome<std::uint32_t, TradeError> next_trade_version(
    const std::uint32_t current_version) noexcept {
    if (current_version == 0U) {
        return Outcome<std::uint32_t, TradeError>::failure(
            TradeError::InvalidVersion);
    }
    if (current_version == std::numeric_limits<std::uint32_t>::max()) {
        return Outcome<std::uint32_t, TradeError>::failure(
            TradeError::VersionOverflow);
    }
    return Outcome<std::uint32_t, TradeError>::success(current_version + 1U);
}

namespace detail {

class TradeOperations final {
public:
    [[nodiscard]] static Outcome<TradeAmendment, TradeError> amend(
        const Trade& current,
        FxTerms replacement_terms) {
        const auto transition_result =
            transition(current.state_, TradeAction::AmendTrade);
        if (!transition_result ||
            transition_result.value() != TradeState::Superseded) {
            return Outcome<TradeAmendment, TradeError>::failure(
                TradeError::AmendmentRequiresConfirmed);
        }

        const auto replacement_version = next_trade_version(current.version_);
        if (!replacement_version) {
            return Outcome<TradeAmendment, TradeError>::failure(
                replacement_version.error());
        }

        Trade superseded(
            current.id_,
            current.version_,
            current.book_id_,
            current.counterparty_id_,
            current.netting_set_id_,
            current.terms_,
            TradeState::Superseded,
            current.supersedes_,
            replacement_version.value());

        Trade replacement(
            current.id_,
            replacement_version.value(),
            current.book_id_,
            current.counterparty_id_,
            current.netting_set_id_,
            std::move(replacement_terms),
            TradeState::Confirmed,
            current.version_,
            std::nullopt);

        // The replacement remains confirmed because it replaces an already
        // confirmed contractual version.
        return Outcome<TradeAmendment, TradeError>::success(TradeAmendment(
            std::move(superseded),
            std::move(replacement)));
    }
};

}  // namespace detail

Outcome<TradeAmendment, TradeError> amend_trade(
    const Trade& current,
    FxTerms replacement_terms) {
    return detail::TradeOperations::amend(
        current,
        std::move(replacement_terms));
}

}  // namespace backbook::domain
