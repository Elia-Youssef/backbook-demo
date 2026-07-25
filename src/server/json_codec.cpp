#include "backbook/server/json_codec.hpp"

#include "backbook/domain/date.hpp"
#include "backbook/domain/fx_terms.hpp"
#include "backbook/domain/id.hpp"
#include "backbook/domain/ledger_totals.hpp"
#include "backbook/journal/fingerprint.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <charconv>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace backbook::server {
namespace {

using Json = nlohmann::json;

constexpr std::size_t maximum_type_length = 32U;
constexpr std::size_t maximum_date_length = 10U;
constexpr std::size_t maximum_version_length = 10U;
constexpr std::size_t maximum_minor_units_length = 20U;

template <typename> inline constexpr bool always_false = false;

[[nodiscard]] std::string child_path(const std::string_view parent,
                                     const std::string_view child) {
    std::string result(parent);
    result.push_back('.');
    result.append(child);
    return result;
}

void add_violation(std::vector<FieldViolation>& violations, std::string field,
                   std::string message) {
    violations.push_back(FieldViolation{std::move(field), std::move(message)});
}

[[nodiscard]] bool
is_allowed(const std::string_view name,
           const std::initializer_list<std::string_view> allowed) {
    for (const auto candidate : allowed) {
        if (candidate == name) {
            return true;
        }
    }
    return false;
}

void reject_unknown_fields(
    const Json& value, const std::string_view path,
    const std::initializer_list<std::string_view> allowed,
    std::vector<FieldViolation>& violations) {
    if (!value.is_object()) {
        add_violation(violations, std::string(path), "must be a JSON object");
        return;
    }
    for (auto member = value.begin(); member != value.end(); ++member) {
        if (!is_allowed(member.key(), allowed)) {
            add_violation(violations, child_path(path, member.key()),
                          "is not a supported field");
        }
    }
}

[[nodiscard]] const Json*
required_member(const Json& object, const std::string_view name,
                const std::string_view path,
                std::vector<FieldViolation>& violations) {
    if (!object.is_object()) {
        return nullptr;
    }
    const auto found = object.find(name);
    if (found == object.end()) {
        add_violation(violations, child_path(path, name), "is required");
        return nullptr;
    }
    return &*found;
}

[[nodiscard]] std::optional<std::string>
required_string(const Json& object, const std::string_view name,
                const std::string_view path, const std::size_t maximum_length,
                std::vector<FieldViolation>& violations) {
    const auto* member = required_member(object, name, path, violations);
    if (member == nullptr) {
        return std::nullopt;
    }
    const auto field = child_path(path, name);
    if (!member->is_string()) {
        add_violation(violations, field, "must be a string");
        return std::nullopt;
    }
    auto result = member->get<std::string>();
    if (result.size() > maximum_length) {
        add_violation(violations, field, "is too long");
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] std::optional<domain::CommandId>
parse_command_id(const Json& object, const std::string_view name,
                 const std::string_view path,
                 std::vector<FieldViolation>& violations) {
    auto text = required_string(object, name, path, 64U, violations);
    if (!text.has_value()) {
        return std::nullopt;
    }
    auto parsed = domain::CommandId::parse(*text);
    if (!parsed) {
        add_violation(violations, child_path(path, name),
                      "must match the documented identifier grammar");
        return std::nullopt;
    }
    return std::move(parsed).value();
}

[[nodiscard]] std::optional<domain::TradeId>
parse_trade_id(const Json& object, const std::string_view name,
               const std::string_view path,
               std::vector<FieldViolation>& violations) {
    auto text = required_string(object, name, path, 64U, violations);
    if (!text.has_value()) {
        return std::nullopt;
    }
    auto parsed = domain::TradeId::parse(*text);
    if (!parsed) {
        add_violation(violations, child_path(path, name),
                      "must match the documented identifier grammar");
        return std::nullopt;
    }
    return std::move(parsed).value();
}

[[nodiscard]] std::optional<domain::BookId>
parse_book_id(const Json& object, const std::string_view name,
              const std::string_view path,
              std::vector<FieldViolation>& violations) {
    auto text = required_string(object, name, path, 64U, violations);
    if (!text.has_value()) {
        return std::nullopt;
    }
    auto parsed = domain::BookId::parse(*text);
    if (!parsed) {
        add_violation(violations, child_path(path, name),
                      "must match the documented identifier grammar");
        return std::nullopt;
    }
    return std::move(parsed).value();
}

[[nodiscard]] std::optional<domain::CounterpartyId>
parse_counterparty_id(const Json& object, const std::string_view name,
                      const std::string_view path,
                      std::vector<FieldViolation>& violations) {
    auto text = required_string(object, name, path, 64U, violations);
    if (!text.has_value()) {
        return std::nullopt;
    }
    auto parsed = domain::CounterpartyId::parse(*text);
    if (!parsed) {
        add_violation(violations, child_path(path, name),
                      "must match the documented identifier grammar");
        return std::nullopt;
    }
    return std::move(parsed).value();
}

[[nodiscard]] std::optional<domain::NettingSetId>
parse_netting_set_id(const Json& object, const std::string_view name,
                     const std::string_view path,
                     std::vector<FieldViolation>& violations) {
    auto text = required_string(object, name, path, 64U, violations);
    if (!text.has_value()) {
        return std::nullopt;
    }
    auto parsed = domain::NettingSetId::parse(*text);
    if (!parsed) {
        add_violation(violations, child_path(path, name),
                      "must match the documented identifier grammar");
        return std::nullopt;
    }
    return std::move(parsed).value();
}

[[nodiscard]] std::optional<domain::PostingId>
parse_posting_id(const Json& object, const std::string_view name,
                 const std::string_view path,
                 std::vector<FieldViolation>& violations) {
    auto text = required_string(object, name, path, 64U, violations);
    if (!text.has_value()) {
        return std::nullopt;
    }
    auto parsed = domain::PostingId::parse(*text);
    if (!parsed) {
        add_violation(violations, child_path(path, name),
                      "must match the documented identifier grammar");
        return std::nullopt;
    }
    return std::move(parsed).value();
}

[[nodiscard]] bool
is_canonical_unsigned(const std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }
    if (value == "0") {
        return true;
    }
    if (value.front() < '1' || value.front() > '9') {
        return false;
    }
    for (const char character : value) {
        if (character < '0' || character > '9') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool is_canonical_signed(const std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }
    if (value.front() != '-') {
        return is_canonical_unsigned(value);
    }
    if (value.size() < 2U || value[1U] == '0') {
        return false;
    }
    return is_canonical_unsigned(value.substr(1U));
}

[[nodiscard]] std::optional<std::uint32_t>
parse_expected_version(const Json& object,
                       std::vector<FieldViolation>& violations) {
    auto text = required_string(object, "expectedVersion", "$",
                                maximum_version_length, violations);
    if (!text.has_value()) {
        return std::nullopt;
    }
    if (!is_canonical_unsigned(*text)) {
        add_violation(violations, "$.expectedVersion",
                      "must be a canonical unsigned decimal string");
        return std::nullopt;
    }
    std::uint32_t version = 0U;
    const auto parsed =
        std::from_chars(text->data(), text->data() + text->size(), version);
    if (parsed.ec != std::errc{} || parsed.ptr != text->data() + text->size() ||
        version == 0U) {
        add_violation(violations, "$.expectedVersion",
                      "must be between 1 and 4294967295");
        return std::nullopt;
    }
    return version;
}

[[nodiscard]] std::optional<domain::Currency>
parse_currency(const std::string_view text, const std::string& path,
               std::vector<FieldViolation>& violations) {
    if (text == "USD") {
        return domain::Currency::Usd;
    }
    if (text == "JPY") {
        return domain::Currency::Jpy;
    }
    if (text == "KWD") {
        return domain::Currency::Kwd;
    }
    add_violation(violations, path, "must be one of USD, JPY, or KWD");
    return std::nullopt;
}

[[nodiscard]] std::optional<domain::Money>
parse_money(const Json& value, const std::string_view path,
            std::vector<FieldViolation>& violations) {
    reject_unknown_fields(value, path, {"currency", "minorUnits"}, violations);
    if (!value.is_object()) {
        return std::nullopt;
    }
    const auto currency_text =
        required_string(value, "currency", path, 3U, violations);
    const auto units_text = required_string(
        value, "minorUnits", path, maximum_minor_units_length, violations);
    if (!currency_text.has_value() || !units_text.has_value()) {
        return std::nullopt;
    }
    const auto currency = parse_currency(
        *currency_text, child_path(path, "currency"), violations);
    if (!is_canonical_signed(*units_text)) {
        add_violation(violations, child_path(path, "minorUnits"),
                      "must be a canonical signed decimal string");
        return std::nullopt;
    }
    std::int64_t minor_units = 0;
    const auto parsed =
        std::from_chars(units_text->data(),
                        units_text->data() + units_text->size(), minor_units);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != units_text->data() + units_text->size()) {
        add_violation(violations, child_path(path, "minorUnits"),
                      "is outside the signed 64-bit range");
        return std::nullopt;
    }
    if (!currency.has_value()) {
        return std::nullopt;
    }
    auto money = domain::Money::from_minor_units(*currency, minor_units);
    if (!money) {
        add_violation(violations, std::string(path),
                      "contains an unsupported money value");
        return std::nullopt;
    }
    return std::move(money).value();
}

[[nodiscard]] std::optional<domain::IsoDate>
parse_date(const Json& object, const std::string_view name,
           const std::string_view path,
           std::vector<FieldViolation>& violations) {
    auto text =
        required_string(object, name, path, maximum_date_length, violations);
    if (!text.has_value()) {
        return std::nullopt;
    }
    auto parsed = domain::IsoDate::parse(*text);
    if (!parsed) {
        add_violation(violations, child_path(path, name),
                      "must be a real ISO date in YYYY-MM-DD form");
        return std::nullopt;
    }
    return std::move(parsed).value();
}

[[nodiscard]] std::optional<domain::InstrumentKind>
parse_instrument_kind(const Json& object, const std::string_view path,
                      std::vector<FieldViolation>& violations) {
    const auto kind = required_string(object, "kind", path, 16U, violations);
    if (!kind.has_value()) {
        return std::nullopt;
    }
    if (*kind == "FX_SPOT") {
        return domain::InstrumentKind::FxSpot;
    }
    if (*kind == "FX_FORWARD") {
        return domain::InstrumentKind::FxForward;
    }
    add_violation(violations, child_path(path, "kind"),
                  "must be FX_SPOT or FX_FORWARD");
    return std::nullopt;
}

[[nodiscard]] std::optional<domain::FxTerms>
parse_terms(const Json& value, const std::string_view path,
            std::vector<FieldViolation>& violations) {
    reject_unknown_fields(value, path,
                          {"kind", "tradeDate", "valueDate", "pay", "receive"},
                          violations);
    if (!value.is_object()) {
        return std::nullopt;
    }
    const auto kind = parse_instrument_kind(value, path, violations);
    const auto trade_date = parse_date(value, "tradeDate", path, violations);
    const auto value_date = parse_date(value, "valueDate", path, violations);
    const auto* pay_json = required_member(value, "pay", path, violations);
    const auto* receive_json =
        required_member(value, "receive", path, violations);
    const auto pay =
        pay_json == nullptr
            ? std::optional<domain::Money>{}
            : parse_money(*pay_json, child_path(path, "pay"), violations);
    const auto receive =
        receive_json == nullptr
            ? std::optional<domain::Money>{}
            : parse_money(*receive_json, child_path(path, "receive"),
                          violations);
    if (!kind.has_value() || !trade_date.has_value() ||
        !value_date.has_value() || !pay.has_value() || !receive.has_value()) {
        return std::nullopt;
    }
    auto terms = domain::FxTerms::create(*kind, *trade_date, *value_date, *pay,
                                         *receive);
    if (!terms) {
        add_violation(violations, std::string(path),
                      "contains invalid FX cashflow terms");
        return std::nullopt;
    }
    return std::move(terms).value();
}

[[nodiscard]] std::optional<domain::ConfirmationPostingIds>
parse_confirmation_ids(const Json& value, const std::string_view path,
                       std::vector<FieldViolation>& violations) {
    reject_unknown_fields(value, path,
                          {"payControlDebit", "payPayableCredit",
                           "receiveReceivableDebit", "receiveControlCredit"},
                          violations);
    if (!value.is_object()) {
        return std::nullopt;
    }
    auto pay_control =
        parse_posting_id(value, "payControlDebit", path, violations);
    auto pay_payable =
        parse_posting_id(value, "payPayableCredit", path, violations);
    auto receive_receivable =
        parse_posting_id(value, "receiveReceivableDebit", path, violations);
    auto receive_control =
        parse_posting_id(value, "receiveControlCredit", path, violations);
    if (!pay_control.has_value() || !pay_payable.has_value() ||
        !receive_receivable.has_value() || !receive_control.has_value()) {
        return std::nullopt;
    }
    return domain::ConfirmationPostingIds{
        std::move(*pay_control), std::move(*pay_payable),
        std::move(*receive_receivable), std::move(*receive_control)};
}

[[nodiscard]] std::optional<domain::ReversalPostingIds>
parse_reversal_ids(const Json& value, const std::string_view path,
                   std::vector<FieldViolation>& violations) {
    reject_unknown_fields(value, path,
                          {"payControlCredit", "payPayableDebit",
                           "receiveReceivableCredit", "receiveControlDebit"},
                          violations);
    if (!value.is_object()) {
        return std::nullopt;
    }
    auto pay_control =
        parse_posting_id(value, "payControlCredit", path, violations);
    auto pay_payable =
        parse_posting_id(value, "payPayableDebit", path, violations);
    auto receive_receivable =
        parse_posting_id(value, "receiveReceivableCredit", path, violations);
    auto receive_control =
        parse_posting_id(value, "receiveControlDebit", path, violations);
    if (!pay_control.has_value() || !pay_payable.has_value() ||
        !receive_receivable.has_value() || !receive_control.has_value()) {
        return std::nullopt;
    }
    return domain::ReversalPostingIds{
        std::move(*pay_control), std::move(*pay_payable),
        std::move(*receive_receivable), std::move(*receive_control)};
}

void reject_expected_version(const Json& document,
                             std::vector<FieldViolation>& violations) {
    if (document.contains("expectedVersion")) {
        add_violation(violations, "$.expectedVersion",
                      "is not valid for this command type");
    }
}

[[nodiscard]] std::optional<service::Command>
parse_book_command(const Json& document, const Json& payload,
                   std::vector<FieldViolation>& violations) {
    reject_expected_version(document, violations);
    reject_unknown_fields(
        payload, "$.payload",
        {"tradeId", "bookId", "counterpartyId", "nettingSetId", "terms"},
        violations);
    if (!payload.is_object()) {
        return std::nullopt;
    }
    auto trade_id = parse_trade_id(payload, "tradeId", "$.payload", violations);
    auto book_id = parse_book_id(payload, "bookId", "$.payload", violations);
    auto counterparty_id = parse_counterparty_id(payload, "counterpartyId",
                                                 "$.payload", violations);
    auto netting_set_id =
        parse_netting_set_id(payload, "nettingSetId", "$.payload", violations);
    const auto* terms_json =
        required_member(payload, "terms", "$.payload", violations);
    auto terms = terms_json == nullptr
                     ? std::optional<domain::FxTerms>{}
                     : parse_terms(*terms_json, "$.payload.terms", violations);
    if (!trade_id.has_value() || !book_id.has_value() ||
        !counterparty_id.has_value() || !netting_set_id.has_value() ||
        !terms.has_value()) {
        return std::nullopt;
    }
    return service::BookTradeCommand{
        std::move(*trade_id), std::move(*book_id), std::move(*counterparty_id),
        std::move(*netting_set_id), std::move(*terms)};
}

[[nodiscard]] std::optional<service::Command>
parse_confirm_command(const Json& document, const Json& payload,
                      std::vector<FieldViolation>& violations) {
    reject_unknown_fields(payload, "$.payload", {"tradeId", "postingIds"},
                          violations);
    if (!payload.is_object()) {
        return std::nullopt;
    }
    auto version = parse_expected_version(document, violations);
    auto trade_id = parse_trade_id(payload, "tradeId", "$.payload", violations);
    const auto* posting_json =
        required_member(payload, "postingIds", "$.payload", violations);
    auto posting_ids =
        posting_json == nullptr
            ? std::optional<domain::ConfirmationPostingIds>{}
            : parse_confirmation_ids(*posting_json, "$.payload.postingIds",
                                     violations);
    if (!version.has_value() || !trade_id.has_value() ||
        !posting_ids.has_value()) {
        return std::nullopt;
    }
    return service::ConfirmTradeCommand{std::move(*trade_id), *version,
                                        std::move(*posting_ids)};
}

[[nodiscard]] std::optional<service::Command>
parse_amend_command(const Json& document, const Json& payload,
                    std::vector<FieldViolation>& violations) {
    reject_unknown_fields(payload, "$.payload",
                          {"tradeId", "replacementTerms", "reversalPostingIds",
                           "replacementPostingIds"},
                          violations);
    if (!payload.is_object()) {
        return std::nullopt;
    }
    auto version = parse_expected_version(document, violations);
    auto trade_id = parse_trade_id(payload, "tradeId", "$.payload", violations);
    const auto* terms_json =
        required_member(payload, "replacementTerms", "$.payload", violations);
    const auto* reversal_json =
        required_member(payload, "reversalPostingIds", "$.payload", violations);
    const auto* replacement_json = required_member(
        payload, "replacementPostingIds", "$.payload", violations);
    auto terms = terms_json == nullptr
                     ? std::optional<domain::FxTerms>{}
                     : parse_terms(*terms_json, "$.payload.replacementTerms",
                                   violations);
    auto reversal_ids =
        reversal_json == nullptr
            ? std::optional<domain::ReversalPostingIds>{}
            : parse_reversal_ids(*reversal_json, "$.payload.reversalPostingIds",
                                 violations);
    auto replacement_ids =
        replacement_json == nullptr
            ? std::optional<domain::ConfirmationPostingIds>{}
            : parse_confirmation_ids(*replacement_json,
                                     "$.payload.replacementPostingIds",
                                     violations);
    if (!version.has_value() || !trade_id.has_value() || !terms.has_value() ||
        !reversal_ids.has_value() || !replacement_ids.has_value()) {
        return std::nullopt;
    }
    return service::AmendTradeCommand{
        std::move(*trade_id), *version, std::move(*terms),
        std::move(*reversal_ids), std::move(*replacement_ids)};
}

[[nodiscard]] std::optional<service::Command>
parse_cancel_command(const Json& document, const Json& payload,
                     std::vector<FieldViolation>& violations) {
    reject_unknown_fields(payload, "$.payload",
                          {"tradeId", "reversalPostingIds"}, violations);
    if (!payload.is_object()) {
        return std::nullopt;
    }
    auto version = parse_expected_version(document, violations);
    auto trade_id = parse_trade_id(payload, "tradeId", "$.payload", violations);
    std::optional<domain::ReversalPostingIds> reversal_ids;
    const auto reversal = payload.find("reversalPostingIds");
    if (reversal != payload.end()) {
        reversal_ids = parse_reversal_ids(
            *reversal, "$.payload.reversalPostingIds", violations);
    }
    if (!version.has_value() || !trade_id.has_value() ||
        (reversal != payload.end() && !reversal_ids.has_value())) {
        return std::nullopt;
    }
    return service::CancelTradeCommand{std::move(*trade_id), *version,
                                       std::move(reversal_ids)};
}

[[nodiscard]] std::optional<service::Command>
parse_eod_command(const Json& document, const Json& payload,
                  std::vector<FieldViolation>& violations) {
    reject_expected_version(document, violations);
    reject_unknown_fields(payload, "$.payload", {"asOfDate"}, violations);
    if (!payload.is_object()) {
        return std::nullopt;
    }
    auto date = parse_date(payload, "asOfDate", "$.payload", violations);
    if (!date.has_value()) {
        return std::nullopt;
    }
    return service::RunEodCommand{*date};
}

[[nodiscard]] std::string_view
currency_text(const domain::Currency currency) noexcept {
    switch (currency) {
    case domain::Currency::Usd:
        return "USD";
    case domain::Currency::Jpy:
        return "JPY";
    case domain::Currency::Kwd:
        return "KWD";
    }
    return "";
}

[[nodiscard]] std::string_view
instrument_text(const domain::InstrumentKind kind) noexcept {
    switch (kind) {
    case domain::InstrumentKind::FxSpot:
        return "FX_SPOT";
    case domain::InstrumentKind::FxForward:
        return "FX_FORWARD";
    }
    return "";
}

[[nodiscard]] std::string_view
trade_state_text(const domain::TradeState state) noexcept {
    switch (state) {
    case domain::TradeState::Captured:
        return "CAPTURED";
    case domain::TradeState::Confirmed:
        return "CONFIRMED";
    case domain::TradeState::Superseded:
        return "SUPERSEDED";
    case domain::TradeState::Cancelled:
        return "CANCELLED";
    case domain::TradeState::Settled:
        return "SETTLED";
    }
    return "";
}

[[nodiscard]] std::string_view
posting_side_text(const domain::PostingSide side) noexcept {
    switch (side) {
    case domain::PostingSide::Debit:
        return "DEBIT";
    case domain::PostingSide::Credit:
        return "CREDIT";
    }
    return "";
}

[[nodiscard]] std::string_view settlement_direction_text(
    const domain::SettlementDirection direction) noexcept {
    switch (direction) {
    case domain::SettlementDirection::Outgoing:
        return "OUTGOING";
    case domain::SettlementDirection::Incoming:
        return "INCOMING";
    }
    return "";
}

[[nodiscard]] std::string_view
limit_level_text(const domain::LimitLevel level) noexcept {
    switch (level) {
    case domain::LimitLevel::Group:
        return "GROUP";
    case domain::LimitLevel::Counterparty:
        return "COUNTERPARTY";
    case domain::LimitLevel::NettingSet:
        return "NETTING_SET";
    case domain::LimitLevel::Book:
        return "BOOK";
    }
    return "";
}

[[nodiscard]] Json money_json(const domain::Money& money) {
    return Json{{"currency", currency_text(money.currency())},
                {"minorUnits", std::to_string(money.minor_units())}};
}

[[nodiscard]] Json money_json(const domain::Currency currency,
                              const std::int64_t minor_units) {
    return Json{{"currency", currency_text(currency)},
                {"minorUnits", std::to_string(minor_units)}};
}

[[nodiscard]] Json terms_json(const domain::FxTerms& terms) {
    return Json{{"kind", instrument_text(terms.kind())},
                {"tradeDate", terms.trade_date().to_string()},
                {"valueDate", terms.value_date().to_string()},
                {"pay", money_json(terms.pay())},
                {"receive", money_json(terms.receive())}};
}

[[nodiscard]] std::string fingerprint_text(const std::uint64_t value) {
    std::array<char, 16U> digits{};
    digits.fill('0');
    std::array<char, 16U> encoded{};
    const auto result = std::to_chars(
        encoded.data(), encoded.data() + encoded.size(), value, 16);
    const auto size = static_cast<std::size_t>(result.ptr - encoded.data());
    const auto offset = digits.size() - size;
    for (std::size_t index = 0U; index < size; ++index) {
        digits[offset + index] = encoded[index];
    }
    return "0x" + std::string(digits.data(), digits.size());
}

[[nodiscard]] domain::Outcome<Json, ResponseEncodingError>
read_envelope(const domain::State& state, Json data) {
    const auto fingerprint = journal::state_fingerprint(state);
    if (!fingerprint) {
        return domain::Outcome<Json, ResponseEncodingError>::failure(
            ResponseEncodingError::FingerprintFailed);
    }
    return domain::Outcome<Json, ResponseEncodingError>::success(
        Json{{"stateVersion", std::to_string(state.version())},
             {"stateFingerprint", fingerprint_text(fingerprint.value())},
             {"data", std::move(data)}});
}

[[nodiscard]] Json command_result_json(const journal::CommandResult& result) {
    return std::visit(
        [](const auto& value) -> Json {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, journal::TradeBookedResult>) {
                return Json{
                    {"type", "TRADE_BOOKED"},
                    {"tradeId", value.trade_id.value()},
                    {"version", std::to_string(value.version)},
                    {"stateVersion", std::to_string(value.state_version)}};
            } else if constexpr (std::is_same_v<
                                     Value, journal::TradeConfirmedResult>) {
                return Json{
                    {"type", "TRADE_CONFIRMED"},
                    {"tradeId", value.trade_id.value()},
                    {"version", std::to_string(value.version)},
                    {"stateVersion", std::to_string(value.state_version)}};
            } else if constexpr (std::is_same_v<Value,
                                                journal::TradeAmendedResult>) {
                return Json{
                    {"type", "TRADE_AMENDED"},
                    {"tradeId", value.trade_id.value()},
                    {"supersededVersion",
                     std::to_string(value.superseded_version)},
                    {"replacementVersion",
                     std::to_string(value.replacement_version)},
                    {"stateVersion", std::to_string(value.state_version)}};
            } else if constexpr (std::is_same_v<
                                     Value, journal::TradeCancelledResult>) {
                return Json{
                    {"type", "TRADE_CANCELLED"},
                    {"tradeId", value.trade_id.value()},
                    {"version", std::to_string(value.version)},
                    {"stateVersion", std::to_string(value.state_version)}};
            } else if constexpr (std::is_same_v<Value, journal::EodRunResult>) {
                return Json{
                    {"type", "EOD_RUN"},
                    {"asOfDate", value.as_of_date.to_string()},
                    {"settledTradeCount",
                     std::to_string(value.settled_trade_count)},
                    {"stateVersion", std::to_string(value.state_version)}};
            } else {
                static_assert(always_false<Value>);
            }
        },
        result);
}

