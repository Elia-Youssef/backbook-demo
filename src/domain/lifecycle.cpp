#include "backbook/domain/lifecycle.hpp"

#include <array>
#include <cstddef>
#include <optional>

namespace backbook::domain {
namespace {

constexpr std::size_t trade_state_count = 5;
constexpr std::size_t trade_action_count = 5;

using Transition = std::optional<TradeState>;
using TransitionRow = std::array<Transition, trade_action_count>;
using TransitionTable = std::array<TransitionRow, trade_state_count>;

constexpr Transition illegal = std::nullopt;

constexpr TransitionTable transitions{{
    // BookTrade, ConfirmTrade, AmendTrade, CancelTrade, RunEod
    TransitionRow{illegal, TradeState::Confirmed, illegal, TradeState::Cancelled, illegal},
    TransitionRow{
        illegal,
        illegal,
        TradeState::Superseded,
        TradeState::Cancelled,
        TradeState::Settled,
    },
    TransitionRow{illegal, illegal, illegal, illegal, illegal},
    TransitionRow{illegal, illegal, illegal, illegal, illegal},
    TransitionRow{illegal, illegal, illegal, illegal, illegal},
}};

[[nodiscard]] constexpr std::size_t index_of(TradeState state) noexcept {
    return static_cast<std::size_t>(state);
}

[[nodiscard]] constexpr std::size_t index_of(TradeAction action) noexcept {
    return static_cast<std::size_t>(action);
}

}  // namespace

Outcome<TradeState, LifecycleError> transition(TradeState state, TradeAction action) {
    const auto state_index = index_of(state);
    if (state_index >= transitions.size()) {
        return Outcome<TradeState, LifecycleError>::failure(LifecycleError::InvalidState);
    }

    const auto action_index = index_of(action);
    if (action_index >= transitions[state_index].size()) {
        return Outcome<TradeState, LifecycleError>::failure(LifecycleError::InvalidAction);
    }

    const auto next_state = transitions[state_index][action_index];
    if (!next_state.has_value()) {
        return Outcome<TradeState, LifecycleError>::failure(
            LifecycleError::IllegalTransition);
    }

    return Outcome<TradeState, LifecycleError>::success(*next_state);
}

}  // namespace backbook::domain
