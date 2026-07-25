#pragma once

#include "backbook/domain/outcome.hpp"
#include "backbook/journal/command_batch.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace backbook::journal {

inline constexpr std::uint8_t frame_format_version = 1U;
inline constexpr std::uint8_t event_format_version = 1U;
inline constexpr std::uint8_t result_format_version = 1U;
inline constexpr std::uint32_t maximum_payload_length = 64U * 1024U * 1024U;

enum class CodecErrorCode : std::uint8_t {
    SizeOverflow,
    UnexpectedEnd,
    InvalidMagic,
    UnsupportedFrameVersion,
    UnsupportedEventVersion,
    UnsupportedResultVersion,
    InvalidEventTag,
    InvalidResultTag,
    InvalidEnum,
    InvalidId,
    InvalidDate,
    InvalidMoney,
    InvalidTerms,
    InvalidBatch,
    InvalidOptionalFlag,
    CrcMismatch,
    TrailingBytes,
    MalformedPayload,
    PayloadTooLarge,
};

struct CodecError final {
    CodecErrorCode code;
    std::size_t offset;
    std::uint64_t detail;

    [[nodiscard]] friend bool operator==(const CodecError&,
                                         const CodecError&) = default;
};

struct DecodedFrame final {
    CommandBatch batch;
    std::uint64_t bytes_consumed;

    [[nodiscard]] friend bool operator==(const DecodedFrame&,
                                         const DecodedFrame&) = default;
};

struct JournalScanResult final {
    std::vector<CommandBatch> batches;
    std::uint64_t last_valid_offset;
    bool truncated_tail;

    [[nodiscard]] friend bool operator==(const JournalScanResult&,
                                         const JournalScanResult&) = default;
};

[[nodiscard]] std::uint32_t crc32(std::span<const std::uint8_t> bytes) noexcept;

[[nodiscard]] domain::Outcome<Bytes, CodecError>
encode_payload(const CommandBatch& batch);
[[nodiscard]] domain::Outcome<CommandBatch, CodecError>
decode_payload(std::span<const std::uint8_t> payload);

[[nodiscard]] domain::Outcome<Bytes, CodecError>
encode_frame(const CommandBatch& batch);
[[nodiscard]] domain::Outcome<DecodedFrame, CodecError>
decode_frame(std::span<const std::uint8_t> bytes);

[[nodiscard]] domain::Outcome<JournalScanResult, CodecError>
scan_journal(std::span<const std::uint8_t> bytes);

}  // namespace backbook::journal