[[nodiscard]] ProblemDetails internal_problem() {
    return ProblemDetails{ProblemCode::InternalError, 500, "Internal error",
                          "The request could not be completed."};
}

[[nodiscard]] ProblemDetails validation_problem(const std::string& detail) {
    return ProblemDetails{ProblemCode::ValidationFailed, 422,
                          "Validation failed", detail};
}

[[nodiscard]] ProblemDetails
state_problem(const domain::StateError& state_error) {
    switch (state_error.code) {
    case domain::StateErrorCode::TradeNotFound:
        return ProblemDetails{ProblemCode::NotFound, 404, "Trade not found",
                              "The requested trade does not exist."};
    case domain::StateErrorCode::VersionConflict: {
        auto problem = ProblemDetails{
            ProblemCode::VersionConflict, 409, "Version conflict",
            "The expected trade version does not match the current version."};
        problem.expected_version = state_error.expected_version;
        problem.actual_version = state_error.actual_version;
        return problem;
    }
    case domain::StateErrorCode::IllegalTransition:
    case domain::StateErrorCode::DuplicateTradeId:
        return ProblemDetails{ProblemCode::IllegalTransition, 409,
                              "Illegal transition",
                              "The command is not valid for the trade's "
                              "current lifecycle state."};
    case domain::StateErrorCode::LimitFailure:
        if (state_error.limit_error.has_value() &&
            state_error.limit_error->code == domain::LimitErrorCode::Breach) {
            const auto& breach = *state_error.limit_error;
            auto problem = ProblemDetails{
                ProblemCode::LimitBreach, 409, "Settlement limit breached",
                "The outgoing cashflow exceeds remaining settlement headroom."};
            problem.node_path = breach.node.path_components();
            problem.currency = breach.currency;
            problem.required_minor_units = breach.required_minor_units;
            problem.remaining_minor_units = breach.remaining_minor_units;
            return problem;
        }
        return validation_problem(
            "The command references an invalid settlement-limit path.");
    case domain::StateErrorCode::AmendmentFailure:
    case domain::StateErrorCode::PostingPolicyFailure:
    case domain::StateErrorCode::DuplicatePostingId:
    case domain::StateErrorCode::MissingConfirmationEntry:
    case domain::StateErrorCode::MissingReversalIds:
    case domain::StateErrorCode::UnexpectedReversalIds:
        return validation_problem(
            "The command is inconsistent with the current trade.");
    case domain::StateErrorCode::LedgerTotalsFailure:
    case domain::StateErrorCode::SettlementFailure:
    case domain::StateErrorCode::StateVersionOverflow:
        return internal_problem();
    }
    return internal_problem();
}

}  // namespace

