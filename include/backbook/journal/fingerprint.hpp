#pragma once

#include "backbook/domain/outcome.hpp"
#include "backbook/domain/state.hpp"
#include "backbook/journal/command_batch.hpp"

#include <cstdint>
#include <span>

namespace backbook::journal {

inline constexpr std::uint8_t state_export_format_version = 1U;

enum class FingerprintError : std::uint8_t {
    SizeOverflow,
};

[[nodiscard]] domain::Outcome<Bytes, FingerprintError>
canonical_state_bytes(const domain::State& state);

[[nodiscard]] std::uint64_t
fnv1a64(std::span<const std::uint8_t> bytes) noexcept;

[[nodiscard]] domain::Outcome<std::uint64_t, FingerprintError>
state_fingerprint(const domain::State& state);

}  // namespace backbook::journal
