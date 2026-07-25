#include "backbook/server/demo.hpp"

#include "backbook/domain/ledger_totals.hpp"
#include "backbook/journal/fingerprint.hpp"
#include "backbook/server/http_server.hpp"

#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace backbook::server {
namespace {

[[nodiscard]] bool
append_limit(std::vector<domain::LimitDefinition>& definitions,
             const domain::LimitNode& node, const domain::Currency currency,
             const std::int64_t capacity) {
    auto money = domain::Money::from_minor_units(currency, capacity);
    if (!money) {
        return false;
    }
    definitions.push_back(
        domain::LimitDefinition{node, std::move(money).value()});
    return true;
}

[[nodiscard]] bool append_path_limits(
    std::vector<domain::LimitDefinition>& definitions,
    const domain::LimitPath& path, const domain::Currency currency,
    const std::int64_t counterparty_capacity,
    const std::int64_t netting_set_capacity, const std::int64_t book_capacity) {
    const auto nodes = path.nodes();
    return append_limit(definitions, nodes[1U], currency,
                        counterparty_capacity) &&
           append_limit(definitions, nodes[2U], currency,
                        netting_set_capacity) &&
           append_limit(definitions, nodes[3U], currency, book_capacity);
}

[[nodiscard]] bool
is_expected_limit_rejection(const SeedCommandResult& command) {
    if (!command.error.has_value() ||
        command.error->code !=
            service::CommandServiceErrorCode::DomainRejected ||
        !command.error->state_error.has_value()) {
        return false;
    }
    const auto& state_error = *command.error->state_error;
    if (state_error.code != domain::StateErrorCode::LimitFailure ||
        !state_error.limit_error.has_value()) {
        return false;
    }
    const auto& limit_error = *state_error.limit_error;
    const auto path = limit_error.node.path_components();
    return limit_error.code == domain::LimitErrorCode::Breach &&
           limit_error.node.level() == domain::LimitLevel::Book &&
           path.size() == 4U && path[1U] == "CPTY-A" && path[2U] == "NET-A" &&
           path[3U] == "BOOK-FX-1" &&
           limit_error.currency == domain::Currency::Usd &&
           limit_error.required_minor_units == 6'000'000 &&
           limit_error.remaining_minor_units == 4'875'000;
}

[[nodiscard]] bool state_matches_canonical_demo(const domain::State& state) {
    const auto trade_1001_id = domain::TradeId::parse("TRD-1001");
    const auto trade_1002_id = domain::TradeId::parse("TRD-1002");
    const auto trade_1003_id = domain::TradeId::parse("TRD-1003");
    const auto counterparty_a = domain::CounterpartyId::parse("CPTY-A");
    const auto netting_a = domain::NettingSetId::parse("NET-A");
    const auto book_one = domain::BookId::parse("BOOK-FX-1");
    if (!trade_1001_id || !trade_1002_id || !trade_1003_id || !counterparty_a ||
        !netting_a || !book_one) {
        return false;
    }

    const auto* trade_1001 = state.current_trade(trade_1001_id.value());
    const auto* trade_1002 = state.current_trade(trade_1002_id.value());
    const auto* trade_1003 = state.current_trade(trade_1003_id.value());
    const auto* trade_1001_v1 = state.find_trade(trade_1001_id.value(), 1U);
    if (trade_1001 == nullptr || trade_1002 == nullptr ||
        trade_1003 == nullptr || trade_1001_v1 == nullptr) {
        return false;
    }
    if (trade_1001->version() != 2U ||
        trade_1001->state() != domain::TradeState::Confirmed ||
        trade_1001->terms().pay().minor_units() != 10'125'000 ||
        trade_1001->terms().receive().minor_units() != 15'187'500 ||
        trade_1001_v1->state() != domain::TradeState::Superseded ||
        trade_1001_v1->superseded_by() != 2U ||
        trade_1001->supersedes() != 1U ||
        trade_1002->state() != domain::TradeState::Captured ||
        trade_1003->state() != domain::TradeState::Settled) {
        return false;
    }

    const domain::LimitNode book_node = domain::LimitNode::book(
        counterparty_a.value(), netting_a.value(), book_one.value());
    const auto headroom =
        state.limits().headroom(book_node, domain::Currency::Usd);
    if (!headroom || headroom.value().minor_units() != 4'875'000) {
        return false;
    }

    bool has_outgoing = false;
    bool has_incoming = false;
    for (const auto& settlement : state.settlements()) {
        has_outgoing =
            has_outgoing ||
            settlement.direction() == domain::SettlementDirection::Outgoing;
        has_incoming =
            has_incoming ||
            settlement.direction() == domain::SettlementDirection::Incoming;
    }
    const auto recomputed =
        domain::recompute_ledger_totals(state.ledger_entries());
    return state.version() == 7U && state.trade_versions().size() == 4U &&
           state.ledger_entries().size() == 4U &&
           state.posting_count() == 16U && state.settlements().size() == 2U &&
           has_outgoing && has_incoming && recomputed &&
           recomputed.value() == state.ledger_totals();
}

}  // namespace