domain::Outcome<service::CommandEnvelope, CommandDecodeError>
decode_command_request(const std::string_view body) {
    const auto document = Json::parse(body.begin(), body.end(), nullptr, false);
    if (document.is_discarded()) {
        return domain::Outcome<service::CommandEnvelope,
                               CommandDecodeError>::failure(CommandDecodeError{
            CommandDecodeErrorCode::MalformedJson, {}});
    }

    std::vector<FieldViolation> violations;
    reject_unknown_fields(document, "$",
                          {"commandId", "type", "expectedVersion", "payload"},
                          violations);
    if (!document.is_object()) {
        return domain::Outcome<service::CommandEnvelope,
                               CommandDecodeError>::failure(CommandDecodeError{
            CommandDecodeErrorCode::ValidationFailed, std::move(violations)});
    }

    auto command_id = parse_command_id(document, "commandId", "$", violations);
    const auto type =
        required_string(document, "type", "$", maximum_type_length, violations);
    const auto* payload = required_member(document, "payload", "$", violations);
    std::optional<service::Command> command;
    if (type.has_value() && payload != nullptr) {
        if (*type == "BOOK_TRADE") {
            command = parse_book_command(document, *payload, violations);
        } else if (*type == "CONFIRM_TRADE") {
            command = parse_confirm_command(document, *payload, violations);
        } else if (*type == "AMEND_TRADE") {
            command = parse_amend_command(document, *payload, violations);
        } else if (*type == "CANCEL_TRADE") {
            command = parse_cancel_command(document, *payload, violations);
        } else if (*type == "RUN_EOD") {
            command = parse_eod_command(document, *payload, violations);
        } else {
            add_violation(violations, "$.type",
                          "is not a supported command type");
        }
    }

    if (!violations.empty() || !command_id.has_value() ||
        !command.has_value()) {
        return domain::Outcome<service::CommandEnvelope,
                               CommandDecodeError>::failure(CommandDecodeError{
            CommandDecodeErrorCode::ValidationFailed, std::move(violations)});
    }
    return domain::Outcome<service::CommandEnvelope, CommandDecodeError>::
        success(service::CommandEnvelope{std::move(*command_id),
                                         std::move(*command)});
}

