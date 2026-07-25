#include "backbook/server/demo.hpp"
#include "backbook/server/http_server.hpp"
#include "backbook/service/command_service.hpp"
#include "backbook/storage/journal_store.hpp"

#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace backbook::server {
namespace {

using Json = nlohmann::json;

[[nodiscard]] std::string
book_request(const std::string& command_id, const std::string& trade_id,
             const std::string& pay_minor_units = "10000000") {
    return Json{
        {"commandId", command_id},
        {"type", "BOOK_TRADE"},
        {"payload",
         {{"tradeId", trade_id},
          {"bookId", "BOOK-FX-1"},
          {"counterpartyId", "CPTY-A"},
          {"nettingSetId", "NET-A"},
          {"terms",
           {{"kind", "FX_SPOT"},
            {"tradeDate", "2026-07-25"},
            {"valueDate", "2026-07-29"},
            {"pay", {{"currency", "USD"}, {"minorUnits", pay_minor_units}}},
            {"receive", {{"currency", "JPY"}, {"minorUnits", "15000000"}}}}}}}}
        .dump();
}

[[nodiscard]] std::string confirm_request(const std::string& command_id,
                                          const std::string& trade_id,
                                          const std::string& expected_version,
                                          const std::string& posting_prefix) {
    return Json{
        {"commandId", command_id},
        {"type", "CONFIRM_TRADE"},
        {"expectedVersion", expected_version},
        {"payload",
         {{"tradeId", trade_id},
          {"postingIds",
           {{"payControlDebit", posting_prefix + "-PAY-CONTROL-D"},
            {"payPayableCredit", posting_prefix + "-PAY-PAYABLE-C"},
            {"receiveReceivableDebit", posting_prefix + "-RECV-RECEIVABLE-D"},
            {"receiveControlCredit", posting_prefix + "-RECV-CONTROL-C"}}}}}}
        .dump();
}

[[nodiscard]] std::string cancel_request(const std::string& command_id,
                                         const std::string& trade_id) {
    return Json{{"commandId", command_id},
                {"type", "CANCEL_TRADE"},
                {"expectedVersion", "1"},
                {"payload", {{"tradeId", trade_id}}}}
        .dump();
}

class RunningServer final {
public:
    explicit RunningServer(
        std::optional<std::filesystem::path> static_root = std::nullopt) {
        auto limits = canonical_demo_limits();
        EXPECT_TRUE(limits);
        auto owned_store = std::make_unique<storage::MemoryJournalStore>();
        store_ = owned_store.get();
        auto created = service::CommandService::create(
            std::move(owned_store), std::move(limits).value());
        EXPECT_TRUE(created);
        command_service_ = std::move(created).value();
        http_server_ = std::make_unique<HttpServer>(*command_service_,
                                                    std::move(static_root));
        port_ = http_server_->bind_to_any_port("127.0.0.1");
        EXPECT_GT(port_, 0);
        listener_ = std::thread(
            [this] { static_cast<void>(http_server_->listen_after_bind()); });
    }

    ~RunningServer() {
        http_server_->stop();
        if (listener_.joinable()) {
            listener_.join();
        }
    }

    RunningServer(const RunningServer&) = delete;
    RunningServer& operator=(const RunningServer&) = delete;

    [[nodiscard]] int port() const noexcept { return port_; }

    [[nodiscard]] storage::MemoryJournalStore& store() noexcept {
        return *store_;
    }

    [[nodiscard]] service::CommandService& command_service() noexcept {
        return *command_service_;
    }

private:
    storage::MemoryJournalStore* store_{nullptr};
    std::unique_ptr<service::CommandService> command_service_;
    std::unique_ptr<HttpServer> http_server_;
    int port_{-1};
    std::thread listener_;
};

class ThrowingStore final : public storage::JournalStore {
public:
    [[nodiscard]] domain::Outcome<journal::Bytes, storage::JournalStoreError>
    read_all() override {
        return domain::Outcome<journal::Bytes,
                               storage::JournalStoreError>::success({});
    }

    [[nodiscard]] domain::Outcome<storage::JournalStoreSuccess,
                                  storage::JournalStoreError>
    append_and_flush(std::span<const std::uint8_t>) override {
        throw std::runtime_error("sensitive diagnostic");
    }

    [[nodiscard]] domain::Outcome<storage::JournalStoreSuccess,
                                  storage::JournalStoreError>
    truncate(std::uint64_t) override {
        return domain::Outcome<storage::JournalStoreSuccess,
                               storage::JournalStoreError>::success({});
    }

