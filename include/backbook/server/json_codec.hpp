#pragma once

#include "backbook/domain/money.hpp"
#include "backbook/domain/outcome.hpp"
#include "backbook/domain/state.hpp"
#include "backbook/service/command.hpp"
#include "backbook/service/command_service.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace backbook::server {

struct FieldViolation final {
    std::string field;
    std::string message;

    [[nodiscard]] friend bool operator==(const FieldViolation&,
                                         const FieldViolation&) = default;
};

enum class CommandDecodeErrorCode : std::uint8_t {
    MalformedJson,
    ValidationFailed,
};

struct CommandDecodeError final {
    CommandDecodeErrorCode code;
    std::vector<FieldViolation> violations;
};

enum class ProblemCode : std::uint8_t {
    ValidationFailed,
    NotFound,
    IllegalTransition,
    VersionConflict,
    LimitBreach,
    IdempotencyConflict,
    JournalUnavailable,
    InternalError,
};

struct ProblemDetails final {
    ProblemCode code;
    int status;
    std::string title;
    std::string detail;
    std::vector<FieldViolation> violations{};
    std::optional<std::uint32_t> expected_version{};
    std::optional<std::uint32_t> actual_version{};
    std::vector<std::string> node_path{};
    std::optional<domain::Currency> currency{};
    std::optional<std::int64_t> required_minor_units{};
    std::optional<std::int64_t> remaining_minor_units{};
};

enum class ResponseEncodingError : std::uint8_t {
    FingerprintFailed,
    InvalidState,
};

[[nodiscard]] domain::Outcome<service::CommandEnvelope, CommandDecodeError>
decode_command_request(std::string_view body);

[[nodiscard]] std::string
encode_command_response(const service::CommandReceipt& receipt);

[[nodiscard]] domain::Outcome<std::string, ResponseEncodingError>
encode_state_response(const domain::State& state);

[[nodiscard]] domain::Outcome<std::string, ResponseEncodingError>
encode_ledger_response(const domain::State& state);

[[nodiscard]] domain::Outcome<std::string, ResponseEncodingError>
encode_settlements_response(const domain::State& state);

[[nodiscard]] std::string
encode_problem_response(const ProblemDetails& problem);

[[nodiscard]] ProblemDetails
problem_from_service_error(const service::CommandServiceError& error);

[[nodiscard]] std::string_view problem_code_name(ProblemCode code) noexcept;

}  // namespace backbook::server