std::string encode_command_response(const service::CommandReceipt& receipt) {
    return Json{{"idempotentReplay", receipt.idempotent_replay},
                {"result", command_result_json(receipt.result)}}
        .dump();
}

domain::Outcome<std::string, ResponseEncodingError>
encode_state_response(const domain::State& state) {
    Json trades = Json::array();
    for (const auto& [unused, trade] : state.trade_versions()) {
        static_cast<void>(unused);
        Json item{{"tradeId", trade.id().value()},
                  {"version", std::to_string(trade.version())},
                  {"state", trade_state_text(trade.state())},
                  {"bookId", trade.book_id().value()},
                  {"counterpartyId", trade.counterparty_id().value()},
                  {"nettingSetId", trade.netting_set_id().value()},
                  {"terms", terms_json(trade.terms())},
                  {"supersedes", nullptr},
                  {"supersededBy", nullptr}};
        if (trade.supersedes().has_value()) {
            item["supersedes"] = std::to_string(*trade.supersedes());
        }
        if (trade.superseded_by().has_value()) {
            item["supersededBy"] = std::to_string(*trade.superseded_by());
        }
        trades.push_back(std::move(item));
    }

    Json limits = Json::array();
    for (const auto& balance : state.limits().snapshots()) {
        limits.push_back(
            Json{{"level", limit_level_text(balance.node.level())},
                 {"nodePath", balance.node.path_components()},
                 {"currency", currency_text(balance.currency)},
                 {"capacity",
                  money_json(balance.currency, balance.capacity_minor_units)},
                 {"reserved",
                  money_json(balance.currency, balance.reserved_minor_units)},
                 {"headroom", money_json(balance.currency,
                                         balance.capacity_minor_units -
                                             balance.reserved_minor_units)}});
    }

    auto envelope = read_envelope(state, Json{{"trades", std::move(trades)},
                                              {"limits", std::move(limits)}});
    if (!envelope) {
        return domain::Outcome<std::string, ResponseEncodingError>::failure(
            envelope.error());
    }
    return domain::Outcome<std::string, ResponseEncodingError>::success(
        envelope.value().dump());
}

