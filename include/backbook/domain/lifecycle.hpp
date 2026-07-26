#pragma once

#include "backbook/domain/outcome.hpp"

#include <cstdint>

namespace backbook::domain {

enum class TradeState : std::uint8_t {
    Captured = 0,
    Confirmed = 1,
    Superseded = 2,
    Cancelled = 3,
    Settled = 4,
};

enum class TradeAction : std::uint8_t {
    BookTrade = 0,
    ConfirmTrade = 1,
    AmendTrade = 2,
    CancelTrade = 3,
    RunEod = 4,
};

enum class LifecycleError : std::uint8_t {
    IllegalTransition,
    InvalidState,
    InvalidAction,
};

// Lifecycle changes go through one closed transition table so illegal state
// changes cannot be introduced by individual callers.
[[nodiscard]] Outcome<TradeState, LifecycleError> transition(
    TradeState state,
    TradeAction action);

}  // namespace backbook::domain
