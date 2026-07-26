#include "backbook/service/command.hpp"

#include "backbook/domain/money.hpp"

#include <bit>
#include <cstddef>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace backbook::service {
namespace {

constexpr std::uint8_t book_trade_tag = 1U;
constexpr std::uint8_t confirm_trade_tag = 2U;
constexpr std::uint8_t amend_trade_tag = 3U;
constexpr std::uint8_t cancel_trade_tag = 4U;
constexpr std::uint8_t run_eod_tag = 5U;

template <typename> inline constexpr bool always_false = false;

class Writer final {
public:
    void write_u8(const std::uint8_t value) {
        bytes_.push_back(value);
    }

    void write_u16(const std::uint16_t value) {
        bytes_.push_back(static_cast<std::uint8_t>(value & 0xffU));
        bytes_.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    }

    void write_u32(const std::uint32_t value) {
        for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
            bytes_.push_back(
                static_cast<std::uint8_t>((value >> shift) & 0xffU));
        }
    }

    void write_u64(const std::uint64_t value) {
        for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
            bytes_.push_back(
                static_cast<std::uint8_t>((value >> shift) & 0xffU));
        }
    }

    void write_i32(const std::int32_t value) {
        write_u32(std::bit_cast<std::uint32_t>(value));
    }

    void write_i64(const std::int64_t value) {
        write_u64(std::bit_cast<std::uint64_t>(value));
    }

    void write_string(const std::string_view value) {
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    [[nodiscard]] journal::Bytes take() {
        return std::move(bytes_);
    }

private:
    journal::Bytes bytes_;
};

class Reader final {
public:
    explicit Reader(const std::span<const std::uint8_t> bytes) noexcept
        : bytes_(bytes) {}

    [[nodiscard]] bool read_u8(std::uint8_t& value) noexcept {
        if (remaining() < 1U) {
            return false;
        }
        value = bytes_[offset_++];
        return true;
    }

    [[nodiscard]] bool read_u16(std::uint16_t& value) noexcept {
        if (remaining() < 2U) {
            return false;
        }
        value = static_cast<std::uint16_t>(bytes_[offset_]) |
                static_cast<std::uint16_t>(
                    static_cast<std::uint16_t>(bytes_[offset_ + 1U]) << 8U);
        offset_ += 2U;
        return true;
    }

    [[nodiscard]] bool skip(const std::size_t count) noexcept {
        if (remaining() < count) {
            return false;
        }
        offset_ += count;
        return true;
    }

    [[nodiscard]] bool read_id(std::span<const std::uint8_t>& value) noexcept {
        std::uint16_t length = 0U;
        if (!read_u16(length) || length == 0U || length > 64U ||
            remaining() < length) {
            return false;
        }
        value = bytes_.subspan(offset_, length);
        offset_ += length;
        return valid_id(value);
    }

    [[nodiscard]] bool finished() const noexcept {
        return offset_ == bytes_.size();
    }

private:
    [[nodiscard]] static bool
    is_initial_id_byte(const std::uint8_t value) noexcept {
        return (value >= static_cast<std::uint8_t>('A') &&
                value <= static_cast<std::uint8_t>('Z')) ||
               (value >= static_cast<std::uint8_t>('a') &&
                value <= static_cast<std::uint8_t>('z')) ||
               (value >= static_cast<std::uint8_t>('0') &&
                value <= static_cast<std::uint8_t>('9'));
    }

