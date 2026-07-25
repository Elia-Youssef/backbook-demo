#pragma once

#include "backbook/domain/limits.hpp"
#include "backbook/domain/outcome.hpp"
#include "backbook/server/json_codec.hpp"
#include "backbook/service/command_service.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace backbook::server {

inline constexpr std::uint64_t canonical_demo_state_fingerprint =
    0x21bd5cac4ef6e98dULL;

enum class DemoLimitError : std::uint8_t {
    DefinitionFailed,
};

[[nodiscard]] domain::Outcome<domain::LimitHierarchy, DemoLimitError>
canonical_demo_limits();

struct SeedCommandResult final {
    std::size_t line_number;
    domain::CommandId command_id;
    std::optional<service::CommandReceipt> receipt{};
    std::optional<service::CommandServiceError> error{};
};

struct SeedRunReport final {
    std::vector<SeedCommandResult> commands;
    std::size_t accepted_count;
    std::size_t rejected_count;
};

enum class SeedErrorCode : std::uint8_t {
    OpenFailed,
    ReadFailed,
    EmptyLine,
    LineTooLarge,
    DecodeFailed,
    ExecutionFailed,
};

struct SeedError final {
    SeedErrorCode code;
    std::size_t line_number;
    std::optional<CommandDecodeError> decode_error{};
    std::optional<service::CommandServiceError> service_error{};
};

[[nodiscard]] domain::Outcome<SeedRunReport, SeedError>
run_seed_file(service::CommandService& command_service,
              const std::filesystem::path& seed_path);

enum class DemoVerificationError : std::uint8_t {
    UnexpectedCommandCounts,
    UnexpectedRejection,
    UnexpectedState,
    FingerprintFailed,
    UnexpectedFingerprint,
};

struct DemoVerification final {
    std::uint64_t state_version;
    std::uint64_t state_fingerprint;
    std::size_t accepted_count;
    std::size_t rejected_count;
};

[[nodiscard]] domain::Outcome<DemoVerification, DemoVerificationError>
verify_canonical_demo(const SeedRunReport& report, const domain::State& state);

}  // namespace backbook::server
