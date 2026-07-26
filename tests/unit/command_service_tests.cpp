#include "backbook/service/command_service.hpp"

#include "backbook/journal/codec.hpp"
#include "backbook/journal/fingerprint.hpp"
#include "backbook/service/command_evaluator.hpp"
#include "journal_test_support.hpp"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace backbook::service {
namespace {

using ExecutionResult = domain::Outcome<CommandReceipt, CommandServiceError>;

[[nodiscard]] std::uint16_t read_u16(const journal::Bytes& bytes,
                                     const std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(
               static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

[[nodiscard]] std::uint32_t read_u32(const journal::Bytes& bytes,
                                     const std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] std::string hex(const journal::Bytes& bytes) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2U);
    for (const auto byte : bytes) {
        result.push_back(digits[byte >> 4U]);
        result.push_back(digits[byte & 0x0fU]);
    }
    return result;
}

[[nodiscard]] domain::LimitHierarchy
limits_with_usd_capacity(const std::int64_t capacity) {
    std::vector<domain::LimitDefinition> definitions;
    const auto path = test_support::limit_path();
    for (const domain::Currency currency : {domain::Currency::Usd,
                                            domain::Currency::Jpy,
                                            domain::Currency::Kwd}) {
        const auto currency_capacity =
            currency == domain::Currency::Usd ? capacity : 2'000'000'000;
        for (const auto& node : path.nodes()) {
            definitions.push_back(domain::LimitDefinition{
                node, test_support::money(currency, currency_capacity)});
        }
    }
    auto created = domain::LimitHierarchy::create(std::move(definitions));
    EXPECT_TRUE(created);
    return std::move(created).value();
}

[[nodiscard]] CommandEnvelope
book_command(const std::string_view command_id,
             const std::string_view trade_id,
             const std::int64_t pay_minor_units = 10'000'000,
             const std::int64_t receive_minor_units = 1'500'000'000) {
    const auto path = test_support::limit_path();
    return CommandEnvelope{
        test_support::id<domain::CommandId>(command_id),
        BookTradeCommand{
            test_support::id<domain::TradeId>(trade_id),
            path.book_id(),
            path.counterparty_id(),
            path.netting_set_id(),
            test_support::terms(pay_minor_units, receive_minor_units)}};
}

[[nodiscard]] CommandEnvelope
confirm_command(const std::string_view command_id,
                const std::string_view trade_id,
                const std::string& posting_prefix) {
    return CommandEnvelope{
        test_support::id<domain::CommandId>(command_id),
        ConfirmTradeCommand{test_support::id<domain::TradeId>(trade_id),
                            1U,
                            test_support::confirmation_ids(posting_prefix)}};
}

[[nodiscard]] std::unique_ptr<CommandService>
make_service(storage::MemoryJournalStore*& store,
             domain::LimitHierarchy limits = test_support::limits(),
             journal::Bytes initial_bytes = {}) {
    auto owned_store =
        std::make_unique<storage::MemoryJournalStore>(std::move(initial_bytes));
    store = owned_store.get();
    auto created =
        CommandService::create(std::move(owned_store), std::move(limits));
    EXPECT_TRUE(created);
    return std::move(created).value();
}

[[nodiscard]] journal::CommandBatch
service_batch(const std::uint64_t sequence, const CommandEnvelope& envelope,
              journal::Event event, journal::CommandResult result) {
    const auto request = canonical_command_bytes(envelope);
    EXPECT_TRUE(request);
    auto created = journal::CommandBatch::create(
        sequence, envelope.command_id, request.value(),
        std::vector<journal::Event>{std::move(event)}, std::move(result));
    EXPECT_TRUE(created);
    return std::move(created).value();
}

TEST(CommandEncodingTest, IsDeterministicAndIncludesTheWholeEnvelope) {
    const auto first = book_command("CMD-1", "TRD-1001", 100, 15'000);
    const auto same = book_command("CMD-1", "TRD-1001", 100, 15'000);
    const auto different_id = book_command("CMD-2", "TRD-1001", 100, 15'000);
    const auto different_payload =
        book_command("CMD-1", "TRD-1001", 101, 15'000);

    const auto first_bytes = canonical_command_bytes(first);
    const auto same_bytes = canonical_command_bytes(same);
    const auto id_bytes = canonical_command_bytes(different_id);
    const auto payload_bytes = canonical_command_bytes(different_payload);

    ASSERT_TRUE(first_bytes);
    ASSERT_TRUE(same_bytes);
    ASSERT_TRUE(id_bytes);
    ASSERT_TRUE(payload_bytes);
    EXPECT_EQ(first_bytes.value(), same_bytes.value());
    EXPECT_NE(first_bytes.value(), id_bytes.value());
    EXPECT_NE(first_bytes.value(), payload_bytes.value());
    ASSERT_GE(first_bytes.value().size(), 9U);
    EXPECT_EQ(first_bytes.value()[0U], command_request_format_version);
    EXPECT_EQ(first_bytes.value()[1U], 5U);
    EXPECT_EQ(first_bytes.value()[2U], 0U);
    EXPECT_EQ(first_bytes.value()[8U], 1U);
}

TEST(CommandServiceTest, RejectsMissingStore) {
    const auto created =
        CommandService::create(nullptr, test_support::limits());

    ASSERT_TRUE(created.has_error());
    EXPECT_EQ(created.error().code, CommandServiceErrorCode::MissingStore);
}

TEST(CommandServiceTest, CommitsThenPublishesAnImmutableSnapshot) {
    storage::MemoryJournalStore* store = nullptr;
    auto service = make_service(store);
    ASSERT_NE(store, nullptr);
    const auto before = service->snapshot();
    const auto command = book_command("CMD-BOOK-1", "TRD-1001");

    const auto executed = service->execute(command);

    ASSERT_TRUE(executed);
    EXPECT_FALSE(executed.value().idempotent_replay);
    const auto* result =
        std::get_if<journal::TradeBookedResult>(&executed.value().result);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->version, 1U);
    EXPECT_EQ(result->state_version, 1U);

    const auto after = service->snapshot();
    EXPECT_EQ(before->version(), 0U);
    EXPECT_TRUE(before->trade_versions().empty());
    EXPECT_EQ(after->version(), 1U);
    ASSERT_NE(
        after->current_trade(test_support::id<domain::TradeId>("TRD-1001")),
        nullptr);

    const auto scanned = journal::scan_journal(store->bytes());
    ASSERT_TRUE(scanned);
    ASSERT_EQ(scanned.value().batches.size(), 1U);
    const auto canonical = canonical_command_bytes(command);
    ASSERT_TRUE(canonical);
    EXPECT_EQ(scanned.value().batches.front().canonical_request(),
              canonical.value());
}

TEST(CommandServiceTest, RejectedDomainCommandHasNoSideEffects) {
    storage::MemoryJournalStore* store = nullptr;
    auto service = make_service(store);
    ASSERT_TRUE(service->execute(
        book_command("CMD-BOOK-1", "TRD-1001", 16'000'000, 2'400'000'000)));
    const auto before = service->snapshot();
    const auto before_bytes = store->bytes();

    const auto rejected = service->execute(
        confirm_command("CMD-CONFIRM-1", "TRD-1001", "PST-V1"));

    ASSERT_TRUE(rejected.has_error());
    EXPECT_EQ(rejected.error().code, CommandServiceErrorCode::DomainRejected);
    ASSERT_TRUE(rejected.error().state_error.has_value());
    EXPECT_EQ(rejected.error().state_error->code,
              domain::StateErrorCode::LimitFailure);
    EXPECT_EQ(service->snapshot().get(), before.get());
    EXPECT_EQ(store->bytes(), before_bytes);
    EXPECT_TRUE(before->ledger_entries().empty());
}

TEST(CommandServiceTest, ExactRetryIsIdempotentAndChangedRetryConflicts) {
    storage::MemoryJournalStore* store = nullptr;
    auto service = make_service(store);
    const auto command = book_command("CMD-BOOK-1", "TRD-1001");
    const auto first = service->execute(command);
    ASSERT_TRUE(first);
    const auto after_first = service->snapshot();
    const auto bytes_after_first = store->bytes();

    const auto repeated = service->execute(command);

    ASSERT_TRUE(repeated);
    EXPECT_TRUE(repeated.value().idempotent_replay);
    EXPECT_EQ(repeated.value().result, first.value().result);
    EXPECT_EQ(service->snapshot().get(), after_first.get());
    EXPECT_EQ(store->bytes(), bytes_after_first);

    const auto conflict =
        service->execute(book_command("CMD-BOOK-1", "TRD-DIFFERENT"));
    ASSERT_TRUE(conflict.has_error());
    EXPECT_EQ(conflict.error().code,
              CommandServiceErrorCode::IdempotencyConflict);
    EXPECT_EQ(service->snapshot().get(), after_first.get());
    EXPECT_EQ(store->bytes(), bytes_after_first);
}

TEST(CommandServiceTest, ReplayRebuildsStateSequenceAndIdempotency) {
    storage::MemoryJournalStore* first_store = nullptr;
    auto first_service = make_service(first_store);
    const auto booked = book_command("CMD-BOOK-1", "TRD-1001");
    const auto confirmed =
        confirm_command("CMD-CONFIRM-1", "TRD-1001", "PST-V1");
    ASSERT_TRUE(first_service->execute(booked));
    ASSERT_TRUE(first_service->execute(confirmed));
    const auto persisted = first_store->bytes();

    storage::MemoryJournalStore* recovered_store = nullptr;
    auto recovered_service =
        make_service(recovered_store, test_support::limits(), persisted);

    EXPECT_EQ(recovered_service->snapshot()->version(), 2U);
    EXPECT_EQ(recovered_service->snapshot()->posting_count(), 4U);
    const auto repeated = recovered_service->execute(confirmed);
    ASSERT_TRUE(repeated);
    EXPECT_TRUE(repeated.value().idempotent_replay);
    EXPECT_EQ(recovered_store->bytes(), persisted);

    const CommandEnvelope cancelled{
        test_support::id<domain::CommandId>("CMD-CANCEL-1"),
        CancelTradeCommand{test_support::id<domain::TradeId>("TRD-1001"),
                           1U,
                           test_support::reversal_ids("PST-V1-REV")}};
    ASSERT_TRUE(recovered_service->execute(cancelled));
    const auto scanned = journal::scan_journal(recovered_store->bytes());
    ASSERT_TRUE(scanned);
    ASSERT_EQ(scanned.value().batches.size(), 3U);
    EXPECT_EQ(scanned.value().batches[2U].sequence(), 3U);
}

TEST(CommandServiceTest, FullLifecycleReplaysToIdenticalCanonicalState) {
    storage::MemoryJournalStore* first_store = nullptr;
    auto first_service = make_service(first_store);
    const auto booked = book_command("CMD-1", "TRD-1001");
    const auto confirmed = confirm_command("CMD-2", "TRD-1001", "PST-V1");
    const CommandEnvelope amended{
        test_support::id<domain::CommandId>("CMD-3"),
        AmendTradeCommand{test_support::id<domain::TradeId>("TRD-1001"),
                          1U,
                          test_support::terms(10'125'000, 1'518'750'000),
                          test_support::reversal_ids("PST-V1-REV"),
                          test_support::confirmation_ids("PST-V2")}};
    const CommandEnvelope eod{test_support::id<domain::CommandId>("CMD-4"),
                              RunEodCommand{test_support::date("2026-07-27")}};

    ASSERT_TRUE(first_service->execute(booked));
    ASSERT_TRUE(first_service->execute(confirmed));
    ASSERT_TRUE(first_service->execute(amended));
    ASSERT_TRUE(first_service->execute(eod));

    const auto before = first_service->snapshot();
    const auto before_bytes = journal::canonical_state_bytes(*before);
    const auto before_fingerprint = journal::state_fingerprint(*before);
    ASSERT_TRUE(before_bytes);
    ASSERT_TRUE(before_fingerprint);
    EXPECT_EQ(before->version(), 4U);
    EXPECT_EQ(before->trade_versions().size(), 2U);
    EXPECT_EQ(before->ledger_entries().size(), 3U);
    EXPECT_EQ(before->settlements().size(), 2U);
    EXPECT_EQ(before_fingerprint.value(), 0x3e861e6ba08cf018ULL);
    const auto persisted = first_store->bytes();

    storage::MemoryJournalStore* recovered_store = nullptr;
    auto recovered_service =
        make_service(recovered_store, test_support::limits(), persisted);
    const auto after = recovered_service->snapshot();
    const auto after_bytes = journal::canonical_state_bytes(*after);
    const auto after_fingerprint = journal::state_fingerprint(*after);

    ASSERT_TRUE(after_bytes);
    ASSERT_TRUE(after_fingerprint);
    EXPECT_EQ(after_bytes.value(), before_bytes.value());
    EXPECT_EQ(after_fingerprint.value(), before_fingerprint.value());

    const auto repeated = recovered_service->execute(amended);
    ASSERT_TRUE(repeated);
    EXPECT_TRUE(repeated.value().idempotent_replay);
    const auto* repeated_result =
        std::get_if<journal::TradeAmendedResult>(&repeated.value().result);
    ASSERT_NE(repeated_result, nullptr);
    EXPECT_EQ(repeated_result->state_version, 3U);
    EXPECT_EQ(recovered_service->snapshot().get(), after.get());
    EXPECT_EQ(recovered_store->bytes(), persisted);
}

TEST(CommandServiceCharacterizationTest,
     EveryCommandVariantProducesTheStableJournalAndStateContract) {
    storage::MemoryJournalStore* store = nullptr;
    auto service = make_service(store);
    const auto trade_one = test_support::id<domain::TradeId>("TRD-1001");
    const auto trade_two = test_support::id<domain::TradeId>("TRD-1002");
    const auto path = test_support::limit_path();

    const std::array commands{
        book_command("CMD-1", "TRD-1001"),
        confirm_command("CMD-2", "TRD-1001", "PST-V1"),
        CommandEnvelope{
            test_support::id<domain::CommandId>("CMD-3"),
            AmendTradeCommand{trade_one, 1U,
                              test_support::terms(10'125'000, 1'518'750'000),
                              test_support::reversal_ids("PST-V1-REV"),
                              test_support::confirmation_ids("PST-V2")}},
        CommandEnvelope{test_support::id<domain::CommandId>("CMD-4"),
                        RunEodCommand{test_support::date("2026-07-27")}},
        CommandEnvelope{
            test_support::id<domain::CommandId>("CMD-5"),
            BookTradeCommand{trade_two, path.book_id(), path.counterparty_id(),
                             path.netting_set_id(),
                             test_support::terms(1'000'000, 150'000'000)}},
        CommandEnvelope{test_support::id<domain::CommandId>("CMD-6"),
                        CancelTradeCommand{trade_two, 1U, std::nullopt}},
    };

    for (const auto& command : commands) {
        const auto executed = service->execute(command);
        ASSERT_TRUE(executed);
        EXPECT_FALSE(executed.value().idempotent_replay);
    }

    const auto scanned = journal::scan_journal(store->bytes());
    ASSERT_TRUE(scanned);
    ASSERT_EQ(scanned.value().batches.size(), commands.size());
    constexpr std::array<std::uint8_t, 6U> expected_durable_tags{1U, 2U, 3U,
                                                                 5U, 1U, 4U};
    constexpr std::array<std::string_view, 6U> expected_request_hex{
        "010500434d442d310108005452442d313030310900424f4f4b2d46582d310600435"
        "054592d4105004e45542d4100b3500000b550000000809698000000000001002f685"
        "900000000",
        "010500434d442d320208005452442d313030310100000014005053542d56312d504"
        "1592d434f4e54524f4c2d4414005053542d56312d5041592d50415941424c452d431"
        "8005053542d56312d524543562d52454345495641424c452d4415005053542d56312"
        "d524543562d434f4e54524f4c2d43",
        "010500434d442d330308005452442d313030310100000000b3500000b550000000c"
        "87e9a0000000000013049865a0000000018005053542d56312d5245562d5041592d4"
        "34f4e54524f4c2d4318005053542d56312d5245562d5041592d50415941424c452d4"
        "41c005053542d56312d5245562d524543562d52454345495641424c452d431900505"
        "3542d56312d5245562d524543562d434f4e54524f4c2d4414005053542d56322d504"
        "1592d434f4e54524f4c2d4414005053542d56322d5041592d50415941424c452d431"
        "8005053542d56322d524543562d52454345495641424c452d4415005053542d56322"
        "d524543562d434f4e54524f4c2d43",
        "010500434d442d3405b5500000",
        "010500434d442d350108005452442d313030320900424f4f4b2d46582d310600435"
        "054592d4105004e45542d4100b3500000b55000000040420f00000000000180d1f00"
        "800000000",
        "010500434d442d360408005452442d313030320100000000",
    };
    for (std::size_t index = 0U; index < commands.size(); ++index) {
        const auto& batch = scanned.value().batches[index];
        EXPECT_EQ(batch.sequence(), index + 1U);
        ASSERT_EQ(batch.events().size(), 1U);
        const auto canonical = canonical_command_bytes(commands[index]);
        ASSERT_TRUE(canonical);
        EXPECT_EQ(batch.canonical_request(), canonical.value());
        EXPECT_EQ(hex(batch.canonical_request()), expected_request_hex[index]);
        EXPECT_EQ(journal::result_state_version(batch.result()), index + 1U);

        const auto frame = journal::encode_frame(batch);
        ASSERT_TRUE(frame);
        constexpr std::size_t frame_header_size = 9U;
        auto cursor = frame_header_size + sizeof(std::uint64_t);
        ASSERT_LT(cursor + 2U, frame.value().size());
        cursor += 2U + read_u16(frame.value(), cursor);
        ASSERT_LT(cursor + 4U, frame.value().size());
        cursor += 4U + read_u32(frame.value(), cursor);
        ASSERT_LT(cursor + 2U, frame.value().size());
        EXPECT_EQ(read_u16(frame.value(), cursor), 1U);
        cursor += 2U;
        ASSERT_LT(cursor + 5U, frame.value().size());
        EXPECT_EQ(frame.value()[cursor], expected_durable_tags[index]);
        const auto event_body_size = read_u32(frame.value(), cursor + 1U);
        cursor += 5U + event_body_size;
        ASSERT_LT(cursor, frame.value().size());
        EXPECT_EQ(frame.value()[cursor], expected_durable_tags[index]);
    }

    const auto snapshot = service->snapshot();
    EXPECT_EQ(snapshot->version(), 6U);
    EXPECT_EQ(snapshot->trade_versions().size(), 3U);
    EXPECT_EQ(snapshot->ledger_entries().size(), 3U);
    EXPECT_EQ(snapshot->posting_count(), 12U);
    EXPECT_EQ(snapshot->settlements().size(), 2U);
    ASSERT_NE(snapshot->current_trade(trade_one), nullptr);
    ASSERT_NE(snapshot->current_trade(trade_two), nullptr);
    EXPECT_EQ(snapshot->current_trade(trade_one)->state(),
              domain::TradeState::Settled);
    EXPECT_EQ(snapshot->current_trade(trade_two)->state(),
              domain::TradeState::Cancelled);

    const auto fingerprint = journal::state_fingerprint(*snapshot);
    ASSERT_TRUE(fingerprint);
    EXPECT_EQ(fingerprint.value(), 0x41a9a83742d77c33ULL);
}

TEST(CommandEvaluationTest,
     ProducesProspectiveStateEventAndResultWithoutStorage) {
    const domain::State current(test_support::limits());
    const auto envelope = book_command("CMD-BOOK-1", "TRD-1001");

    const auto evaluated = evaluate_command(current, envelope.command);

    ASSERT_TRUE(evaluated);
    EXPECT_EQ(current.version(), 0U);
    EXPECT_TRUE(current.trade_versions().empty());
    EXPECT_EQ(evaluated.value().prospective_state.version(), 1U);
    ASSERT_NE(evaluated.value().prospective_state.current_trade(
                  test_support::id<domain::TradeId>("TRD-1001")),
              nullptr);

    const auto* event =
        std::get_if<journal::TradeBookedEvent>(&evaluated.value().event);
    ASSERT_NE(event, nullptr);
    EXPECT_EQ(event->trade_id, test_support::id<domain::TradeId>("TRD-1001"));

    const auto* result =
        std::get_if<journal::TradeBookedResult>(&evaluated.value().result);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->trade_id, test_support::id<domain::TradeId>("TRD-1001"));
    EXPECT_EQ(result->version, 1U);
    EXPECT_EQ(result->state_version, 1U);
}

TEST(CommandEvaluationTest, PreservesTypedDomainRejection) {
    const domain::State current(test_support::limits());
    const auto envelope =
        confirm_command("CMD-CONFIRM-1", "TRD-MISSING", "PST-V1");

    const auto evaluated = evaluate_command(current, envelope.command);

    ASSERT_TRUE(evaluated.has_error());
    EXPECT_EQ(evaluated.error().code,
              CommandEvaluationErrorCode::DomainRejected);
    ASSERT_TRUE(evaluated.error().state_error.has_value());
    EXPECT_EQ(evaluated.error().state_error->code,
              domain::StateErrorCode::TradeNotFound);
    EXPECT_EQ(current.version(), 0U);
    EXPECT_TRUE(current.trade_versions().empty());
}

TEST(CommandServiceTest, AppendFailurePublishesNothingAndDisablesWrites) {
    storage::MemoryJournalStore* store = nullptr;
    auto service = make_service(store);
    const auto before = service->snapshot();
    store->fail_next_append();

    const auto failed =
        service->execute(book_command("CMD-BOOK-1", "TRD-1001"));

    ASSERT_TRUE(failed.has_error());
    EXPECT_EQ(failed.error().code, CommandServiceErrorCode::JournalUnavailable);
    ASSERT_TRUE(failed.error().store_error.has_value());
    EXPECT_EQ(failed.error().store_error->code,
              storage::JournalStoreErrorCode::AppendFailed);
    EXPECT_EQ(service->snapshot().get(), before.get());
    EXPECT_TRUE(store->bytes().empty());
    EXPECT_FALSE(store->writable());

    const auto retry = service->execute(book_command("CMD-BOOK-2", "TRD-1002"));
    ASSERT_TRUE(retry.has_error());
    EXPECT_EQ(retry.error().code, CommandServiceErrorCode::JournalUnavailable);
    EXPECT_TRUE(store->bytes().empty());
}

TEST(CommandServiceTest, NoOpEodIsDurableWithoutChangingStateVersion) {
    storage::MemoryJournalStore* store = nullptr;
    auto service = make_service(store);
    const CommandEnvelope eod{test_support::id<domain::CommandId>("CMD-EOD-1"),
                              RunEodCommand{test_support::date("2026-07-27")}};

    const auto executed = service->execute(eod);

    ASSERT_TRUE(executed);
    const auto* result =
        std::get_if<journal::EodRunResult>(&executed.value().result);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->settled_trade_count, 0U);
    EXPECT_EQ(result->state_version, 0U);
    EXPECT_EQ(service->snapshot()->version(), 0U);
    const auto scanned = journal::scan_journal(store->bytes());
    ASSERT_TRUE(scanned);
    ASSERT_EQ(scanned.value().batches.size(), 1U);
    EXPECT_EQ(scanned.value().batches.front().sequence(), 1U);
}

TEST(CommandServiceTest, RecoversAndTruncatesTornTailBeforeServing) {
    const auto path = test_support::limit_path();
    const auto trade_id = test_support::id<domain::TradeId>("TRD-1001");
    const auto terms = test_support::terms();
    const auto book = book_command("CMD-1", "TRD-1001");
    const auto confirm = confirm_command("CMD-2", "TRD-1001", "PST-V1");
    const auto first_batch =
        service_batch(1U, book,
                      journal::TradeBookedEvent{trade_id, path.book_id(),
                                                path.counterparty_id(),
                                                path.netting_set_id(), terms},
                      journal::TradeBookedResult{trade_id, 1U, 1U});
    const auto second_batch = service_batch(
        2U, confirm,
        journal::TradeConfirmedEvent{trade_id, 1U,
                                     test_support::confirmation_ids("PST-V1")},
        journal::TradeConfirmedResult{trade_id, 1U, 2U});
    const auto first = journal::encode_frame(first_batch);
    const auto second = journal::encode_frame(second_batch);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    auto torn = first.value();
    torn.insert(torn.end(), second.value().begin(), second.value().end() - 1);

    storage::MemoryJournalStore* store = nullptr;
    auto service = make_service(store, test_support::limits(), std::move(torn));

    EXPECT_TRUE(service->recovered_torn_tail());
    EXPECT_EQ(service->snapshot()->version(), 1U);
    EXPECT_EQ(store->bytes(), first.value());
}

TEST(CommandServiceTest, RejectsDuplicateCommandIdsDuringRecovery) {
    const auto path = test_support::limit_path();
    const auto duplicate_id =
        test_support::id<domain::CommandId>("CMD-DUPLICATE");
    const auto first_trade = test_support::id<domain::TradeId>("TRD-1001");
    const auto second_trade = test_support::id<domain::TradeId>("TRD-1002");
    const CommandEnvelope first_command{
        duplicate_id,
        BookTradeCommand{first_trade, path.book_id(), path.counterparty_id(),
                         path.netting_set_id(),
                         test_support::terms(100, 15'000)}};
    const CommandEnvelope second_command{
        duplicate_id,
        BookTradeCommand{second_trade, path.book_id(), path.counterparty_id(),
                         path.netting_set_id(),
                         test_support::terms(100, 15'000)}};
    const auto first_request = canonical_command_bytes(first_command);
    const auto second_request = canonical_command_bytes(second_command);
    ASSERT_TRUE(first_request);
    ASSERT_TRUE(second_request);
    auto first = journal::CommandBatch::create(
        1U, duplicate_id, first_request.value(),
        {journal::TradeBookedEvent{
            first_trade, path.book_id(), path.counterparty_id(),
            path.netting_set_id(), test_support::terms(100, 15'000)}},
        journal::TradeBookedResult{first_trade, 1U, 1U});
    auto second = journal::CommandBatch::create(
        2U, duplicate_id, second_request.value(),
        {journal::TradeBookedEvent{
            second_trade, path.book_id(), path.counterparty_id(),
            path.netting_set_id(), test_support::terms(100, 15'000)}},
        journal::TradeBookedResult{second_trade, 1U, 2U});
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    const auto first_frame = journal::encode_frame(first.value());
    const auto second_frame = journal::encode_frame(second.value());
    ASSERT_TRUE(first_frame);
    ASSERT_TRUE(second_frame);
    auto bytes = first_frame.value();
    bytes.insert(
        bytes.end(), second_frame.value().begin(), second_frame.value().end());

    auto store =
        std::make_unique<storage::MemoryJournalStore>(std::move(bytes));
    const auto created =
        CommandService::create(std::move(store), test_support::limits());

    ASSERT_TRUE(created.has_error());
    EXPECT_EQ(created.error().code,
              CommandServiceErrorCode::DuplicateJournalCommandId);
}

TEST(CommandServiceTest,
     RejectsUnsupportedCanonicalRequestVersionDuringRecovery) {
    const auto path = test_support::limit_path();
    const auto trade_id = test_support::id<domain::TradeId>("TRD-1001");
    const auto terms = test_support::terms();
    const auto command = book_command("CMD-1", "TRD-1001");
    const auto source =
        service_batch(1U, command,
                      journal::TradeBookedEvent{trade_id, path.book_id(),
                                                path.counterparty_id(),
                                                path.netting_set_id(), terms},
                      journal::TradeBookedResult{trade_id, 1U, 1U});
    auto unsupported_request = source.canonical_request();
    ASSERT_FALSE(unsupported_request.empty());
    unsupported_request.front() = command_request_format_version + 1U;
    const auto poisoned = journal::CommandBatch::create(
        source.sequence(), source.command_id(), std::move(unsupported_request),
        source.events(), source.result());
    ASSERT_TRUE(poisoned);
    const auto frame = journal::encode_frame(poisoned.value());
    ASSERT_TRUE(frame);

    auto store = std::make_unique<storage::MemoryJournalStore>(frame.value());
    const auto created =
        CommandService::create(std::move(store), test_support::limits());

    ASSERT_TRUE(created.has_error());
    EXPECT_EQ(created.error().code,
              CommandServiceErrorCode::InvalidCanonicalRequest);
    ASSERT_TRUE(created.error().canonical_request_error.has_value());
    EXPECT_EQ(*created.error().canonical_request_error,
              CanonicalCommandRequestError::UnsupportedVersion);
}

TEST(CommandServiceConcurrencyTest,
     TwoConfirmationsRacingForOneReservationHaveExactlyOneWinner) {
    constexpr std::uint32_t trial_count = 32U;
    for (std::uint32_t trial = 0U; trial < trial_count; ++trial) {
        SCOPED_TRACE(trial);
        storage::MemoryJournalStore* store = nullptr;
        auto service = make_service(store, limits_with_usd_capacity(100));
        ASSERT_TRUE(service->execute(
            book_command("CMD-BOOK-1", "TRD-1001", 60, 9'000)));
        ASSERT_TRUE(service->execute(
            book_command("CMD-BOOK-2", "TRD-1002", 60, 9'000)));
        const auto first_confirm =
            confirm_command("CMD-CONFIRM-1", "TRD-1001", "PST-TRD1");
        const auto second_confirm =
            confirm_command("CMD-CONFIRM-2", "TRD-1002", "PST-TRD2");

        std::barrier start(3);
        std::optional<ExecutionResult> first_result;
        std::optional<ExecutionResult> second_result;
        std::thread first_thread([&] {
            start.arrive_and_wait();
            first_result.emplace(service->execute(first_confirm));
        });
        std::thread second_thread([&] {
            start.arrive_and_wait();
            second_result.emplace(service->execute(second_confirm));
        });
        start.arrive_and_wait();
        first_thread.join();
        second_thread.join();

        ASSERT_TRUE(first_result.has_value());
        ASSERT_TRUE(second_result.has_value());
        const auto success_count =
            static_cast<std::uint32_t>(first_result->has_value()) +
            static_cast<std::uint32_t>(second_result->has_value());
        EXPECT_EQ(success_count, 1U);

        const auto& failed =
            first_result->has_error() ? *first_result : *second_result;
        ASSERT_TRUE(failed.has_error());
        EXPECT_EQ(failed.error().code, CommandServiceErrorCode::DomainRejected);
        ASSERT_TRUE(failed.error().state_error.has_value());
        EXPECT_EQ(failed.error().state_error->code,
                  domain::StateErrorCode::LimitFailure);

        const auto final = service->snapshot();
        EXPECT_EQ(final->version(), 3U);
        EXPECT_EQ(final->ledger_entries().size(), 1U);
        EXPECT_EQ(final->posting_count(), 4U);
        const auto first_trade =
            final->current_trade(test_support::id<domain::TradeId>("TRD-1001"));
        const auto second_trade =
            final->current_trade(test_support::id<domain::TradeId>("TRD-1002"));
        ASSERT_NE(first_trade, nullptr);
        ASSERT_NE(second_trade, nullptr);
        EXPECT_NE(first_trade->state(), second_trade->state());

        const auto path = test_support::limit_path();
        const auto reserved = final->limits().reserved(path.nodes().back(),
                                                       domain::Currency::Usd);
        ASSERT_TRUE(reserved);
        EXPECT_EQ(reserved.value().minor_units(), 60);

        const auto scanned = journal::scan_journal(store->bytes());
        ASSERT_TRUE(scanned);
        EXPECT_EQ(scanned.value().batches.size(), 3U);
    }
}

TEST(CommandServiceConcurrencyTest,
     ConcurrentIdenticalCommandIdsExecuteExactlyOnce) {
    storage::MemoryJournalStore* store = nullptr;
    auto service = make_service(store);
    const auto command = book_command("CMD-BOOK-1", "TRD-1001");

    std::barrier start(3);
    std::optional<ExecutionResult> first_result;
    std::optional<ExecutionResult> second_result;
    std::thread first_thread([&] {
        start.arrive_and_wait();
        first_result.emplace(service->execute(command));
    });
    std::thread second_thread([&] {
        start.arrive_and_wait();
        second_result.emplace(service->execute(command));
    });
    start.arrive_and_wait();
    first_thread.join();
    second_thread.join();

    ASSERT_TRUE(first_result.has_value());
    ASSERT_TRUE(second_result.has_value());
    ASSERT_TRUE(first_result->has_value());
    ASSERT_TRUE(second_result->has_value());
    EXPECT_EQ(first_result->value().result, second_result->value().result);
    const auto replay_count =
        static_cast<std::uint32_t>(first_result->value().idempotent_replay) +
        static_cast<std::uint32_t>(second_result->value().idempotent_replay);
    EXPECT_EQ(replay_count, 1U);

    const auto snapshot = service->snapshot();
    EXPECT_EQ(snapshot->version(), 1U);
    EXPECT_EQ(snapshot->trade_versions().size(), 1U);
    const auto scanned = journal::scan_journal(store->bytes());
    ASSERT_TRUE(scanned);
    EXPECT_EQ(scanned.value().batches.size(), 1U);
}

TEST(CommandServiceConcurrencyTest,
     ReadersObserveOnlyCompletePreOrPostCommandSnapshots) {
    storage::MemoryJournalStore* store = nullptr;
    auto service = make_service(store, limits_with_usd_capacity(100));
    ASSERT_TRUE(
        service->execute(book_command("CMD-BOOK-1", "TRD-1001", 60, 9'000)));
    const auto command =
        confirm_command("CMD-CONFIRM-1", "TRD-1001", "PST-TRD1");
    const auto trade_id = test_support::id<domain::TradeId>("TRD-1001");
    const auto path = test_support::limit_path();

    const auto snapshot_is_complete = [&](const domain::State& snapshot) {
        const auto* trade = snapshot.current_trade(trade_id);
        if (trade == nullptr) {
            return false;
        }
        const auto reserved = snapshot.limits().reserved(path.nodes().back(),
                                                         domain::Currency::Usd);
        if (!reserved) {
            return false;
        }
        if (snapshot.version() == 1U) {
            return trade->state() == domain::TradeState::Captured &&
                   snapshot.ledger_entries().empty() &&
                   snapshot.posting_count() == 0U &&
                   reserved.value().minor_units() == 0;
        }
        if (snapshot.version() == 2U) {
            return trade->state() == domain::TradeState::Confirmed &&
                   snapshot.ledger_entries().size() == 1U &&
                   snapshot.posting_count() == 4U &&
                   reserved.value().minor_units() == 60;
        }
        return false;
    };

    std::barrier start(3);
    std::atomic<bool> writer_done{false};
    std::atomic<bool> invalid_snapshot{false};
    std::optional<ExecutionResult> writer_result;
    std::thread reader([&] {
        start.arrive_and_wait();
        do {
            if (!snapshot_is_complete(*service->snapshot())) {
                invalid_snapshot.store(true, std::memory_order_release);
            }
        } while (!writer_done.load(std::memory_order_acquire));
    });
    std::thread writer([&] {
        start.arrive_and_wait();
        writer_result.emplace(service->execute(command));
        writer_done.store(true, std::memory_order_release);
    });
    start.arrive_and_wait();
    reader.join();
    writer.join();

    ASSERT_TRUE(writer_result.has_value());
    ASSERT_TRUE(writer_result->has_value());
    EXPECT_FALSE(invalid_snapshot.load(std::memory_order_acquire));
    EXPECT_TRUE(snapshot_is_complete(*service->snapshot()));
}

}  // namespace
}  // namespace backbook::service
