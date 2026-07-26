#pragma once

#include "backbook/domain/outcome.hpp"
#include "backbook/journal/codec.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>

namespace backbook::storage {

enum class JournalStoreErrorCode : std::uint8_t {
    SizeOverflow,
    OpenFailed,
    ReadFailed,
    AppendFailed,
    FlushFailed,
    TruncateFailed,
    Unavailable,
};

struct JournalStoreError final {
    JournalStoreErrorCode code;

    [[nodiscard]] friend bool operator==(const JournalStoreError&,
                                         const JournalStoreError&) = default;
};

struct JournalStoreSuccess final {
    [[nodiscard]] friend bool operator==(const JournalStoreSuccess&,
                                         const JournalStoreSuccess&) = default;
};

// This is the single runtime-polymorphic boundary in the application. Tests and
// production code share the same append-and-flush contract.
class JournalStore {
public:
    virtual ~JournalStore() = default;

    [[nodiscard]] virtual domain::Outcome<journal::Bytes, JournalStoreError>
    read_all() = 0;

    [[nodiscard]] virtual domain::Outcome<JournalStoreSuccess,
                                          JournalStoreError>
    append_and_flush(std::span<const std::uint8_t> frame) = 0;

    [[nodiscard]] virtual domain::Outcome<JournalStoreSuccess,
                                          JournalStoreError>
    truncate(std::uint64_t size) = 0;

    [[nodiscard]] virtual bool writable() const noexcept = 0;
};

class FileJournalStore final : public JournalStore {
public:
    explicit FileJournalStore(std::filesystem::path path);

    [[nodiscard]] domain::Outcome<journal::Bytes, JournalStoreError>
    read_all() override;

    [[nodiscard]] domain::Outcome<JournalStoreSuccess, JournalStoreError>
    append_and_flush(std::span<const std::uint8_t> frame) override;

    [[nodiscard]] domain::Outcome<JournalStoreSuccess, JournalStoreError>
    truncate(std::uint64_t size) override;

    [[nodiscard]] bool writable() const noexcept override {
        return writable_;
    }

private:
    // Once a write fails, further writes are refused until restart and replay.
    [[nodiscard]] domain::Outcome<JournalStoreSuccess, JournalStoreError>
    write_failure(JournalStoreErrorCode code);

    std::filesystem::path path_;
    bool writable_{true};
};

class MemoryJournalStore final : public JournalStore {
public:
    explicit MemoryJournalStore(journal::Bytes bytes = {});

    [[nodiscard]] domain::Outcome<journal::Bytes, JournalStoreError>
    read_all() override;

    [[nodiscard]] domain::Outcome<JournalStoreSuccess, JournalStoreError>
    append_and_flush(std::span<const std::uint8_t> frame) override;

    [[nodiscard]] domain::Outcome<JournalStoreSuccess, JournalStoreError>
    truncate(std::uint64_t size) override;

    [[nodiscard]] bool writable() const noexcept override {
        return writable_;
    }

    void fail_next_append() noexcept {
        fail_next_append_ = true;
    }

    void fail_next_truncate() noexcept {
        fail_next_truncate_ = true;
    }

    [[nodiscard]] const journal::Bytes& bytes() const noexcept {
        return bytes_;
    }

private:
    journal::Bytes bytes_;
    bool writable_{true};
    bool fail_next_append_{false};
    bool fail_next_truncate_{false};
};

enum class JournalRecoveryErrorCode : std::uint8_t {
    StoreFailure,
    CodecFailure,
};

struct JournalRecoveryError final {
    JournalRecoveryErrorCode code;
    std::optional<JournalStoreError> store_error{};
    std::optional<journal::CodecError> codec_error{};
};

[[nodiscard]] domain::Outcome<journal::JournalScanResult, JournalRecoveryError>
recover_journal(JournalStore& store);

}  // namespace backbook::storage