    [[nodiscard]] static bool
    valid_id(const std::span<const std::uint8_t> value) noexcept {
        if (value.empty() || !is_initial_id_byte(value.front())) {
            return false;
        }
        for (const auto byte : value.subspan(1U)) {
            if (!is_initial_id_byte(byte) &&
                byte != static_cast<std::uint8_t>('.') &&
                byte != static_cast<std::uint8_t>('_') &&
                byte != static_cast<std::uint8_t>(':') &&
                byte != static_cast<std::uint8_t>('-')) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - offset_;
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t offset_{0U};
};

template <typename IdType> void write_id(Writer& writer, const IdType& id) {
    const auto value = id.value();
    writer.write_u16(static_cast<std::uint16_t>(value.size()));
    writer.write_string(value);
}

void write_currency(Writer& writer, const domain::Currency currency) {
    switch (currency) {
    case domain::Currency::Usd:
        writer.write_u8(0U);
        return;
    case domain::Currency::Jpy:
        writer.write_u8(1U);
        return;
    case domain::Currency::Kwd:
        writer.write_u8(2U);
        return;
    }
}

void write_instrument_kind(Writer& writer, const domain::InstrumentKind kind) {
    switch (kind) {
    case domain::InstrumentKind::FxSpot:
        writer.write_u8(0U);
        return;
    case domain::InstrumentKind::FxForward:
        writer.write_u8(1U);
        return;
    }
}

void write_date(Writer& writer, const domain::IsoDate& value) {
    writer.write_i32(value.to_epoch_days());
}

void write_money(Writer& writer, const domain::Money& value) {
    write_currency(writer, value.currency());
    writer.write_i64(value.minor_units());
}

void write_terms(Writer& writer, const domain::FxTerms& terms) {
    write_instrument_kind(writer, terms.kind());
    write_date(writer, terms.trade_date());
    write_date(writer, terms.value_date());
    write_money(writer, terms.pay());
    write_money(writer, terms.receive());
}

void write_confirmation_ids(Writer& writer,
                            const domain::ConfirmationPostingIds& ids) {
    write_id(writer, ids.pay_control_debit);
    write_id(writer, ids.pay_payable_credit);
    write_id(writer, ids.receive_receivable_debit);
    write_id(writer, ids.receive_control_credit);
}

void write_reversal_ids(Writer& writer, const domain::ReversalPostingIds& ids) {
    write_id(writer, ids.pay_control_credit);
    write_id(writer, ids.pay_payable_debit);
    write_id(writer, ids.receive_receivable_credit);
    write_id(writer, ids.receive_control_debit);
}

void write_command(Writer& writer, const Command& command) {
    std::visit(
        [&writer](const auto& value) {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, BookTradeCommand>) {
                writer.write_u8(book_trade_tag);
                write_id(writer, value.trade_id);
                write_id(writer, value.book_id);
                write_id(writer, value.counterparty_id);
                write_id(writer, value.netting_set_id);
                write_terms(writer, value.terms);
            } else if constexpr (std::is_same_v<Value, ConfirmTradeCommand>) {
                writer.write_u8(confirm_trade_tag);
                write_id(writer, value.trade_id);
                writer.write_u32(value.expected_version);
                write_confirmation_ids(writer, value.posting_ids);
            } else if constexpr (std::is_same_v<Value, AmendTradeCommand>) {
                writer.write_u8(amend_trade_tag);
                write_id(writer, value.trade_id);
                writer.write_u32(value.expected_version);
                write_terms(writer, value.replacement_terms);
                write_reversal_ids(writer, value.reversal_ids);
                write_confirmation_ids(writer, value.replacement_posting_ids);
            } else if constexpr (std::is_same_v<Value, CancelTradeCommand>) {
                writer.write_u8(cancel_trade_tag);
                write_id(writer, value.trade_id);
                writer.write_u32(value.expected_version);
                writer.write_u8(value.reversal_ids.has_value() ? 1U : 0U);
                if (value.reversal_ids.has_value()) {
                    write_reversal_ids(writer, *value.reversal_ids);
                }
            } else if constexpr (std::is_same_v<Value, RunEodCommand>) {
                writer.write_u8(run_eod_tag);
                write_date(writer, value.as_of_date);
            } else {
                static_assert(always_false<Value>);
            }
        },
        command);
}

[[nodiscard]] bool read_id(Reader& reader) {
    std::span<const std::uint8_t> unused;
    return reader.read_id(unused);
}

[[nodiscard]] bool read_money(Reader& reader) {
    std::uint8_t currency = 0U;
    return reader.read_u8(currency) && currency <= 2U &&
           reader.skip(sizeof(std::int64_t));
}

[[nodiscard]] bool read_terms(Reader& reader) {
    std::uint8_t instrument = 0U;
    return reader.read_u8(instrument) && instrument <= 1U &&
           reader.skip(sizeof(std::int32_t) * 2U) && read_money(reader) &&
           read_money(reader);
}

[[nodiscard]] bool read_confirmation_ids(Reader& reader) {
    return read_id(reader) && read_id(reader) && read_id(reader) &&
           read_id(reader);
}

[[nodiscard]] bool read_reversal_ids(Reader& reader) {
    return read_id(reader) && read_id(reader) && read_id(reader) &&
           read_id(reader);
}

[[nodiscard]] bool read_command_body(Reader& reader, const std::uint8_t tag) {
    switch (tag) {
    case book_trade_tag:
        return read_id(reader) && read_id(reader) && read_id(reader) &&
               read_id(reader) && read_terms(reader);
    case confirm_trade_tag:
        return read_id(reader) && reader.skip(sizeof(std::uint32_t)) &&
               read_confirmation_ids(reader);
    case amend_trade_tag:
        return read_id(reader) && reader.skip(sizeof(std::uint32_t)) &&
               read_terms(reader) && read_reversal_ids(reader) &&
               read_confirmation_ids(reader);
    case cancel_trade_tag: {
        std::uint8_t has_reversal_ids = 0U;
        if (!read_id(reader) || !reader.skip(sizeof(std::uint32_t)) ||
            !reader.read_u8(has_reversal_ids) || has_reversal_ids > 1U) {
            return false;
        }
        return has_reversal_ids == 0U || read_reversal_ids(reader);
    }
    case run_eod_tag:
        return reader.skip(sizeof(std::int32_t));
    default:
        return false;
    }
}

}  // namespace

