#include "backbook/domain/lifecycle.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace backbook::domain {
namespace {

constexpr std::array all_states{
    TradeState::Captured,
    TradeState::Confirmed,
    TradeState::Superseded,
    TradeState::Cancelled,
    TradeState::Settled,
};

constexpr std::array all_actions{
    TradeAction::BookTrade,
    TradeAction::ConfirmTrade,
    TradeAction::AmendTrade,
    TradeAction::CancelTrade,
    TradeAction::RunEod,
};

constexpr std::array<std::optional<TradeState>, all_states.size() * all_actions.size()>
    expected_transitions{
        // Captured
        std::nullopt,
        TradeState::Confirmed,
        std::nullopt,
        TradeState::Cancelled,
        std::nullopt,
        // Confirmed
        std::nullopt,
        std::nullopt,
        TradeState::Superseded,
        TradeState::Cancelled,
        TradeState::Settled,
        // Superseded
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        // Cancelled
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        // Settled
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
    };

static_assert(all_states.size() == 5);
static_assert(all_actions.size() == 5);
static_assert(expected_transitions.size() == all_states.size() * all_actions.size());
static_assert(std::is_same_v<std::underlying_type_t<TradeState>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<TradeAction>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<LifecycleError>, std::uint8_t>);

TEST(LifecycleTest, ImplementsEveryNamedStateActionCell) {
    std::size_t tested_cells = 0;

    for (std::size_t state_index = 0; state_index < all_states.size(); ++state_index) {
        for (std::size_t action_index = 0; action_index < all_actions.size();
             ++action_index) {
            const auto expected =
                expected_transitions[(state_index * all_actions.size()) + action_index];
            const auto result = transition(all_states[state_index], all_actions[action_index]);

            if (expected.has_value()) {
                ASSERT_TRUE(result) << "state index " << state_index << ", action index "
                                    << action_index;
                EXPECT_EQ(result.value(), *expected);
            } else {
                ASSERT_FALSE(result) << "state index " << state_index << ", action index "
                                     << action_index;
                EXPECT_EQ(result.error(), LifecycleError::IllegalTransition);
            }
            ++tested_cells;
        }
    }

    EXPECT_EQ(tested_cells, expected_transitions.size());
}

TEST(LifecycleTest, ProducesEveryLegalTransitionExactly) {
    struct LegalTransition final {
        TradeState from;
        TradeAction action;
        TradeState to;
    };

    constexpr std::array legal_transitions{
        LegalTransition{TradeState::Captured, TradeAction::ConfirmTrade, TradeState::Confirmed},
        LegalTransition{TradeState::Captured, TradeAction::CancelTrade, TradeState::Cancelled},
        LegalTransition{
            TradeState::Confirmed,
            TradeAction::AmendTrade,
            TradeState::Superseded,
        },
        LegalTransition{
            TradeState::Confirmed,
            TradeAction::CancelTrade,
            TradeState::Cancelled,
        },
        LegalTransition{TradeState::Confirmed, TradeAction::RunEod, TradeState::Settled},
    };

    for (const auto& expected : legal_transitions) {
        const auto result = transition(expected.from, expected.action);

        ASSERT_TRUE(result);
        EXPECT_EQ(result.value(), expected.to);
    }
}

TEST(LifecycleTest, BookTradeIsIllegalForEveryExistingState) {
    for (const auto state : all_states) {
        const auto result = transition(state, TradeAction::BookTrade);

        ASSERT_FALSE(result);
        EXPECT_EQ(result.error(), LifecycleError::IllegalTransition);
    }
}

TEST(LifecycleTest, RejectsInvalidStateBeforeIndexingTransitionTable) {
    const auto result =
        transition(static_cast<TradeState>(0xFFU), TradeAction::ConfirmTrade);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), LifecycleError::InvalidState);
}

TEST(LifecycleTest, RejectsInvalidActionBeforeIndexingTransitionTable) {
    const auto result =
        transition(TradeState::Captured, static_cast<TradeAction>(0xFFU));

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), LifecycleError::InvalidAction);
}

TEST(LifecycleTest, InvalidStateTakesPrecedenceWhenBothValuesAreInvalid) {
    const auto result =
        transition(static_cast<TradeState>(0xFEU), static_cast<TradeAction>(0xFDU));

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), LifecycleError::InvalidState);
}

}  // namespace
}  // namespace backbook::domain