domain::Outcome<std::string, ResponseEncodingError>
encode_ledger_response(const domain::State& state) {
    Json totals = Json::array();
    bool balanced = true;
    for (const auto currency : {domain::Currency::Usd, domain::Currency::Jpy,
                                domain::Currency::Kwd}) {
        const auto total = state.ledger_totals().total(currency);
        if (!total) {
            return domain::Outcome<std::string, ResponseEncodingError>::failure(
                ResponseEncodingError::InvalidState);
        }
        totals.push_back(money_json(total.value()));
        balanced = balanced && total.value().minor_units() == 0;
    }

    Json entries = Json::array();
    std::size_t entry_index = 0U;
    for (const auto& entry : state.ledger_entries()) {
        Json postings = Json::array();
        for (const auto& posting : entry.postings()) {
            Json item{{"postingId", posting.id().value()},
                      {"tradeId", posting.trade_id().value()},
                      {"tradeVersion", std::to_string(posting.trade_version())},
                      {"account", posting.account()},
                      {"side", posting_side_text(posting.side())},
                      {"amount", money_json(posting.amount())},
                      {"reversalOf", nullptr}};
            if (posting.reversal_of().has_value()) {
                item["reversalOf"] = posting.reversal_of()->value();
            }
            postings.push_back(std::move(item));
        }
        entries.push_back(Json{{"entryIndex", std::to_string(entry_index)},
                               {"postings", std::move(postings)}});
        ++entry_index;
    }

    auto envelope = read_envelope(state, Json{{"balanced", balanced},
                                              {"totals", std::move(totals)},
                                              {"entries", std::move(entries)}});
    if (!envelope) {
        return domain::Outcome<std::string, ResponseEncodingError>::failure(
            envelope.error());
    }
    return domain::Outcome<std::string, ResponseEncodingError>::success(
        envelope.value().dump());
}

