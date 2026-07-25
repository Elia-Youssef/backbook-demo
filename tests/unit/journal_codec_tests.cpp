#include "backbook/journal/codec.hpp"

#include "journal_test_support.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace backbook::journal {
namespace {

[[nodiscard]] std::uint32_t read_u32(const std::span<const std::uint8_t> bytes,
                                     const std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] std::size_t first_event_body_offset(const Bytes& payload) {
    std::size_t offset = 8U;
    const auto command_id_size =
        static_cast<std::size_t>(payload[offset]) |
        (static_cast<std::size_t>(payload[offset + 1U]) << 8U);
    offset += 2U + command_id_size;
    const auto request_size =
        static_cast<std::size_t>(read_u32(payload, offset));
    offset += 4U + request_size;
    offset += 2U;
    offset += 1U + 4U;
    return offset;
}

[[nodiscard]] std::size_t result_version_offset(const Bytes& payload) {
    const auto body_offset = first_event_body_offset(payload);
    const auto body_size =
        static_cast<std::size_t>(read_u32(payload, body_offset - 4U));
    return body_offset + body_size + 1U;
}

TEST(CommandBatchTest, RejectsZeroSequenceAndImpossibleStateVersion) {
    const auto trade_id = test_support::id<domain::TradeId>("TRD-1001");
    const Event event =
        TradeBookedEvent{trade_id,
                         test_support::id<domain::BookId>("BOOK-FX-1"),
                         test_support::id<domain::CounterpartyId>("CPTY-A"),
                         test_support::id<domain::NettingSetId>("NET-A"),
                         test_support::terms()};

    const auto zero =
        CommandBatch::create(0U,
                             test_support::id<domain::CommandId>("CMD-ZERO"),
                             {},
                             {event},
                             TradeBookedResult{trade_id, 1U, 0U});
    ASSERT_TRUE(zero.has_error());
    EXPECT_EQ(zero.error(), CommandBatchError::ZeroSequence);

    const auto future =
        CommandBatch::create(1U,
                             test_support::id<domain::CommandId>("CMD-FUTURE"),
                             {},
                             {event},
                             TradeBookedResult{trade_id, 1U, 2U});
    ASSERT_TRUE(future.has_error());
    EXPECT_EQ(future.error(),
              CommandBatchError::ResultStateVersionExceedsSequence);
}

TEST(JournalCodecTest, UsesStandardCrc32Vector) {
    constexpr std::array<std::uint8_t, 9U> bytes{
        '1', '2', '3', '4', '5', '6', '7', '8', '9'};
    EXPECT_EQ(crc32(bytes), 0xcbf43926U);
}

TEST(JournalCodecTest, RoundTripsEveryEventAndResultVariant) {
    const auto trade_id = test_support::id<domain::TradeId>("TRD-1001");
    const auto path = test_support::limit_path();
    const auto confirmation = test_support::confirmation_ids("PST-V1");
    const auto reversal = test_support::reversal_ids("PST-V1-REV");
    const auto replacement = test_support::confirmation_ids("PST-V2");

    const std::vector<CommandBatch> batches{
        test_support::batch(1U,
                            TradeBookedEvent{trade_id,
                                             path.book_id(),
                                             path.counterparty_id(),
                                             path.netting_set_id(),
                                             test_support::terms()},
                            TradeBookedResult{trade_id, 1U, 1U}),
        test_support::batch(2U,
                            TradeConfirmedEvent{trade_id, 1U, confirmation},
                            TradeConfirmedResult{trade_id, 1U, 2U}),
        test_support::batch(
            3U,
            TradeAmendedEvent{trade_id,
                              1U,
                              test_support::terms(10'125'000, 1'518'750'000),
                              reversal,
                              replacement},
            TradeAmendedResult{trade_id, 1U, 2U, 3U}),
        test_support::batch(
            4U,
            TradeCancelledEvent{
                trade_id, 2U, test_support::reversal_ids("PST-V2-REV")},
            TradeCancelledResult{trade_id, 2U, 4U}),
        test_support::batch(
            5U,
            EodRunEvent{test_support::date("2026-07-27")},
            EodRunResult{test_support::date("2026-07-27"), 1U, 4U})};

    for (const auto& batch : batches) {
        const auto first = encode_frame(batch);
        ASSERT_TRUE(first);
        const auto second = encode_frame(batch);
        ASSERT_TRUE(second);
        EXPECT_EQ(first.value(), second.value());

        ASSERT_GE(first.value().size(), 13U);
        EXPECT_TRUE(std::equal(
            first.value().begin(),
            first.value().begin() + 4,
            std::array<std::uint8_t, 4U>{'B', 'B', 'K', '1'}.begin()));
        EXPECT_EQ(first.value()[4U], frame_format_version);
        EXPECT_EQ(read_u32(first.value(), 5U), first.value().size() - 13U);

        const auto decoded = decode_frame(first.value());
        ASSERT_TRUE(decoded);
        EXPECT_EQ(decoded.value().batch, batch);
        EXPECT_EQ(decoded.value().bytes_consumed, first.value().size());
    }
}

TEST(JournalCodecTest, EncodesFixedWidthValuesLittleEndian) {
    const auto trade_id = test_support::id<domain::TradeId>("TRD-1001");
    const auto batch = test_support::batch(
        0x0102030405060708ULL,
        TradeBookedEvent{trade_id,
                         test_support::id<domain::BookId>("BOOK-FX-1"),
                         test_support::id<domain::CounterpartyId>("CPTY-A"),
                         test_support::id<domain::NettingSetId>("NET-A"),
                         test_support::terms()},
        TradeBookedResult{trade_id, 1U, 1U});

    const auto payload = encode_payload(batch);
    ASSERT_TRUE(payload);
    const std::array<std::uint8_t, 8U> expected{
        0x08U, 0x07U, 0x06U, 0x05U, 0x04U, 0x03U, 0x02U, 0x01U};
    EXPECT_TRUE(
        std::equal(expected.begin(), expected.end(), payload.value().begin()));
}

TEST(JournalCodecTest, RejectsCrcMismatchInCompleteFrame) {
    const auto batch = test_support::canonical_batches().front();
    auto frame = encode_frame(batch);
    ASSERT_TRUE(frame);
    frame.value()[9U] ^= 0x01U;

    const auto decoded = decode_frame(frame.value());
    ASSERT_TRUE(decoded.has_error());
    EXPECT_EQ(decoded.error().code, CodecErrorCode::CrcMismatch);
}

TEST(JournalCodecTest, RecoversOnlyIncompleteFinalFrame) {
    const auto batches = test_support::canonical_batches();
    const auto first = encode_frame(batches[0U]);
    const auto second = encode_frame(batches[1U]);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);

    const std::array<std::size_t, 3U> cuts{3U, 10U, second.value().size() - 1U};
    for (const std::size_t cut : cuts) {
        Bytes journal = first.value();
        journal.insert(journal.end(),
                       second.value().begin(),
                       second.value().begin() +
                           static_cast<std::ptrdiff_t>(cut));

        const auto scanned = scan_journal(journal);
        ASSERT_TRUE(scanned);
        EXPECT_TRUE(scanned.value().truncated_tail);
        EXPECT_EQ(scanned.value().last_valid_offset, first.value().size());
        ASSERT_EQ(scanned.value().batches.size(), 1U);
        EXPECT_EQ(scanned.value().batches.front(), batches.front());
    }

    Bytes corrupt = first.value();
    auto complete_second = second.value();
    complete_second[9U] ^= 0x80U;
    corrupt.insert(
        corrupt.end(), complete_second.begin(), complete_second.end());
    const auto rejected = scan_journal(corrupt);
    ASSERT_TRUE(rejected.has_error());
    EXPECT_EQ(rejected.error().code, CodecErrorCode::CrcMismatch);
}

TEST(JournalCodecTest, RejectsUnknownFormatVersionsAndTags) {
    const auto batch = test_support::canonical_batches().front();
    auto frame = encode_frame(batch);
    ASSERT_TRUE(frame);
    frame.value()[4U] = frame_format_version + 1U;
    const auto bad_frame = decode_frame(frame.value());
    ASSERT_TRUE(bad_frame.has_error());
    EXPECT_EQ(bad_frame.error().code, CodecErrorCode::UnsupportedFrameVersion);

    auto event_payload = encode_payload(batch);
    ASSERT_TRUE(event_payload);
    event_payload.value()[first_event_body_offset(event_payload.value())] =
        event_format_version + 1U;
    const auto bad_event = decode_payload(event_payload.value());
    ASSERT_TRUE(bad_event.has_error());
    EXPECT_EQ(bad_event.error().code, CodecErrorCode::UnsupportedEventVersion);

    auto result_payload = encode_payload(batch);
    ASSERT_TRUE(result_payload);
    result_payload.value()[result_version_offset(result_payload.value())] =
        result_format_version + 1U;
    const auto bad_result = decode_payload(result_payload.value());
    ASSERT_TRUE(bad_result.has_error());
    EXPECT_EQ(bad_result.error().code,
              CodecErrorCode::UnsupportedResultVersion);

    auto tag_payload = encode_payload(batch);
    ASSERT_TRUE(tag_payload);
    tag_payload.value()[first_event_body_offset(tag_payload.value()) - 5U] =
        0xffU;
    const auto bad_tag = decode_payload(tag_payload.value());
    ASSERT_TRUE(bad_tag.has_error());
    EXPECT_EQ(bad_tag.error().code, CodecErrorCode::InvalidEventTag);
}

}  // namespace
}  // namespace backbook::journal