domain::Outcome<journal::Bytes, CommandEncodingError>
canonical_command_bytes(const CommandEnvelope& envelope) {
    if (envelope.command_id.value().size() >
        std::numeric_limits<std::uint16_t>::max()) {
        return domain::Outcome<journal::Bytes, CommandEncodingError>::failure(
            CommandEncodingError::SizeOverflow);
    }

    Writer writer;
    writer.write_u8(command_request_format_version);
    write_id(writer, envelope.command_id);
    write_command(writer, envelope.command);
    return domain::Outcome<journal::Bytes, CommandEncodingError>::success(
        writer.take());
}

domain::Outcome<std::uint8_t, CanonicalCommandRequestError>
validate_canonical_command_request(
    const std::span<const std::uint8_t> bytes,
    const domain::CommandId& expected_command_id) {
    if (bytes.empty()) {
        return domain::Outcome<std::uint8_t, CanonicalCommandRequestError>::
            failure(CanonicalCommandRequestError::Empty);
    }

    Reader reader(bytes);
    std::uint8_t version = 0U;
    if (!reader.read_u8(version)) {
        return domain::Outcome<std::uint8_t, CanonicalCommandRequestError>::
            failure(CanonicalCommandRequestError::Empty);
    }
    if (version != command_request_format_version) {
        return domain::Outcome<std::uint8_t, CanonicalCommandRequestError>::
            failure(CanonicalCommandRequestError::UnsupportedVersion);
    }

    std::span<const std::uint8_t> encoded_command_id;
    if (!reader.read_id(encoded_command_id)) {
        return domain::Outcome<std::uint8_t, CanonicalCommandRequestError>::
            failure(CanonicalCommandRequestError::Malformed);
    }
    const auto expected = expected_command_id.value();
    if (encoded_command_id.size() != expected.size()) {
        return domain::Outcome<std::uint8_t, CanonicalCommandRequestError>::
            failure(CanonicalCommandRequestError::CommandIdMismatch);
    }
    for (std::size_t index = 0U; index < encoded_command_id.size(); ++index) {
        if (encoded_command_id[index] !=
            static_cast<std::uint8_t>(expected[index])) {
            return domain::Outcome<std::uint8_t, CanonicalCommandRequestError>::
                failure(CanonicalCommandRequestError::CommandIdMismatch);
        }
    }

    std::uint8_t tag = 0U;
    if (!reader.read_u8(tag) || !read_command_body(reader, tag) ||
        !reader.finished()) {
        return domain::Outcome<std::uint8_t, CanonicalCommandRequestError>::
            failure(CanonicalCommandRequestError::Malformed);
    }
    return domain::Outcome<std::uint8_t, CanonicalCommandRequestError>::success(
        tag);
}

}  // namespace backbook::service