    [[nodiscard]] bool writable() const noexcept override { return true; }
};

[[nodiscard]] std::filesystem::path create_static_fixture() {
    const auto root = std::filesystem::temp_directory_path();
    std::random_device random;
    for (std::uint32_t attempt = 0U; attempt < 32U; ++attempt) {
        const auto path =
            root / ("backbook-static-" + std::to_string(random()));
        std::error_code error;
        if (!std::filesystem::create_directory(path, error)) {
            continue;
        }
        std::ofstream(path / "index.html", std::ios::binary)
            << "<!doctype html><title>Backbook</title>";
        std::ofstream(path / "app.mjs", std::ios::binary)
            << "export const ready = true;";
        std::ofstream(path / "styles.css", std::ios::binary)
            << "body{color:white}";
        std::ofstream(path / "app.js.map", std::ios::binary) << "{}";
        return path;
    }
    return {};
}

TEST(HttpServerTest, HealthAndReadsUseSafeCoherentEnvelopes) {
    RunningServer server;
    httplib::Client client("127.0.0.1", server.port());
    const auto before = server.command_service().snapshot();

    const auto health = client.Get("/healthz");
    ASSERT_TRUE(health);
    EXPECT_EQ(health->status, 200);
    EXPECT_EQ(health->get_header_value("Content-Type"), "application/json");
    EXPECT_EQ(Json::parse(health->body)["status"], "ok");
    EXPECT_EQ(server.command_service().snapshot().get(), before.get());

    const auto state = client.Get("/api/v1/state");
    const auto ledger = client.Get("/api/v1/ledger");
    const auto settlements = client.Get("/api/v1/settlements");
    ASSERT_TRUE(state);
    ASSERT_TRUE(ledger);
    ASSERT_TRUE(settlements);
    EXPECT_EQ(state->status, 200);
    EXPECT_EQ(ledger->status, 200);
    EXPECT_EQ(settlements->status, 200);

    const auto state_json = Json::parse(state->body);
    const auto ledger_json = Json::parse(ledger->body);
    const auto settlements_json = Json::parse(settlements->body);
    EXPECT_EQ(state_json["stateVersion"], "0");
    EXPECT_EQ(ledger_json["stateVersion"], "0");
    EXPECT_EQ(settlements_json["stateVersion"], "0");
    EXPECT_EQ(state_json["stateFingerprint"], ledger_json["stateFingerprint"]);
    EXPECT_EQ(state_json["stateFingerprint"],
              settlements_json["stateFingerprint"]);
    EXPECT_TRUE(state_json["stateFingerprint"].is_string());
    ASSERT_FALSE(state_json["data"]["limits"].empty());
    EXPECT_TRUE(
        state_json["data"]["limits"][0U]["capacity"]["minorUnits"].is_string());
}

TEST(HttpServerTest, ValidCommandAndExactRetryExecuteOnce) {
    RunningServer server;
    httplib::Client client("127.0.0.1", server.port());
    const auto request = book_request("CMD-BOOK-HTTP-1", "TRD-HTTP-1");

    const auto first = client.Post("/api/v1/commands", request,
                                   "application/json; charset=utf-8");
    const auto repeated =
        client.Post("/api/v1/commands", request, "application/json");

    ASSERT_TRUE(first);
    ASSERT_TRUE(repeated);
    EXPECT_EQ(first->status, 200);
    EXPECT_EQ(repeated->status, 200);
    const auto first_json = Json::parse(first->body);
    const auto repeated_json = Json::parse(repeated->body);
    EXPECT_FALSE(first_json["idempotentReplay"].get<bool>());
    EXPECT_TRUE(repeated_json["idempotentReplay"].get<bool>());
    EXPECT_EQ(first_json["result"], repeated_json["result"]);
    EXPECT_EQ(server.command_service().snapshot()->version(), 1U);
    const auto scanned = journal::scan_journal(server.store().bytes());
    ASSERT_TRUE(scanned);
    EXPECT_EQ(scanned.value().batches.size(), 1U);
}

TEST(HttpServerTest, CancelsCapturedTradeAndRejectsRepeatedTransition) {
    RunningServer server;
    httplib::Client client("127.0.0.1", server.port());

    const auto booked = client.Post(
        "/api/v1/commands", book_request("CMD-BOOK-CANCEL", "TRD-CANCEL"),
        "application/json");
    const auto cancelled = client.Post(
        "/api/v1/commands", cancel_request("CMD-CANCEL-1", "TRD-CANCEL"),
        "application/json");
    const auto repeated = client.Post(
        "/api/v1/commands", cancel_request("CMD-CANCEL-2", "TRD-CANCEL"),
        "application/json");

    ASSERT_TRUE(booked);
    ASSERT_TRUE(cancelled);
    ASSERT_TRUE(repeated);
    EXPECT_EQ(booked->status, 200);
    EXPECT_EQ(cancelled->status, 200);
    EXPECT_EQ(Json::parse(cancelled->body)["result"]["type"],
              "TRADE_CANCELLED");
    EXPECT_EQ(repeated->status, 409);
    EXPECT_EQ(Json::parse(repeated->body)["code"], "ILLEGAL_TRANSITION");

    const auto state = client.Get("/api/v1/state");
    ASSERT_TRUE(state);
    const auto state_json = Json::parse(state->body);
    EXPECT_EQ(state_json["stateVersion"], "2");
    ASSERT_EQ(state_json["data"]["trades"].size(), 1U);
    EXPECT_EQ(state_json["data"]["trades"][0U]["state"], "CANCELLED");

    const auto scanned = journal::scan_journal(server.store().bytes());
    ASSERT_TRUE(scanned);
    EXPECT_EQ(scanned.value().batches.size(), 2U);
}

TEST(HttpServerTest, RejectsMalformedUnsupportedInvalidAndOversizedBodies) {
    RunningServer server;
    httplib::Client client("127.0.0.1", server.port());

    const auto wrong_type = client.Post("/api/v1/commands", "{}", "text/plain");
    const auto malformed =
        client.Post("/api/v1/commands", "{", "application/json");
    const auto invalid = client.Post(
        "/api/v1/commands",
        R"({"commandId":"CMD-1","type":"BOOK_TRADE","payload":{"tradeId":"bad/id"}})",
        "application/json");
    const std::string oversized(maximum_request_body_bytes + 1U, 'x');
    const auto too_large =
        client.Post("/api/v1/commands", oversized, "application/json");

    ASSERT_TRUE(wrong_type);
    ASSERT_TRUE(malformed);
    ASSERT_TRUE(invalid);
    ASSERT_TRUE(too_large);
    EXPECT_EQ(wrong_type->status, 415);
    EXPECT_EQ(malformed->status, 400);
    EXPECT_EQ(invalid->status, 422);
    EXPECT_EQ(too_large->status, 413);
    EXPECT_EQ(Json::parse(wrong_type->body)["code"], "VALIDATION_FAILED");
    EXPECT_EQ(Json::parse(malformed->body)["code"], "VALIDATION_FAILED");
    const auto invalid_json = Json::parse(invalid->body);
    EXPECT_EQ(invalid_json["code"], "VALIDATION_FAILED");
    ASSERT_TRUE(invalid_json.contains("violations"));
    EXPECT_EQ(invalid_json["violations"][0U]["field"], "$.payload.tradeId");
    EXPECT_TRUE(server.store().bytes().empty());
}

TEST(HttpServerTest, MapsVersionLimitIdempotencyAndStorageFailures) {
    RunningServer server;
    httplib::Client client("127.0.0.1", server.port());

    const auto booked =
        client.Post("/api/v1/commands", book_request("CMD-BOOK-1", "TRD-1001"),
                    "application/json");
    ASSERT_TRUE(booked);
    ASSERT_EQ(booked->status, 200);

    const auto stale = client.Post(
        "/api/v1/commands",
        confirm_request("CMD-CONFIRM-STALE", "TRD-1001", "2", "PST-STALE"),
        "application/json");
    ASSERT_TRUE(stale);
    EXPECT_EQ(stale->status, 409);
    const auto stale_json = Json::parse(stale->body);
    EXPECT_EQ(stale_json["code"], "VERSION_CONFLICT");
    EXPECT_EQ(stale_json["expectedVersion"], "2");
    EXPECT_EQ(stale_json["actualVersion"], "1");

    const auto conflict = client.Post(
        "/api/v1/commands", book_request("CMD-BOOK-1", "TRD-DIFFERENT"),
        "application/json");
    ASSERT_TRUE(conflict);
    EXPECT_EQ(conflict->status, 409);
    EXPECT_EQ(Json::parse(conflict->body)["code"], "IDEMPOTENCY_CONFLICT");

    const auto large_trade =
        client.Post("/api/v1/commands",
                    book_request("CMD-BOOK-LIMIT", "TRD-LIMIT", "16000000"),
                    "application/json");
    ASSERT_TRUE(large_trade);
    ASSERT_EQ(large_trade->status, 200);
    const auto limit = client.Post(
        "/api/v1/commands",
        confirm_request("CMD-CONFIRM-LIMIT", "TRD-LIMIT", "1", "PST-LIMIT"),
        "application/json");
    ASSERT_TRUE(limit);
    EXPECT_EQ(limit->status, 409);
    const auto limit_json = Json::parse(limit->body);
    EXPECT_EQ(limit_json["code"], "LIMIT_BREACH");
    EXPECT_EQ(limit_json["nodePath"].back(), "BOOK-FX-1");
    EXPECT_EQ(limit_json["currency"], "USD");
    EXPECT_EQ(limit_json["required"]["minorUnits"], "16000000");
    EXPECT_EQ(limit_json["remaining"]["minorUnits"], "15000000");

    server.store().fail_next_append();
    const auto unavailable = client.Post(
        "/api/v1/commands", book_request("CMD-BOOK-FAIL", "TRD-FAIL"),
        "application/json");
    ASSERT_TRUE(unavailable);
    EXPECT_EQ(unavailable->status, 503);
    EXPECT_EQ(Json::parse(unavailable->body)["code"], "JOURNAL_UNAVAILABLE");
}

TEST(HttpServerTest, UnexpectedExceptionReturnsGenericProblem) {
    auto limits = canonical_demo_limits();
    ASSERT_TRUE(limits);
    auto created = service::CommandService::create(
        std::make_unique<ThrowingStore>(), std::move(limits).value());
    ASSERT_TRUE(created);
    auto command_service = std::move(created).value();
    HttpServer server(*command_service);
    const int port = server.bind_to_any_port("127.0.0.1");
    ASSERT_GT(port, 0);
    std::thread listener(
        [&server] { static_cast<void>(server.listen_after_bind()); });
    httplib::Client client("127.0.0.1", port);

    const auto response =
        client.Post("/api/v1/commands", book_request("CMD-THROW", "TRD-THROW"),
                    "application/json");

    server.stop();
    listener.join();
    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 500);
    const auto problem = Json::parse(response->body);
    EXPECT_EQ(problem["code"], "INTERNAL_ERROR");
    EXPECT_EQ(problem["detail"], "The request could not be completed.");
    EXPECT_EQ(response->body.find("sensitive diagnostic"), std::string::npos);
}