domain::Outcome<domain::LimitHierarchy, DemoLimitError>
canonical_demo_limits() {
    auto counterparty_a = domain::CounterpartyId::parse("CPTY-A");
    auto counterparty_b = domain::CounterpartyId::parse("CPTY-B");
    auto netting_a = domain::NettingSetId::parse("NET-A");
    auto netting_b = domain::NettingSetId::parse("NET-B");
    auto book_one = domain::BookId::parse("BOOK-FX-1");
    auto book_two = domain::BookId::parse("BOOK-FX-2");
    if (!counterparty_a || !counterparty_b || !netting_a || !netting_b ||
        !book_one || !book_two) {
        return domain::Outcome<domain::LimitHierarchy, DemoLimitError>::failure(
            DemoLimitError::DefinitionFailed);
    }

    const domain::LimitPath path_a(std::move(counterparty_a).value(),
                                   std::move(netting_a).value(),
                                   std::move(book_one).value());
    const domain::LimitPath path_b(std::move(counterparty_b).value(),
                                   std::move(netting_b).value(),
                                   std::move(book_two).value());

    std::vector<domain::LimitDefinition> definitions;
    definitions.reserve(21U);
    const bool valid =
        append_limit(definitions, domain::LimitNode::group(),
                     domain::Currency::Usd, 50'000'000) &&
        append_path_limits(definitions, path_a, domain::Currency::Usd,
                           30'000'000, 20'000'000, 15'000'000) &&
        append_path_limits(definitions, path_b, domain::Currency::Usd,
                           30'000'000, 20'000'000, 15'000'000) &&
        append_limit(definitions, domain::LimitNode::group(),
                     domain::Currency::Jpy, 5'000'000'000) &&
        append_path_limits(definitions, path_a, domain::Currency::Jpy,
                           2'000'000'000, 2'000'000'000, 2'000'000'000) &&
        append_path_limits(definitions, path_b, domain::Currency::Jpy,
                           2'000'000'000, 2'000'000'000, 2'000'000'000) &&
        append_limit(definitions, domain::LimitNode::group(),
                     domain::Currency::Kwd, 1'000'000'000) &&
        append_path_limits(definitions, path_a, domain::Currency::Kwd,
                           500'000'000, 500'000'000, 500'000'000) &&
        append_path_limits(definitions, path_b, domain::Currency::Kwd,
                           500'000'000, 500'000'000, 500'000'000);
    if (!valid) {
        return domain::Outcome<domain::LimitHierarchy, DemoLimitError>::failure(
            DemoLimitError::DefinitionFailed);
    }

    auto hierarchy = domain::LimitHierarchy::create(std::move(definitions));
    if (!hierarchy) {
        return domain::Outcome<domain::LimitHierarchy, DemoLimitError>::failure(
            DemoLimitError::DefinitionFailed);
    }
    return domain::Outcome<domain::LimitHierarchy, DemoLimitError>::success(
        std::move(hierarchy).value());
}

domain::Outcome<SeedRunReport, SeedError>
run_seed_file(service::CommandService& command_service,
              const std::filesystem::path& seed_path) {
    std::ifstream stream(seed_path, std::ios::binary);
    if (!stream.is_open()) {
        return domain::Outcome<SeedRunReport, SeedError>::failure(
            SeedError{SeedErrorCode::OpenFailed, 0U});
    }

    SeedRunReport report{{}, 0U, 0U};
    std::string line;
    std::size_t line_number = 0U;
    while (std::getline(stream, line)) {
        ++line_number;
        if (line.empty()) {
            return domain::Outcome<SeedRunReport, SeedError>::failure(
                SeedError{SeedErrorCode::EmptyLine, line_number});
        }
        if (line.size() > maximum_request_body_bytes) {
            return domain::Outcome<SeedRunReport, SeedError>::failure(
                SeedError{SeedErrorCode::LineTooLarge, line_number});
        }

        auto command = decode_command_request(line);
        if (!command) {
            auto error = SeedError{SeedErrorCode::DecodeFailed, line_number};
            error.decode_error = command.error();
            return domain::Outcome<SeedRunReport, SeedError>::failure(
                std::move(error));
        }

        const auto executed = command_service.execute(command.value());
        if (executed) {
            report.commands.push_back(
                SeedCommandResult{line_number, command.value().command_id,
                                  executed.value(), std::nullopt});
            ++report.accepted_count;
            continue;
        }
        if (executed.error().code !=
            service::CommandServiceErrorCode::DomainRejected) {
            auto error = SeedError{SeedErrorCode::ExecutionFailed, line_number};
            error.service_error = executed.error();
            return domain::Outcome<SeedRunReport, SeedError>::failure(
                std::move(error));
        }
        report.commands.push_back(
            SeedCommandResult{line_number, command.value().command_id,
                              std::nullopt, executed.error()});
        ++report.rejected_count;
    }
    if (stream.bad()) {
        return domain::Outcome<SeedRunReport, SeedError>::failure(
            SeedError{SeedErrorCode::ReadFailed, line_number});
    }
    return domain::Outcome<SeedRunReport, SeedError>::success(
        std::move(report));
}

domain::Outcome<DemoVerification, DemoVerificationError>
verify_canonical_demo(const SeedRunReport& report, const domain::State& state) {
    if (report.commands.size() != 8U || report.accepted_count != 7U ||
        report.rejected_count != 1U) {
        return domain::Outcome<DemoVerification, DemoVerificationError>::
            failure(DemoVerificationError::UnexpectedCommandCounts);
    }

    std::size_t rejection_count = 0U;
    for (const auto& command : report.commands) {
        if (command.error.has_value()) {
            ++rejection_count;
            if (!is_expected_limit_rejection(command)) {
                return domain::
                    Outcome<DemoVerification, DemoVerificationError>::failure(
                        DemoVerificationError::UnexpectedRejection);
            }
        }
    }
    if (rejection_count != 1U) {
        return domain::Outcome<DemoVerification, DemoVerificationError>::
            failure(DemoVerificationError::UnexpectedRejection);
    }
    if (!state_matches_canonical_demo(state)) {
        return domain::Outcome<DemoVerification, DemoVerificationError>::
            failure(DemoVerificationError::UnexpectedState);
    }

    const auto fingerprint = journal::state_fingerprint(state);
    if (!fingerprint) {
        return domain::Outcome<DemoVerification, DemoVerificationError>::
            failure(DemoVerificationError::FingerprintFailed);
    }
    if (fingerprint.value() != canonical_demo_state_fingerprint) {
        return domain::Outcome<DemoVerification, DemoVerificationError>::
            failure(DemoVerificationError::UnexpectedFingerprint);
    }
    return domain::Outcome<DemoVerification, DemoVerificationError>::success(
        DemoVerification{state.version(), fingerprint.value(),
                         report.accepted_count, report.rejected_count});
}

}  // namespace backbook::server