domain::Outcome<std::string, ResponseEncodingError>
encode_settlements_response(const domain::State& state) {
    Json obligations = Json::array();
    for (const auto& obligation : state.settlements()) {
        obligations.push_back(Json{
            {"counterpartyId", obligation.counterparty_id().value()},
            {"nettingSetId", obligation.netting_set_id().value()},
            {"valueDate", obligation.value_date().to_string()},
            {"direction", settlement_direction_text(obligation.direction())},
            {"amount", money_json(obligation.amount())}});
    }
    auto envelope =
        read_envelope(state, Json{{"obligations", std::move(obligations)}});
    if (!envelope) {
        return domain::Outcome<std::string, ResponseEncodingError>::failure(
            envelope.error());
    }
    return domain::Outcome<std::string, ResponseEncodingError>::success(
        envelope.value().dump());
}

std::string encode_problem_response(const ProblemDetails& problem) {
    const auto code = problem_code_name(problem.code);
    Json document{{"type", "urn:backbook:problem:" + std::string(code)},
                  {"title", problem.title},
                  {"status", problem.status},
                  {"detail", problem.detail},
                  {"code", code}};
    if (!problem.violations.empty()) {
        Json violations = Json::array();
        for (const auto& violation : problem.violations) {
            violations.push_back(Json{{"field", violation.field},
                                      {"message", violation.message}});
        }
        document["violations"] = std::move(violations);
    }
    if (problem.expected_version.has_value()) {
        document["expectedVersion"] = std::to_string(*problem.expected_version);
    }
    if (problem.actual_version.has_value()) {
        document["actualVersion"] = std::to_string(*problem.actual_version);
    }
    if (!problem.node_path.empty()) {
        document["nodePath"] = problem.node_path;
    }
    if (problem.currency.has_value() &&
        problem.required_minor_units.has_value() &&
        problem.remaining_minor_units.has_value()) {
        document["currency"] = currency_text(*problem.currency);
        document["required"] =
            money_json(*problem.currency, *problem.required_minor_units);
        document["remaining"] =
            money_json(*problem.currency, *problem.remaining_minor_units);
    }
    return document.dump();
}