TEST(HttpServerTest, ServesCommittedFrontendWithoutSourceMaps) {
    const auto distribution = std::filesystem::path("web") / "dist";
    ASSERT_TRUE(std::filesystem::is_directory(distribution));

    std::vector<std::string> asset_paths;
    std::error_code filesystem_error;
    for (std::filesystem::recursive_directory_iterator
             current(distribution, filesystem_error),
         end;
         !filesystem_error && current != end;
         current.increment(filesystem_error)) {
        if (!current->is_regular_file()) {
            continue;
        }
        EXPECT_NE(current->path().extension(), ".map");
        if (current->path().extension() == ".js" ||
            current->path().extension() == ".css") {
            asset_paths.push_back(
                "/" + std::filesystem::relative(current->path(), distribution)
                          .generic_string());
        }
    }
    ASSERT_FALSE(filesystem_error);
    ASSERT_EQ(asset_paths.size(), 2U);

    RunningServer server(distribution);
    httplib::Client client("127.0.0.1", server.port());
    const auto index = client.Get("/");
    ASSERT_TRUE(index);
    EXPECT_EQ(index->status, 200);
    EXPECT_EQ(index->get_header_value("Content-Type"), "text/html");
    EXPECT_NE(index->body.find(R"(<div id="root"></div>)"), std::string::npos);

    for (const auto& asset_path : asset_paths) {
        const auto asset = client.Get(asset_path);
        ASSERT_TRUE(asset);
        EXPECT_EQ(asset->status, 200);
        const auto expected_content_type =
            std::filesystem::path(asset_path).extension() == ".js"
                ? "text/javascript"
                : "text/css";
        EXPECT_EQ(asset->get_header_value("Content-Type"),
                  expected_content_type);
    }
}

