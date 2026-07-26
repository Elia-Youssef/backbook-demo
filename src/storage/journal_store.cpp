#include "backbook/storage/journal_store.hpp"

#include <bit>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>
#include <vector>

namespace backbook::storage {
namespace {

[[nodiscard]] JournalStoreError store_error(const JournalStoreErrorCode code) {
    return JournalStoreError{code};
}

[[nodiscard]] JournalRecoveryError
recovery_store_error(const JournalStoreError error) {
    return JournalRecoveryError{
        JournalRecoveryErrorCode::StoreFailure, error, std::nullopt};
}

[[nodiscard]] JournalRecoveryError
recovery_codec_error(const journal::CodecError error) {
    return JournalRecoveryError{
        JournalRecoveryErrorCode::CodecFailure, std::nullopt, error};
}

}  // namespace

FileJournalStore::FileJournalStore(std::filesystem::path path)
    : path_(std::move(path)) {}

domain::Outcome<journal::Bytes, JournalStoreError>
FileJournalStore::read_all() {
    std::error_code filesystem_error;
    const bool exists = std::filesystem::exists(path_, filesystem_error);
    if (filesystem_error) {
        return domain::Outcome<journal::Bytes, JournalStoreError>::failure(
            store_error(JournalStoreErrorCode::ReadFailed));
    }
    if (!exists) {
        return domain::Outcome<journal::Bytes, JournalStoreError>::success({});
    }

    const auto file_size = std::filesystem::file_size(path_, filesystem_error);
    if (filesystem_error) {
        return domain::Outcome<journal::Bytes, JournalStoreError>::failure(
            store_error(JournalStoreErrorCode::ReadFailed));
    }
    if (file_size > std::numeric_limits<std::size_t>::max() ||
        file_size > static_cast<std::uint64_t>(
                        std::numeric_limits<std::streamsize>::max())) {
        return domain::Outcome<journal::Bytes, JournalStoreError>::failure(
            store_error(JournalStoreErrorCode::SizeOverflow));
    }

    std::ifstream stream(path_, std::ios::binary);
    if (!stream.is_open()) {
        return domain::Outcome<journal::Bytes, JournalStoreError>::failure(
            store_error(JournalStoreErrorCode::OpenFailed));
    }

    std::vector<char> encoded(static_cast<std::size_t>(file_size));
    if (!encoded.empty()) {
        stream.read(encoded.data(),
                    static_cast<std::streamsize>(encoded.size()));
    }
    if (!stream || stream.peek() != std::char_traits<char>::eof()) {
        return domain::Outcome<journal::Bytes, JournalStoreError>::failure(
            store_error(JournalStoreErrorCode::ReadFailed));
    }
    journal::Bytes bytes;
    bytes.reserve(encoded.size());
    for (const char byte : encoded) {
        bytes.push_back(std::bit_cast<std::uint8_t>(byte));
    }
    return domain::Outcome<journal::Bytes, JournalStoreError>::success(
        std::move(bytes));
}

domain::Outcome<JournalStoreSuccess, JournalStoreError>
FileJournalStore::write_failure(const JournalStoreErrorCode code) {
    writable_ = false;
    return domain::Outcome<JournalStoreSuccess, JournalStoreError>::failure(
        store_error(code));
}

domain::Outcome<JournalStoreSuccess, JournalStoreError>
FileJournalStore::append_and_flush(const std::span<const std::uint8_t> frame) {
    if (!writable_) {
        return domain::Outcome<JournalStoreSuccess, JournalStoreError>::failure(
            store_error(JournalStoreErrorCode::Unavailable));
    }

    std::ofstream stream(path_, std::ios::binary | std::ios::app);
    if (!stream.is_open()) {
        return write_failure(JournalStoreErrorCode::OpenFailed);
    }
    for (const std::uint8_t byte : frame) {
        stream.put(std::bit_cast<char>(byte));
    }
    if (!stream) {
        return write_failure(JournalStoreErrorCode::AppendFailed);
    }
    // Flush is the commit guarantee for this process model; this is not a
    // portable power-loss durability claim.
    stream.flush();
    if (!stream) {
        return write_failure(JournalStoreErrorCode::FlushFailed);
    }
    return domain::Outcome<JournalStoreSuccess, JournalStoreError>::success({});
}

domain::Outcome<JournalStoreSuccess, JournalStoreError>
FileJournalStore::truncate(const std::uint64_t size) {
    if (!writable_) {
        return domain::Outcome<JournalStoreSuccess, JournalStoreError>::failure(
            store_error(JournalStoreErrorCode::Unavailable));
    }

    std::error_code filesystem_error;
    std::filesystem::resize_file(path_, size, filesystem_error);
    if (filesystem_error) {
        return write_failure(JournalStoreErrorCode::TruncateFailed);
    }
    return domain::Outcome<JournalStoreSuccess, JournalStoreError>::success({});
}

MemoryJournalStore::MemoryJournalStore(journal::Bytes bytes)
    : bytes_(std::move(bytes)) {}

domain::Outcome<journal::Bytes, JournalStoreError>
MemoryJournalStore::read_all() {
    return domain::Outcome<journal::Bytes, JournalStoreError>::success(bytes_);
}

domain::Outcome<JournalStoreSuccess, JournalStoreError>
MemoryJournalStore::append_and_flush(
    const std::span<const std::uint8_t> frame) {
    if (!writable_) {
        return domain::Outcome<JournalStoreSuccess, JournalStoreError>::failure(
            store_error(JournalStoreErrorCode::Unavailable));
    }
    if (fail_next_append_) {
        fail_next_append_ = false;
        writable_ = false;
        return domain::Outcome<JournalStoreSuccess, JournalStoreError>::failure(
            store_error(JournalStoreErrorCode::AppendFailed));
    }

    bytes_.insert(bytes_.end(), frame.begin(), frame.end());
    return domain::Outcome<JournalStoreSuccess, JournalStoreError>::success({});
}

domain::Outcome<JournalStoreSuccess, JournalStoreError>
MemoryJournalStore::truncate(const std::uint64_t size) {
    if (!writable_) {
        return domain::Outcome<JournalStoreSuccess, JournalStoreError>::failure(
            store_error(JournalStoreErrorCode::Unavailable));
    }
    if (fail_next_truncate_) {
        fail_next_truncate_ = false;
        writable_ = false;
        return domain::Outcome<JournalStoreSuccess, JournalStoreError>::failure(
            store_error(JournalStoreErrorCode::TruncateFailed));
    }
    if (size > bytes_.size()) {
        return domain::Outcome<JournalStoreSuccess, JournalStoreError>::failure(
            store_error(JournalStoreErrorCode::TruncateFailed));
    }
    bytes_.resize(static_cast<std::size_t>(size));
    return domain::Outcome<JournalStoreSuccess, JournalStoreError>::success({});
}

domain::Outcome<journal::JournalScanResult, JournalRecoveryError>
recover_journal(JournalStore& store) {
    auto bytes = store.read_all();
    if (!bytes) {
        return domain::
            Outcome<journal::JournalScanResult, JournalRecoveryError>::failure(
                recovery_store_error(bytes.error()));
    }

    auto scan = journal::scan_journal(bytes.value());
    if (!scan) {
        return domain::
            Outcome<journal::JournalScanResult, JournalRecoveryError>::failure(
                recovery_codec_error(scan.error()));
    }
    if (scan.value().truncated_tail) {
        // Recovery removes only bytes after the last fully validated frame.
        auto truncated = store.truncate(scan.value().last_valid_offset);
        if (!truncated) {
            return domain::Outcome<journal::JournalScanResult,
                                   JournalRecoveryError>::
                failure(recovery_store_error(truncated.error()));
        }
    }
    return domain::Outcome<journal::JournalScanResult,
                           JournalRecoveryError>::success(std::move(scan)
                                                              .value());
}

}  // namespace backbook::storage
