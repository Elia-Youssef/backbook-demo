#include "backbook/journal/codec.hpp"

#include "backbook/domain/date.hpp"
#include "backbook/domain/fx_terms.hpp"
#include "backbook/domain/id.hpp"
#include "backbook/domain/money.hpp"
#include "backbook/domain/posting_policy.hpp"

#include <bit>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace backbook::journal {
namespace {

constexpr std::size_t frame_header_size = 9U;
constexpr std::size_t frame_crc_size = 4U;
constexpr std::uint8_t booked_event_tag = 1U;
constexpr std::uint8_t confirmed_event_tag = 2U;
constexpr std::uint8_t amended_event_tag = 3U;
constexpr std::uint8_t cancelled_event_tag = 4U;
constexpr std::uint8_t eod_event_tag = 5U;
constexpr std::uint8_t booked_result_tag = 1U;
constexpr std::uint8_t confirmed_result_tag = 2U;
constexpr std::uint8_t amended_result_tag = 3U;
constexpr std::uint8_t cancelled_result_tag = 4U;
constexpr std::uint8_t eod_result_tag = 5U;

template <typename> inline constexpr bool always_false = false;

[[nodiscard]] CodecError error(const CodecErrorCode code,
                               const std::size_t offset,
                               const std::uint64_t detail = 0U) {
    return CodecError{code, offset, detail};
}

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

    void write_bytes(const std::span<const std::uint8_t> bytes) {
        bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
    }

    void write_string(const std::string_view value) {
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    [[nodiscard]] Bytes take() {
        return std::move(bytes_);
    }

private:
    Bytes bytes_;
};

class Reader final {
public:
    explicit Reader(const std::span<const std::uint8_t> bytes)
        : bytes_(bytes) {}

    [[nodiscard]] std::size_t offset() const noexcept {
        return offset_;
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - offset_;
    }

    [[nodiscard]] bool read_u8(std::uint8_t& value) {
        if (remaining() < 1U) {
            return false;
        }
        value = bytes_[offset_];
        ++offset_;
        return true;
    }

    [[nodiscard]] bool read_u16(std::uint16_t& value) {
        if (remaining() < 2U) {
            return false;
        }
        value = static_cast<std::uint16_t>(bytes_[offset_]) |
                static_cast<std::uint16_t>(
                    static_cast<std::uint16_t>(bytes_[offset_ + 1U]) << 8U);
        offset_ += 2U;
        return true;
    }

    [[nodiscard]] bool read_u32(std::uint32_t& value) {
        if (remaining() < 4U) {
            return false;
        }
        value = 0U;
        for (std::uint32_t index = 0U; index < 4U; ++index) {
            value |= static_cast<std::uint32_t>(bytes_[offset_ + index])
                     << (index * 8U);
        }
        offset_ += 4U;
        return true;
    }

    [[nodiscard]] bool read_u64(std::uint64_t& value) {
        if (remaining() < 8U) {
            return false;
        }
        value = 0U;
        for (std::uint32_t index = 0U; index < 8U; ++index) {
            value |= static_cast<std::uint64_t>(bytes_[offset_ + index])
                     << (index * 8U);
        }
        offset_ += 8U;
        return true;
    }

    [[nodiscard]] bool read_i32(std::int32_t& value) {
        std::uint32_t encoded = 0U;
        if (!read_u32(encoded)) {
            return false;
        }
        value = std::bit_cast<std::int32_t>(encoded);
        return true;
    }

    [[nodiscard]] bool read_i64(std::int64_t& value) {
        std::uint64_t encoded = 0U;
        if (!read_u64(encoded)) {
            return false;
        }
        value = std::bit_cast<std::int64_t>(encoded);
        return true;
    }