ProblemDetails
problem_from_service_error(const service::CommandServiceError& error) {
    switch (error.code) {
    case service::CommandServiceErrorCode::IdempotencyConflict:
        return ProblemDetails{
            ProblemCode::IdempotencyConflict, 409, "Idempotency conflict",
            "The command identifier was already used for a different request."};
    case service::CommandServiceErrorCode::JournalUnavailable:
        return ProblemDetails{
            ProblemCode::JournalUnavailable, 503, "Journal unavailable",
            "Persistent command storage is unavailable until restart."};
    case service::CommandServiceErrorCode::DomainRejected:
        if (error.state_error.has_value()) {
            return state_problem(*error.state_error);
        }
        return internal_problem();
    case service::CommandServiceErrorCode::MissingStore:
    case service::CommandServiceErrorCode::RecoveryFailure:
    case service::CommandServiceErrorCode::ReplayFailure:
    case service::CommandServiceErrorCode::DuplicateJournalCommandId:
    case service::CommandServiceErrorCode::SequenceExhausted:
    case service::CommandServiceErrorCode::CommandEncodingFailure:
    case service::CommandServiceErrorCode::JournalEncodingFailure:
    case service::CommandServiceErrorCode::InvariantViolation:
        return internal_problem();
    }
    return internal_problem();
}

std::string_view problem_code_name(const ProblemCode code) noexcept {
    switch (code) {
    case ProblemCode::ValidationFailed:
        return "VALIDATION_FAILED";
    case ProblemCode::NotFound:
        return "NOT_FOUND";
    case ProblemCode::IllegalTransition:
        return "ILLEGAL_TRANSITION";
    case ProblemCode::VersionConflict:
        return "VERSION_CONFLICT";
    case ProblemCode::LimitBreach:
        return "LIMIT_BREACH";
    case ProblemCode::IdempotencyConflict:
        return "IDEMPOTENCY_CONFLICT";
    case ProblemCode::JournalUnavailable:
        return "JOURNAL_UNAVAILABLE";
    case ProblemCode::InternalError:
        return "INTERNAL_ERROR";
    }
    return "INTERNAL_ERROR";
}

}  // namespace backbook::server
