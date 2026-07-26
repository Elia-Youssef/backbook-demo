#include "backbook/journal/fingerprint.hpp"

#include "backbook/domain/ledger.hpp"
#include "backbook/domain/limits.hpp"
#include "backbook/domain/settlement.hpp"
#include "backbook/domain/trade.hpp"

#include <bit>
#include <limits>
#include <optional>
#include <string_view>

namespace backbook::journal {
namespace {

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

    [[nodiscard]] Bytes take() {
        return std::move(bytes_);
    }

private:
    Bytes bytes_;
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

void write_trade_state(Writer& writer, const domain::TradeState state) {
    switch (state) {
    case domain::TradeState::Captured:
        writer.write_u8(0U);
        return;
    case domain::TradeState::Confirmed:
        writer.write_u8(1U);
        return;
    case domain::TradeState::Superseded:
        writer.write_u8(2U);
        return;
    case domain::TradeState::Cancelled:
        writer.write_u8(3U);
        return;
    case domain::TradeState::Settled:
        writer.write_u8(4U);
        return;
    }
}

void write_posting_side(Writer& writer, const domain::PostingSide side) {
    switch (side) {
    case domain::PostingSide::Debit:
        writer.write_u8(0U);
        return;
    case domain::PostingSide::Credit:
        writer.write_u8(1U);
        return;
    }
}

void write_settlement_direction(Writer& writer,
                                const domain::SettlementDirection direction) {
    switch (direction) {
    case domain::SettlementDirection::Outgoing:
        writer.write_u8(0U);
        return;
    case domain::SettlementDirection::Incoming:
        writer.write_u8(1U);
        return;
    }
}

void write_limit_level(Writer& writer, const domain::LimitLevel level) {
    switch (level) {
    case domain::LimitLevel::Group:
        writer.write_u8(0U);
        return;
    case domain::LimitLevel::Counterparty:
        writer.write_u8(1U);
        return;
    case domain::LimitLevel::NettingSet:
        writer.write_u8(2U);
        return;
    case domain::LimitLevel::Book:
        writer.write_u8(3U);
        return;
    }
}

template <typename IdType>
void write_optional_id(Writer& writer, const std::optional<IdType>& value) {
    writer.write_u8(value.has_value() ? 1U : 0U);
    if (value.has_value()) {
        write_id(writer, *value);
    }
}

void write_optional_version(Writer& writer,
                            const std::optional<std::uint32_t> value) {
    writer.write_u8(value.has_value() ? 1U : 0U);
    if (value.has_value()) {
        writer.write_u32(*value);
    }
}

void write_money(Writer& writer, const domain::Money& money) {
    write_currency(writer, money.currency());
    writer.write_i64(money.minor_units());
}

void write_terms(Writer& writer, const domain::FxTerms& terms) {
    write_instrument_kind(writer, terms.kind());
    writer.write_i32(terms.trade_date().to_epoch_days());
    writer.write_i32(terms.value_date().to_epoch_days());
    write_money(writer, terms.pay());
    write_money(writer, terms.receive());
}

void write_trade(Writer& writer, const domain::Trade& trade) {
    write_id(writer, trade.id());
    writer.write_u32(trade.version());
    write_id(writer, trade.book_id());
    write_id(writer, trade.counterparty_id());
    write_id(writer, trade.netting_set_id());
    write_terms(writer, trade.terms());
    write_trade_state(writer, trade.state());
    write_optional_version(writer, trade.supersedes());
    write_optional_version(writer, trade.superseded_by());
}

void write_posting(Writer& writer, const domain::Posting& posting) {
    write_id(writer, posting.id());
    write_id(writer, posting.trade_id());
    writer.write_u32(posting.trade_version());
    writer.write_u32(static_cast<std::uint32_t>(posting.account().size()));
    writer.write_string(posting.account());
    write_posting_side(writer, posting.side());
    write_money(writer, posting.amount());
    write_optional_id(writer, posting.reversal_of());
}

[[nodiscard]] bool exceeds_u32(const std::size_t size) {
    return size > std::numeric_limits<std::uint32_t>::max();
}

[[nodiscard]] bool state_sizes_fit(const domain::State& state) {
    if (exceeds_u32(state.current_versions().size()) ||
        exceeds_u32(state.trade_versions().size()) ||
        exceeds_u32(state.ledger_entries().size()) ||
        exceeds_u32(state.settlements().size()) ||
        exceeds_u32(state.limits().snapshots().size())) {
        return false;
    }
    for (const auto& entry : state.ledger_entries()) {
        if (exceeds_u32(entry.postings().size())) {
            return false;
        }
        for (const auto& posting : entry.postings()) {
            if (exceeds_u32(posting.account().size())) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

domain::Outcome<Bytes, FingerprintError>
canonical_state_bytes(const domain::State& state) {
    if (!state_sizes_fit(state)) {
        return domain::Outcome<Bytes, FingerprintError>::failure(
            FingerprintError::SizeOverflow);
    }

    Writer writer;
    writer.write_u8(state_export_format_version);
    writer.write_u64(state.version());

    writer.write_u32(
        static_cast<std::uint32_t>(state.current_versions().size()));
    for (const auto& [trade_id, version] : state.current_versions()) {
        write_id(writer, trade_id);
        writer.write_u32(version);
    }

    writer.write_u32(static_cast<std::uint32_t>(state.trade_versions().size()));
    for (const auto& [unused, trade] : state.trade_versions()) {
        static_cast<void>(unused);
        write_trade(writer, trade);
    }

    writer.write_u32(static_cast<std::uint32_t>(state.ledger_entries().size()));
    for (const auto& entry : state.ledger_entries()) {
        writer.write_u32(static_cast<std::uint32_t>(entry.postings().size()));
        for (const auto& posting : entry.postings()) {
            write_posting(writer, posting);
        }
    }

    for (const domain::Currency currency : {domain::Currency::Usd,
                                            domain::Currency::Jpy,
                                            domain::Currency::Kwd}) {
        const auto total = state.ledger_totals().total(currency);
        if (!total) {
            return domain::Outcome<Bytes, FingerprintError>::failure(
                FingerprintError::SizeOverflow);
        }
        write_money(writer, total.value());
    }

    const auto balances = state.limits().snapshots();
    writer.write_u32(static_cast<std::uint32_t>(balances.size()));
    for (const auto& balance : balances) {
        write_limit_level(writer, balance.node.level());
        write_optional_id(writer, balance.node.counterparty_id());
        write_optional_id(writer, balance.node.netting_set_id());
        write_optional_id(writer, balance.node.book_id());
        write_currency(writer, balance.currency);
        writer.write_i64(balance.capacity_minor_units);
        writer.write_i64(balance.reserved_minor_units);
    }

    writer.write_u32(static_cast<std::uint32_t>(state.settlements().size()));
    for (const auto& settlement : state.settlements()) {
        write_id(writer, settlement.counterparty_id());
        write_id(writer, settlement.netting_set_id());
        writer.write_i32(settlement.value_date().to_epoch_days());
        write_settlement_direction(writer, settlement.direction());
        write_money(writer, settlement.amount());
    }

    return domain::Outcome<Bytes, FingerprintError>::success(writer.take());
}

std::uint64_t fnv1a64(const std::span<const std::uint8_t> bytes) noexcept {
    std::uint64_t hash = 14'695'981'039'346'656'037ULL;
    for (const std::uint8_t byte : bytes) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= 1'099'511'628'211ULL;
    }
    return hash;
}

domain::Outcome<std::uint64_t, FingerprintError>
state_fingerprint(const domain::State& state) {
    // Hash the canonical export, never the in-memory object representation.
    auto bytes = canonical_state_bytes(state);
    if (!bytes) {
        return domain::Outcome<std::uint64_t, FingerprintError>::failure(
            bytes.error());
    }
    return domain::Outcome<std::uint64_t, FingerprintError>::success(
        fnv1a64(bytes.value()));
}

}  // namespace backbook::journal