    [[nodiscard]] bool read_bytes(const std::size_t size,
                                  std::span<const std::uint8_t>& value) {
        if (remaining() < size) {
            return false;
        }
        value = bytes_.subspan(offset_, size);
        offset_ += size;
        return true;
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_{0U};
};

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

[[nodiscard]] domain::Outcome<domain::Currency, CodecError>
read_currency(Reader& reader) {
    const auto start = reader.offset();
    std::uint8_t tag = 0U;
    if (!reader.read_u8(tag)) {
        return domain::Outcome<domain::Currency, CodecError>::failure(
            error(CodecErrorCode::UnexpectedEnd, start));
    }
    switch (tag) {
    case 0U:
        return domain::Outcome<domain::Currency, CodecError>::success(
            domain::Currency::Usd);
    case 1U:
        return domain::Outcome<domain::Currency, CodecError>::success(
            domain::Currency::Jpy);
    case 2U:
        return domain::Outcome<domain::Currency, CodecError>::success(
            domain::Currency::Kwd);
    default:
        return domain::Outcome<domain::Currency, CodecError>::failure(
            error(CodecErrorCode::InvalidEnum, start, tag));
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

[[nodiscard]] domain::Outcome<domain::InstrumentKind, CodecError>
read_instrument_kind(Reader& reader) {
    const auto start = reader.offset();
    std::uint8_t tag = 0U;
    if (!reader.read_u8(tag)) {
        return domain::Outcome<domain::InstrumentKind, CodecError>::failure(
            error(CodecErrorCode::UnexpectedEnd, start));
    }
    switch (tag) {
    case 0U:
        return domain::Outcome<domain::InstrumentKind, CodecError>::success(
            domain::InstrumentKind::FxSpot);
    case 1U:
        return domain::Outcome<domain::InstrumentKind, CodecError>::success(
            domain::InstrumentKind::FxForward);
    default:
        return domain::Outcome<domain::InstrumentKind, CodecError>::failure(
            error(CodecErrorCode::InvalidEnum, start, tag));
    }
}

template <typename IdType> void write_id(Writer& writer, const IdType& id) {
    const auto value = id.value();
    writer.write_u16(static_cast<std::uint16_t>(value.size()));
    writer.write_string(value);
}

template <typename IdType>
[[nodiscard]] domain::Outcome<IdType, CodecError> read_id(Reader& reader) {
    const auto start = reader.offset();
    std::uint16_t size = 0U;
    if (!reader.read_u16(size)) {
        return domain::Outcome<IdType, CodecError>::failure(
            error(CodecErrorCode::UnexpectedEnd, start));
    }
    std::span<const std::uint8_t> encoded;
    if (!reader.read_bytes(size, encoded)) {
        return domain::Outcome<IdType, CodecError>::failure(
            error(CodecErrorCode::UnexpectedEnd, start));
    }
    const std::string text(encoded.begin(), encoded.end());
    auto parsed = IdType::parse(text);
    if (!parsed) {
        return domain::Outcome<IdType, CodecError>::failure(
            error(CodecErrorCode::InvalidId,
                  start,
                  static_cast<std::uint64_t>(parsed.error())));
    }
    return domain::Outcome<IdType, CodecError>::success(
        std::move(parsed).value());
}

void write_date(Writer& writer, const domain::IsoDate& value) {
    writer.write_i32(value.to_epoch_days());
}

[[nodiscard]] domain::Outcome<domain::IsoDate, CodecError>
read_date(Reader& reader) {
    const auto start = reader.offset();
    std::int32_t epoch_days = 0;
    if (!reader.read_i32(epoch_days)) {
        return domain::Outcome<domain::IsoDate, CodecError>::failure(
            error(CodecErrorCode::UnexpectedEnd, start));
    }
    auto value = domain::IsoDate::from_epoch_days(epoch_days);
    if (!value) {
        return domain::Outcome<domain::IsoDate, CodecError>::failure(
            error(CodecErrorCode::InvalidDate,
                  start,
                  static_cast<std::uint64_t>(value.error())));
    }
    return domain::Outcome<domain::IsoDate, CodecError>::success(
        std::move(value).value());
}

void write_money(Writer& writer, const domain::Money& value) {
    write_currency(writer, value.currency());
    writer.write_i64(value.minor_units());
}

[[nodiscard]] domain::Outcome<domain::Money, CodecError>
read_money(Reader& reader) {
    const auto start = reader.offset();
    auto currency = read_currency(reader);
    if (!currency) {
        return domain::Outcome<domain::Money, CodecError>::failure(
            currency.error());
    }
    std::int64_t minor_units = 0;
    if (!reader.read_i64(minor_units)) {
        return domain::Outcome<domain::Money, CodecError>::failure(
            error(CodecErrorCode::UnexpectedEnd, start));
    }
    auto value = domain::Money::from_minor_units(currency.value(), minor_units);
    if (!value) {
        return domain::Outcome<domain::Money, CodecError>::failure(
            error(CodecErrorCode::InvalidMoney,
                  start,
                  static_cast<std::uint64_t>(value.error())));
    }
    return domain::Outcome<domain::Money, CodecError>::success(
        std::move(value).value());
}

void write_terms(Writer& writer, const domain::FxTerms& terms) {
    write_instrument_kind(writer, terms.kind());
    write_date(writer, terms.trade_date());
    write_date(writer, terms.value_date());
    write_money(writer, terms.pay());
    write_money(writer, terms.receive());
}

[[nodiscard]] domain::Outcome<domain::FxTerms, CodecError>
read_terms(Reader& reader) {
    const auto start = reader.offset();
    auto kind = read_instrument_kind(reader);
    if (!kind) {
        return domain::Outcome<domain::FxTerms, CodecError>::failure(
            kind.error());
    }
    auto trade_date = read_date(reader);
    if (!trade_date) {
        return domain::Outcome<domain::FxTerms, CodecError>::failure(
            trade_date.error());
    }
    auto value_date = read_date(reader);
    if (!value_date) {
        return domain::Outcome<domain::FxTerms, CodecError>::failure(
            value_date.error());
    }
    auto pay = read_money(reader);
    if (!pay) {
        return domain::Outcome<domain::FxTerms, CodecError>::failure(
            pay.error());
    }
    auto receive = read_money(reader);
    if (!receive) {
        return domain::Outcome<domain::FxTerms, CodecError>::failure(
            receive.error());
    }
    auto terms = domain::FxTerms::create(kind.value(),
                                         trade_date.value(),
                                         value_date.value(),
                                         pay.value(),
                                         receive.value());
    if (!terms) {
        return domain::Outcome<domain::FxTerms, CodecError>::failure(
            error(CodecErrorCode::InvalidTerms,
                  start,
                  static_cast<std::uint64_t>(terms.error())));
    }
    return domain::Outcome<domain::FxTerms, CodecError>::success(
        std::move(terms).value());
}

void write_confirmation_ids(Writer& writer,
                            const domain::ConfirmationPostingIds& ids) {
    write_id(writer, ids.pay_control_debit);
    write_id(writer, ids.pay_payable_credit);
    write_id(writer, ids.receive_receivable_debit);
    write_id(writer, ids.receive_control_credit);
}

[[nodiscard]] domain::Outcome<domain::ConfirmationPostingIds, CodecError>
read_confirmation_ids(Reader& reader) {
    auto pay_control = read_id<domain::PostingId>(reader);
    if (!pay_control) {
        return domain::Outcome<domain::ConfirmationPostingIds,
                               CodecError>::failure(pay_control.error());
    }
    auto pay_payable = read_id<domain::PostingId>(reader);
    if (!pay_payable) {
        return domain::Outcome<domain::ConfirmationPostingIds,
                               CodecError>::failure(pay_payable.error());
    }
    auto receive_receivable = read_id<domain::PostingId>(reader);
    if (!receive_receivable) {
        return domain::Outcome<domain::ConfirmationPostingIds,
                               CodecError>::failure(receive_receivable.error());
    }
    auto receive_control = read_id<domain::PostingId>(reader);
    if (!receive_control) {
        return domain::Outcome<domain::ConfirmationPostingIds,
                               CodecError>::failure(receive_control.error());
    }
    return domain::Outcome<domain::ConfirmationPostingIds, CodecError>::success(
        domain::ConfirmationPostingIds{std::move(pay_control).value(),
                                       std::move(pay_payable).value(),
                                       std::move(receive_receivable).value(),
                                       std::move(receive_control).value()});
}

void write_reversal_ids(Writer& writer, const domain::ReversalPostingIds& ids) {
    write_id(writer, ids.pay_control_credit);
    write_id(writer, ids.pay_payable_debit);
    write_id(writer, ids.receive_receivable_credit);
    write_id(writer, ids.receive_control_debit);
}

[[nodiscard]] domain::Outcome<domain::ReversalPostingIds, CodecError>
read_reversal_ids(Reader& reader) {
    auto pay_control = read_id<domain::PostingId>(reader);
    if (!pay_control) {
        return domain::Outcome<domain::ReversalPostingIds, CodecError>::failure(
            pay_control.error());
    }
    auto pay_payable = read_id<domain::PostingId>(reader);
    if (!pay_payable) {
        return domain::Outcome<domain::ReversalPostingIds, CodecError>::failure(
            pay_payable.error());
    }
    auto receive_receivable = read_id<domain::PostingId>(reader);
    if (!receive_receivable) {
        return domain::Outcome<domain::ReversalPostingIds, CodecError>::failure(
            receive_receivable.error());
    }
    auto receive_control = read_id<domain::PostingId>(reader);
    if (!receive_control) {
        return domain::Outcome<domain::ReversalPostingIds, CodecError>::failure(
            receive_control.error());
    }
    return domain::Outcome<domain::ReversalPostingIds, CodecError>::success(
        domain::ReversalPostingIds{std::move(pay_control).value(),
                                   std::move(pay_payable).value(),
                                   std::move(receive_receivable).value(),
                                   std::move(receive_control).value()});
}

struct EncodedVariant final {
    std::uint8_t tag;
    Bytes body;
};

[[nodiscard]] EncodedVariant encode_event(const Event& event) {
    return std::visit(
        [](const auto& value) -> EncodedVariant {
            using Value = std::remove_cvref_t<decltype(value)>;
            Writer writer;
            writer.write_u8(event_format_version);
            if constexpr (std::is_same_v<Value, TradeBookedEvent>) {
                write_id(writer, value.trade_id);
                write_id(writer, value.book_id);
                write_id(writer, value.counterparty_id);
                write_id(writer, value.netting_set_id);
                write_terms(writer, value.terms);
                return EncodedVariant{booked_event_tag, writer.take()};
            } else if constexpr (std::is_same_v<Value, TradeConfirmedEvent>) {
                write_id(writer, value.trade_id);
                writer.write_u32(value.expected_version);
                write_confirmation_ids(writer, value.posting_ids);
                return EncodedVariant{confirmed_event_tag, writer.take()};
            } else if constexpr (std::is_same_v<Value, TradeAmendedEvent>) {
                write_id(writer, value.trade_id);
                writer.write_u32(value.expected_version);
                write_terms(writer, value.replacement_terms);
                write_reversal_ids(writer, value.reversal_ids);
                write_confirmation_ids(writer, value.replacement_posting_ids);
                return EncodedVariant{amended_event_tag, writer.take()};
            } else if constexpr (std::is_same_v<Value, TradeCancelledEvent>) {
                write_id(writer, value.trade_id);
                writer.write_u32(value.expected_version);
                writer.write_u8(value.reversal_ids.has_value() ? 1U : 0U);
                if (value.reversal_ids.has_value()) {
                    write_reversal_ids(writer, *value.reversal_ids);
                }
                return EncodedVariant{cancelled_event_tag, writer.take()};
            } else if constexpr (std::is_same_v<Value, EodRunEvent>) {
                write_date(writer, value.as_of_date);
                return EncodedVariant{eod_event_tag, writer.take()};
            } else {
                static_assert(always_false<Value>);
            }
        },
        event);
}

[[nodiscard]] domain::Outcome<Event, CodecError>
decode_event(const std::uint8_t tag,
             const std::span<const std::uint8_t> bytes) {
    Reader reader(bytes);
    std::uint8_t version = 0U;
    if (!reader.read_u8(version)) {
        return domain::Outcome<Event, CodecError>::failure(
            error(CodecErrorCode::UnexpectedEnd, 0U));
    }
    if (version != event_format_version) {
        return domain::Outcome<Event, CodecError>::failure(
            error(CodecErrorCode::UnsupportedEventVersion, 0U, version));
    }

    switch (tag) {
    case booked_event_tag: {
        auto trade_id = read_id<domain::TradeId>(reader);
        if (!trade_id) {
            return domain::Outcome<Event, CodecError>::failure(
                trade_id.error());
        }
        auto book_id = read_id<domain::BookId>(reader);
        if (!book_id) {
            return domain::Outcome<Event, CodecError>::failure(book_id.error());
        }
        auto counterparty_id = read_id<domain::CounterpartyId>(reader);
        if (!counterparty_id) {
            return domain::Outcome<Event, CodecError>::failure(
                counterparty_id.error());
        }
        auto netting_set_id = read_id<domain::NettingSetId>(reader);
        if (!netting_set_id) {
            return domain::Outcome<Event, CodecError>::failure(
                netting_set_id.error());
        }
        auto terms = read_terms(reader);
        if (!terms) {
            return domain::Outcome<Event, CodecError>::failure(terms.error());
        }
        if (reader.remaining() != 0U) {
            return domain::Outcome<Event, CodecError>::failure(
                error(CodecErrorCode::TrailingBytes,
                      reader.offset(),
                      reader.remaining()));
        }
        return domain::Outcome<Event, CodecError>::success(
            TradeBookedEvent{std::move(trade_id).value(),
                             std::move(book_id).value(),
                             std::move(counterparty_id).value(),
                             std::move(netting_set_id).value(),
                             std::move(terms).value()});
    }
    case confirmed_event_tag: {
        auto trade_id = read_id<domain::TradeId>(reader);
        std::uint32_t expected_version = 0U;
        if (!trade_id || !reader.read_u32(expected_version)) {
            return domain::Outcome<Event, CodecError>::failure(
                !trade_id
                    ? trade_id.error()
                    : error(CodecErrorCode::UnexpectedEnd, reader.offset()));
        }
        auto ids = read_confirmation_ids(reader);
        if (!ids) {
            return domain::Outcome<Event, CodecError>::failure(ids.error());
        }
        if (reader.remaining() != 0U) {
            return domain::Outcome<Event, CodecError>::failure(
                error(CodecErrorCode::TrailingBytes,
                      reader.offset(),
                      reader.remaining()));
        }
        return domain::Outcome<Event, CodecError>::success(
            TradeConfirmedEvent{std::move(trade_id).value(),
                                expected_version,
                                std::move(ids).value()});
    }
    case amended_event_tag: {
        auto trade_id = read_id<domain::TradeId>(reader);
        std::uint32_t expected_version = 0U;
        if (!trade_id || !reader.read_u32(expected_version)) {
            return domain::Outcome<Event, CodecError>::failure(
                !trade_id
                    ? trade_id.error()
                    : error(CodecErrorCode::UnexpectedEnd, reader.offset()));
        }
        auto terms = read_terms(reader);
        if (!terms) {
            return domain::Outcome<Event, CodecError>::failure(terms.error());
        }
        auto reversal_ids = read_reversal_ids(reader);
        if (!reversal_ids) {
            return domain::Outcome<Event, CodecError>::failure(
                reversal_ids.error());
        }
        auto replacement_ids = read_confirmation_ids(reader);
        if (!replacement_ids) {
            return domain::Outcome<Event, CodecError>::failure(
                replacement_ids.error());
        }
        if (reader.remaining() != 0U) {
            return domain::Outcome<Event, CodecError>::failure(
                error(CodecErrorCode::TrailingBytes,
                      reader.offset(),
                      reader.remaining()));
        }
        return domain::Outcome<Event, CodecError>::success(
            TradeAmendedEvent{std::move(trade_id).value(),
                              expected_version,
                              std::move(terms).value(),
                              std::move(reversal_ids).value(),
                              std::move(replacement_ids).value()});
    }
    case cancelled_event_tag: {
        auto trade_id = read_id<domain::TradeId>(reader);
        std::uint32_t expected_version = 0U;
        std::uint8_t has_reversal = 0U;
        if (!trade_id || !reader.read_u32(expected_version) ||
            !reader.read_u8(has_reversal)) {
            return domain::Outcome<Event, CodecError>::failure(
                !trade_id
                    ? trade_id.error()
                    : error(CodecErrorCode::UnexpectedEnd, reader.offset()));
        }
        if (has_reversal > 1U) {
            return domain::Outcome<Event, CodecError>::failure(
                error(CodecErrorCode::InvalidOptionalFlag,
                      reader.offset() - 1U,
                      has_reversal));
        }
        std::optional<domain::ReversalPostingIds> reversal_ids;
        if (has_reversal == 1U) {
            auto ids = read_reversal_ids(reader);
            if (!ids) {
                return domain::Outcome<Event, CodecError>::failure(ids.error());
            }
            reversal_ids = std::move(ids).value();
        }
        if (reader.remaining() != 0U) {
            return domain::Outcome<Event, CodecError>::failure(
                error(CodecErrorCode::TrailingBytes,
                      reader.offset(),
                      reader.remaining()));
        }
        return domain::Outcome<Event, CodecError>::success(
            TradeCancelledEvent{std::move(trade_id).value(),
                                expected_version,
                                std::move(reversal_ids)});
    }
    case eod_event_tag: {
        auto as_of_date = read_date(reader);
        if (!as_of_date) {
            return domain::Outcome<Event, CodecError>::failure(
                as_of_date.error());
        }
        if (reader.remaining() != 0U) {
            return domain::Outcome<Event, CodecError>::failure(
                error(CodecErrorCode::TrailingBytes,
                      reader.offset(),
                      reader.remaining()));
        }
        return domain::Outcome<Event, CodecError>::success(
            EodRunEvent{std::move(as_of_date).value()});
    }
    default:
        return domain::Outcome<Event, CodecError>::failure(
            error(CodecErrorCode::InvalidEventTag, 0U, tag));
    }
}

[[nodiscard]] EncodedVariant encode_result(const CommandResult& result) {
    return std::visit(
        [](const auto& value) -> EncodedVariant {
            using Value = std::remove_cvref_t<decltype(value)>;
            Writer writer;
            writer.write_u8(result_format_version);
            if constexpr (std::is_same_v<Value, TradeBookedResult>) {
                write_id(writer, value.trade_id);
                writer.write_u32(value.version);
                writer.write_u64(value.state_version);
                return EncodedVariant{booked_result_tag, writer.take()};
            } else if constexpr (std::is_same_v<Value, TradeConfirmedResult>) {
                write_id(writer, value.trade_id);
                writer.write_u32(value.version);
                writer.write_u64(value.state_version);
                return EncodedVariant{confirmed_result_tag, writer.take()};
            } else if constexpr (std::is_same_v<Value, TradeAmendedResult>) {
                write_id(writer, value.trade_id);
                writer.write_u32(value.superseded_version);
                writer.write_u32(value.replacement_version);
                writer.write_u64(value.state_version);
                return EncodedVariant{amended_result_tag, writer.take()};
            } else if constexpr (std::is_same_v<Value, TradeCancelledResult>) {
                write_id(writer, value.trade_id);
                writer.write_u32(value.version);
                writer.write_u64(value.state_version);
                return EncodedVariant{cancelled_result_tag, writer.take()};
            } else if constexpr (std::is_same_v<Value, EodRunResult>) {
                write_date(writer, value.as_of_date);
                writer.write_u32(value.settled_trade_count);
                writer.write_u64(value.state_version);
                return EncodedVariant{eod_result_tag, writer.take()};
            } else {
                static_assert(always_false<Value>);
            }
        },
        result);
}

[[nodiscard]] domain::Outcome<CommandResult, CodecError>
decode_result(Reader& reader) {
    const auto tag_offset = reader.offset();
    std::uint8_t tag = 0U;
    std::uint8_t version = 0U;
    if (!reader.read_u8(tag) || !reader.read_u8(version)) {
        return domain::Outcome<CommandResult, CodecError>::failure(
            error(CodecErrorCode::UnexpectedEnd, tag_offset));
    }
    if (version != result_format_version) {
        return domain::Outcome<CommandResult, CodecError>::failure(
            error(CodecErrorCode::UnsupportedResultVersion,
                  tag_offset + 1U,
                  version));
    }

    switch (tag) {
    case booked_result_tag:
    case confirmed_result_tag:
    case cancelled_result_tag: {
        auto trade_id = read_id<domain::TradeId>(reader);
        std::uint32_t trade_version = 0U;
        std::uint64_t state_version = 0U;
        if (!trade_id || !reader.read_u32(trade_version) ||
            !reader.read_u64(state_version)) {
            return domain::Outcome<CommandResult, CodecError>::failure(
                !trade_id
                    ? trade_id.error()
                    : error(CodecErrorCode::UnexpectedEnd, reader.offset()));
        }
        if (tag == booked_result_tag) {
            return domain::Outcome<CommandResult, CodecError>::success(
                TradeBookedResult{
                    std::move(trade_id).value(), trade_version, state_version});
        }
        if (tag == confirmed_result_tag) {
            return domain::Outcome<CommandResult, CodecError>::success(
                TradeConfirmedResult{
                    std::move(trade_id).value(), trade_version, state_version});
        }
        return domain::Outcome<CommandResult, CodecError>::success(
            TradeCancelledResult{
                std::move(trade_id).value(), trade_version, state_version});
    }
    case amended_result_tag: {
        auto trade_id = read_id<domain::TradeId>(reader);
        std::uint32_t superseded_version = 0U;
        std::uint32_t replacement_version = 0U;
        std::uint64_t state_version = 0U;
        if (!trade_id || !reader.read_u32(superseded_version) ||
            !reader.read_u32(replacement_version) ||
            !reader.read_u64(state_version)) {
            return domain::Outcome<CommandResult, CodecError>::failure(
                !trade_id
                    ? trade_id.error()
                    : error(CodecErrorCode::UnexpectedEnd, reader.offset()));
        }
        return domain::Outcome<CommandResult, CodecError>::success(
            TradeAmendedResult{std::move(trade_id).value(),
                               superseded_version,
                               replacement_version,
                               state_version});
    }
    case eod_result_tag: {
        auto as_of_date = read_date(reader);
        std::uint32_t settled_trade_count = 0U;
        std::uint64_t state_version = 0U;
        if (!as_of_date || !reader.read_u32(settled_trade_count) ||
            !reader.read_u64(state_version)) {
            return domain::Outcome<CommandResult, CodecError>::failure(
                !as_of_date
                    ? as_of_date.error()
                    : error(CodecErrorCode::UnexpectedEnd, reader.offset()));
        }
        return domain::Outcome<CommandResult, CodecError>::success(EodRunResult{
            std::move(as_of_date).value(), settled_trade_count, state_version});
    }
    default:
        return domain::Outcome<CommandResult, CodecError>::failure(
            error(CodecErrorCode::InvalidResultTag, tag_offset, tag));
    }
}

[[nodiscard]] std::uint32_t
read_u32_at(const std::span<const std::uint8_t> bytes,
            const std::size_t offset) noexcept {
    std::uint32_t value = 0U;
    for (std::uint32_t index = 0U; index < 4U; ++index) {
        value |= static_cast<std::uint32_t>(bytes[offset + index])
                 << (index * 8U);
    }
    return value;
}

}  // namespace

std::uint32_t crc32(const std::span<const std::uint8_t> bytes) noexcept {
    std::uint32_t crc = 0xffffffffU;
    for (const std::uint8_t byte : bytes) {
        crc ^= byte;
        for (std::uint8_t bit = 0U; bit < 8U; ++bit) {
            const std::uint32_t mask =
                0U - static_cast<std::uint32_t>(crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

domain::Outcome<Bytes, CodecError> encode_payload(const CommandBatch& batch) {
    if (batch.canonical_request().size() >
            std::numeric_limits<std::uint32_t>::max() ||
        batch.events().size() > std::numeric_limits<std::uint16_t>::max()) {
        return domain::Outcome<Bytes, CodecError>::failure(
            error(CodecErrorCode::SizeOverflow, 0U));
    }

    Writer writer;
    writer.write_u64(batch.sequence());
    write_id(writer, batch.command_id());
    writer.write_u32(
        static_cast<std::uint32_t>(batch.canonical_request().size()));
    writer.write_bytes(batch.canonical_request());
    writer.write_u16(static_cast<std::uint16_t>(batch.events().size()));

    for (const Event& event : batch.events()) {
        auto encoded = encode_event(event);
        if (encoded.body.size() > std::numeric_limits<std::uint32_t>::max()) {
            return domain::Outcome<Bytes, CodecError>::failure(
                error(CodecErrorCode::SizeOverflow, 0U));
        }
        writer.write_u8(encoded.tag);
        writer.write_u32(static_cast<std::uint32_t>(encoded.body.size()));
        writer.write_bytes(encoded.body);
    }

    auto result = encode_result(batch.result());
    writer.write_u8(result.tag);
    writer.write_bytes(result.body);
    auto payload = writer.take();
    if (payload.size() > maximum_payload_length) {
        return domain::Outcome<Bytes, CodecError>::failure(
            error(CodecErrorCode::PayloadTooLarge, 0U, payload.size()));
    }
    return domain::Outcome<Bytes, CodecError>::success(std::move(payload));
}

domain::Outcome<CommandBatch, CodecError>
decode_payload(const std::span<const std::uint8_t> payload) {
    if (payload.size() > maximum_payload_length) {
        return domain::Outcome<CommandBatch, CodecError>::failure(
            error(CodecErrorCode::PayloadTooLarge, 0U, payload.size()));
    }

    Reader reader(payload);
    std::uint64_t sequence = 0U;
    if (!reader.read_u64(sequence)) {
        return domain::Outcome<CommandBatch, CodecError>::failure(
            error(CodecErrorCode::UnexpectedEnd, reader.offset()));
    }
    auto command_id = read_id<domain::CommandId>(reader);
    if (!command_id) {
        return domain::Outcome<CommandBatch, CodecError>::failure(
            command_id.error());
    }

    std::uint32_t request_size = 0U;
    if (!reader.read_u32(request_size)) {
        return domain::Outcome<CommandBatch, CodecError>::failure(
            error(CodecErrorCode::UnexpectedEnd, reader.offset()));
    }
    std::span<const std::uint8_t> request;
    if (!reader.read_bytes(request_size, request)) {
        return domain::Outcome<CommandBatch, CodecError>::failure(
            error(CodecErrorCode::UnexpectedEnd, reader.offset()));
    }

    std::uint16_t event_count = 0U;
    if (!reader.read_u16(event_count)) {
        return domain::Outcome<CommandBatch, CodecError>::failure(
            error(CodecErrorCode::UnexpectedEnd, reader.offset()));
    }
    std::vector<Event> events;
    events.reserve(event_count);
    for (std::uint16_t index = 0U; index < event_count; ++index) {
        std::uint8_t tag = 0U;
        std::uint32_t body_size = 0U;
        if (!reader.read_u8(tag) || !reader.read_u32(body_size)) {
            return domain::Outcome<CommandBatch, CodecError>::failure(
                error(CodecErrorCode::UnexpectedEnd, reader.offset()));
        }
        std::span<const std::uint8_t> body;
        if (!reader.read_bytes(body_size, body)) {
            return domain::Outcome<CommandBatch, CodecError>::failure(
                error(CodecErrorCode::UnexpectedEnd, reader.offset()));
        }
        auto event = decode_event(tag, body);
        if (!event) {
            auto failure = event.error();
            failure.offset += reader.offset() - body.size();
            return domain::Outcome<CommandBatch, CodecError>::failure(failure);
        }
        events.push_back(std::move(event).value());
    }

    auto result = decode_result(reader);
    if (!result) {
        return domain::Outcome<CommandBatch, CodecError>::failure(
            result.error());
    }
    if (reader.remaining() != 0U) {
        return domain::Outcome<CommandBatch, CodecError>::failure(
            error(CodecErrorCode::TrailingBytes,
                  reader.offset(),
                  reader.remaining()));
    }

    auto batch = CommandBatch::create(sequence,
                                      std::move(command_id).value(),
                                      Bytes(request.begin(), request.end()),
                                      std::move(events),
                                      std::move(result).value());
    if (!batch) {
        return domain::Outcome<CommandBatch, CodecError>::failure(
            error(CodecErrorCode::InvalidBatch,
                  0U,
                  static_cast<std::uint64_t>(batch.error())));
    }
    return domain::Outcome<CommandBatch, CodecError>::success(
        std::move(batch).value());
}

domain::Outcome<Bytes, CodecError> encode_frame(const CommandBatch& batch) {
    auto payload = encode_payload(batch);
    if (!payload) {
        return domain::Outcome<Bytes, CodecError>::failure(payload.error());
    }
    if (payload.value().size() > std::numeric_limits<std::uint32_t>::max()) {
        return domain::Outcome<Bytes, CodecError>::failure(
            error(CodecErrorCode::SizeOverflow, 0U));
    }

    Writer writer;
    // The frame wrapper is deliberately small: magic, version, payload length,
    // canonical payload, and a CRC over that payload.
    writer.write_string("BBK1");
    writer.write_u8(frame_format_version);
    writer.write_u32(static_cast<std::uint32_t>(payload.value().size()));
    writer.write_bytes(payload.value());
    writer.write_u32(crc32(payload.value()));
    return domain::Outcome<Bytes, CodecError>::success(writer.take());
}

domain::Outcome<DecodedFrame, CodecError>
decode_frame(const std::span<const std::uint8_t> bytes) {
    if (bytes.size() < frame_header_size) {
        return domain::Outcome<DecodedFrame, CodecError>::failure(error(
            CodecErrorCode::UnexpectedEnd, bytes.size(), frame_header_size));
    }
    constexpr char magic[] = "BBK1";
    for (std::size_t index = 0U; index < 4U; ++index) {
        if (bytes[index] != static_cast<std::uint8_t>(magic[index])) {
            return domain::Outcome<DecodedFrame, CodecError>::failure(
                error(CodecErrorCode::InvalidMagic, index, bytes[index]));
        }
    }
    if (bytes[4U] != frame_format_version) {
        return domain::Outcome<DecodedFrame, CodecError>::failure(
            error(CodecErrorCode::UnsupportedFrameVersion, 4U, bytes[4U]));
    }

    const std::uint32_t payload_size = read_u32_at(bytes, 5U);
    if (payload_size > maximum_payload_length) {
        return domain::Outcome<DecodedFrame, CodecError>::failure(
            error(CodecErrorCode::PayloadTooLarge, 5U, payload_size));
    }
    const std::uint64_t frame_size =
        static_cast<std::uint64_t>(frame_header_size) + payload_size +
        frame_crc_size;
    if (bytes.size() < frame_size) {
        return domain::Outcome<DecodedFrame, CodecError>::failure(
            error(CodecErrorCode::UnexpectedEnd, bytes.size(), frame_size));
    }

    const auto payload = bytes.subspan(frame_header_size, payload_size);
    const auto stored_crc =
        read_u32_at(bytes, frame_header_size + payload_size);
    const auto computed_crc = crc32(payload);
    if (stored_crc != computed_crc) {
        return domain::Outcome<DecodedFrame, CodecError>::failure(
            error(CodecErrorCode::CrcMismatch,
                  frame_header_size + payload_size,
                  stored_crc));
    }

    auto batch = decode_payload(payload);
    if (!batch) {
        auto failure = batch.error();
        failure.offset += frame_header_size;
        if (failure.code == CodecErrorCode::UnexpectedEnd) {
            failure.code = CodecErrorCode::MalformedPayload;
        }
        return domain::Outcome<DecodedFrame, CodecError>::failure(failure);
    }
    return domain::Outcome<DecodedFrame, CodecError>::success(
        DecodedFrame{std::move(batch).value(), frame_size});
}

domain::Outcome<JournalScanResult, CodecError>
scan_journal(const std::span<const std::uint8_t> bytes) {
    std::vector<CommandBatch> batches;
    std::uint64_t offset = 0U;

    while (offset < bytes.size()) {
        const auto remaining = bytes.subspan(static_cast<std::size_t>(offset));
        auto decoded = decode_frame(remaining);
        if (!decoded) {
            // Only a physically short final frame is a recoverable torn tail.
            // CRC and format failures describe complete corrupt data.
            if (decoded.error().code == CodecErrorCode::UnexpectedEnd) {
                return domain::Outcome<JournalScanResult, CodecError>::success(
                    JournalScanResult{std::move(batches), offset, true});
            }
            auto failure = decoded.error();
            failure.offset += static_cast<std::size_t>(offset);
            return domain::Outcome<JournalScanResult, CodecError>::failure(
                failure);
        }
        auto frame = std::move(decoded).value();
        offset += frame.bytes_consumed;
        batches.push_back(std::move(frame.batch));
    }

    return domain::Outcome<JournalScanResult, CodecError>::success(
        JournalScanResult{std::move(batches), offset, false});
}

}  // namespace backbook::journal