TEST(HttpServerTest, UnknownRoutesAndStaticFilesStayBounded) {
    const auto fixture = create_static_fixture();
    ASSERT_FALSE(fixture.empty());
    {
        RunningServer server(fixture);
        httplib::Client client("127.0.0.1", server.port());
        const auto index = client.Get("/");
        const auto script = client.Get("/app.mjs");
        const auto style = client.Get("/styles.css");
        const auto source_map = client.Get("/app.js.map");
        const auto missing = client.Get("/missing");
        ASSERT_TRUE(index);
        ASSERT_TRUE(script);
        ASSERT_TRUE(style);
        ASSERT_TRUE(source_map);
        ASSERT_TRUE(missing);
        EXPECT_EQ(index->status, 200);
        EXPECT_EQ(script->get_header_value("Content-Type"), "text/javascript");
        EXPECT_EQ(style->get_header_value("Content-Type"), "text/css");
        EXPECT_EQ(source_map->get_header_value("Content-Type"),
                  "application/json");
        EXPECT_EQ(missing->status, 404);
        EXPECT_EQ(Json::parse(missing->body)["code"], "NOT_FOUND");
    }
    {
        RunningServer server;
        httplib::Client client("127.0.0.1", server.port());
        const auto index = client.Get("/");
        ASSERT_TRUE(index);
        EXPECT_EQ(index->status, 404);
        EXPECT_EQ(Json::parse(index->body)["code"], "NOT_FOUND");
    }
    std::error_code error;
    std::filesystem::remove_all(fixture, error);
    EXPECT_FALSE(error);
}

}  // namespace
}  // namespace backbook::server
