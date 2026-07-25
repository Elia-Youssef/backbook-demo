#include "backbook/server/demo.hpp"

#include "backbook/journal/codec.hpp"
#include "backbook/journal/fingerprint.hpp"
#include "backbook/server/json_codec.hpp"
#include "backbook/service/command_service.hpp"
#include "backbook/storage/journal_store.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <utility>

namespace backbook::server {
namespace {

using Json = nlohmann::json;

[[nodiscard]] std::filesystem::path seed_path() {
    return std::filesystem::path("demo") / "day1.jsonl";
}

[[nodiscard]] std::filesystem::path temporary_journal_path() {
    const auto root = std::filesystem::temp_directory_path();
    std::random_device random;
    for (std::uint32_t attempt = 0U; attempt < 32U; ++attempt) {
        const auto directory =
            root / ("backbook-demo-test-" + std::to_string(random()));
        std::error_code error;
        if (std::filesystem::create_directory(directory, error)) {
            return directory / "state.journal";
        }
    }
    return {};
}

TEST(DemoTest, CanonicalJsonlUsesTheCommandBoundaryAndExpectedRejection) {
    auto limits = canonical_demo_limits();
    ASSERT_TRUE(limits);
    auto store = std::make_unique<storage::MemoryJournalStore>();
    auto* store_view = store.get();
    auto created = service::CommandService::create(std::move(store),
                                                   std::move(limits).value());
    ASSERT_TRUE(created);
    auto command_service = std::move(created).value();

    const auto report = run_seed_file(*command_service, seed_path());

    ASSERT_TRUE(report);
    const auto snapshot = command_service->snapshot();
    const auto verification = verify_canonical_demo(report.value(), *snapshot);
    ASSERT_TRUE(verification);
    EXPECT_EQ(verification.value().state_version, 7U);
    EXPECT_EQ(verification.value().accepted_count, 7U);
    EXPECT_EQ(verification.value().rejected_count, 1U);
    EXPECT_EQ(verification.value().state_fingerprint,
              canonical_demo_state_fingerprint);

    const auto scanned = journal::scan_journal(store_view->bytes());
    ASSERT_TRUE(scanned);
    EXPECT_EQ(scanned.value().batches.size(), 7U);

    const auto state_response = encode_state_response(*snapshot);
    const auto ledger_response = encode_ledger_response(*snapshot);
    const auto settlement_response = encode_settlements_response(*snapshot);
    ASSERT_TRUE(state_response);
    ASSERT_TRUE(ledger_response);
    ASSERT_TRUE(settlement_response);
    const auto state_json = Json::parse(state_response.value());
    const auto ledger_json = Json::parse(ledger_response.value());
    const auto settlement_json = Json::parse(settlement_response.value());
    EXPECT_EQ(state_json["stateVersion"], "7");
    EXPECT_EQ(state_json["stateVersion"], ledger_json["stateVersion"]);
    EXPECT_EQ(state_json["stateVersion"], settlement_json["stateVersion"]);
    EXPECT_EQ(state_json["stateFingerprint"], ledger_json["stateFingerprint"]);
    EXPECT_EQ(state_json["stateFingerprint"],
              settlement_json["stateFingerprint"]);
    EXPECT_EQ(state_json["stateFingerprint"], "0x21bd5cac4ef6e98d");
    EXPECT_TRUE(ledger_json["data"]["balanced"].get<bool>());
    EXPECT_EQ(ledger_json["data"]["entries"].size(), 4U);
    EXPECT_EQ(settlement_json["data"]["obligations"].size(), 2U);

    bool found_kwd = false;
    for (const auto& obligation : settlement_json["data"]["obligations"]) {
        if (obligation["amount"]["currency"] == "KWD") {
            found_kwd = true;
            EXPECT_EQ(obligation["amount"]["minorUnits"], "35750125");
        }
    }
    EXPECT_TRUE(found_kwd);
}

TEST(DemoTest, FileJournalRestartReconstructsIdenticalState) {
    const auto journal_path = temporary_journal_path();
    ASSERT_FALSE(journal_path.empty());
    std::uint64_t first_fingerprint = 0U;
    {
        auto limits = canonical_demo_limits();
        ASSERT_TRUE(limits);
        auto created = service::CommandService::create(
            std::make_unique<storage::FileJournalStore>(journal_path),
            std::move(limits).value());
        ASSERT_TRUE(created);
        auto command_service = std::move(created).value();
        const auto report = run_seed_file(*command_service, seed_path());
        ASSERT_TRUE(report);
        const auto verified =
            verify_canonical_demo(report.value(), *command_service->snapshot());
        ASSERT_TRUE(verified);
        first_fingerprint = verified.value().state_fingerprint;
    }

    auto limits = canonical_demo_limits();
    ASSERT_TRUE(limits);
    auto recovered = service::CommandService::create(
        std::make_unique<storage::FileJournalStore>(journal_path),
        std::move(limits).value());
    ASSERT_TRUE(recovered);
    const auto replayed_fingerprint =
        journal::state_fingerprint(*recovered.value()->snapshot());
    ASSERT_TRUE(replayed_fingerprint);
    EXPECT_EQ(replayed_fingerprint.value(), first_fingerprint);
    EXPECT_EQ(replayed_fingerprint.value(), canonical_demo_state_fingerprint);
    EXPECT_EQ(recovered.value()->snapshot()->version(), 7U);

    std::error_code error;
    std::filesystem::remove_all(journal_path.parent_path(), error);
    EXPECT_FALSE(error);
}

TEST(DemoTest, DecoderReportsStablePathsBeforeDomainConstruction) {
    const auto decoded = decode_command_request(
        R"({"commandId":"CMD-1","type":"BOOK_TRADE","unexpected":true,"payload":{"tradeId":"TRD-1","bookId":"BOOK-FX-1","counterpartyId":"CPTY-A","nettingSetId":"NET-A","terms":{"kind":"FX_SPOT","tradeDate":"2026-02-30","valueDate":"2026-07-27","pay":{"currency":"USD","minorUnits":100},"receive":{"currency":"EUR","minorUnits":"1"}}}})");

    ASSERT_TRUE(decoded.has_error());
    EXPECT_EQ(decoded.error().code, CommandDecodeErrorCode::ValidationFailed);
    bool found_unknown = false;
    bool found_date = false;
    bool found_minor_units = false;
    bool found_currency = false;
    for (const auto& violation : decoded.error().violations) {
        found_unknown = found_unknown || violation.field == "$.unexpected";
        found_date =
            found_date || violation.field == "$.payload.terms.tradeDate";
        found_minor_units = found_minor_units ||
                            violation.field == "$.payload.terms.pay.minorUnits";
        found_currency = found_currency ||
                         violation.field == "$.payload.terms.receive.currency";
    }
    EXPECT_TRUE(found_unknown);
    EXPECT_TRUE(found_date);
    EXPECT_TRUE(found_minor_units);
    EXPECT_TRUE(found_currency);
}

}  // namespace
}  // namespace backbook::server
