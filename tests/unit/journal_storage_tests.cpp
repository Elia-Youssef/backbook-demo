#include "backbook/storage/journal_store.hpp"

#include "backbook/journal/codec.hpp"
#include "journal_test_support.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <type_traits>

namespace backbook::storage {
namespace {

static_assert(std::has_virtual_destructor_v<JournalStore>);

class TemporaryJournal final {
public:
    TemporaryJournal()
        : path_(std::filesystem::current_path() /
                "backbook-file-store-test.bbk") {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    ~TemporaryJournal() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

TEST(MemoryJournalStoreTest, AppendsFramesAndRecoversBatches) {
    const auto batches = test_support::canonical_batches();
    MemoryJournalStore store;
    for (const auto& batch : batches) {
        const auto frame = journal::encode_frame(batch);
        ASSERT_TRUE(frame);
        ASSERT_TRUE(store.append_and_flush(frame.value()));
    }

    const auto recovered = recover_journal(store);

    ASSERT_TRUE(recovered);
    EXPECT_FALSE(recovered.value().truncated_tail);
    EXPECT_EQ(recovered.value().batches, batches);
    EXPECT_EQ(recovered.value().last_valid_offset, store.bytes().size());
}

TEST(MemoryJournalStoreTest, AppendFailureMakesStoreUnavailable) {
    MemoryJournalStore store;
    store.fail_next_append();

    const auto failed = store.append_and_flush(journal::Bytes{0x01U});
    ASSERT_TRUE(failed.has_error());
    EXPECT_EQ(failed.error().code, JournalStoreErrorCode::AppendFailed);
    EXPECT_FALSE(store.writable());
    EXPECT_TRUE(store.bytes().empty());

    const auto retry = store.append_and_flush(journal::Bytes{0x02U});
    ASSERT_TRUE(retry.has_error());
    EXPECT_EQ(retry.error().code, JournalStoreErrorCode::Unavailable);
    EXPECT_TRUE(store.bytes().empty());
}

TEST(JournalRecoveryTest, TruncatesOnlyTornTail) {
    const auto batches = test_support::canonical_batches();
    const auto first = journal::encode_frame(batches[0U]);
    const auto second = journal::encode_frame(batches[1U]);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);

    journal::Bytes bytes = first.value();
    bytes.insert(bytes.end(), second.value().begin(), second.value().end() - 1);
    MemoryJournalStore store(std::move(bytes));

    const auto recovered = recover_journal(store);

    ASSERT_TRUE(recovered);
    EXPECT_TRUE(recovered.value().truncated_tail);
    EXPECT_EQ(store.bytes(), first.value());
    ASSERT_EQ(recovered.value().batches.size(), 1U);
    EXPECT_EQ(recovered.value().batches.front(), batches.front());
}

TEST(JournalRecoveryTest, CompleteCrcFailureIsFatalAndNotTruncated) {
    const auto batch = test_support::canonical_batches().front();
    auto frame = journal::encode_frame(batch);
    ASSERT_TRUE(frame);
    frame.value()[9U] ^= 0x01U;
    const auto corrupt = frame.value();
    MemoryJournalStore store(corrupt);

    const auto recovered = recover_journal(store);

    ASSERT_TRUE(recovered.has_error());
    EXPECT_EQ(recovered.error().code, JournalRecoveryErrorCode::CodecFailure);
    ASSERT_TRUE(recovered.error().codec_error.has_value());
    EXPECT_EQ(recovered.error().codec_error->code,
              journal::CodecErrorCode::CrcMismatch);
    EXPECT_EQ(store.bytes(), corrupt);
}

TEST(JournalRecoveryTest, ReportsTailTruncationFailure) {
    const auto batch = test_support::canonical_batches().front();
    auto frame = journal::encode_frame(batch);
    ASSERT_TRUE(frame);
    frame.value().pop_back();
    MemoryJournalStore store(frame.value());
    store.fail_next_truncate();

    const auto recovered = recover_journal(store);

    ASSERT_TRUE(recovered.has_error());
    EXPECT_EQ(recovered.error().code, JournalRecoveryErrorCode::StoreFailure);
    ASSERT_TRUE(recovered.error().store_error.has_value());
    EXPECT_EQ(recovered.error().store_error->code,
              JournalStoreErrorCode::TruncateFailed);
    EXPECT_FALSE(store.writable());
}

TEST(FileJournalStoreTest, PersistsReadsAndTruncatesExactBytes) {
    TemporaryJournal temporary;
    FileJournalStore store(temporary.path());
    const auto batch = test_support::canonical_batches().front();
    const auto frame = journal::encode_frame(batch);
    ASSERT_TRUE(frame);

    ASSERT_TRUE(store.append_and_flush(frame.value()));
    const auto bytes = store.read_all();
    ASSERT_TRUE(bytes);
    EXPECT_EQ(bytes.value(), frame.value());

    ASSERT_TRUE(store.truncate(9U));
    const auto truncated = store.read_all();
    ASSERT_TRUE(truncated);
    ASSERT_EQ(truncated.value().size(), 9U);
    EXPECT_TRUE(std::equal(truncated.value().begin(),
                           truncated.value().end(),
                           frame.value().begin()));
}

}  // namespace
}  // namespace backbook::storage
