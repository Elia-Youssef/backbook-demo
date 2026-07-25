#pragma once

#include "backbook/domain/outcome.hpp"

#include <compare>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace backbook::domain {

enum class IdError {
    Empty,
    TooLong,
    InvalidFirstCharacter,
    InvalidCharacter,
};

template <typename Tag>
class Id final {
public:
    [[nodiscard]] static Outcome<Id, IdError> parse(std::string_view text);

    [[nodiscard]] std::string_view value() const noexcept {
        return value_;
    }

    [[nodiscard]] friend bool operator==(const Id&, const Id&) noexcept =
        default;

    [[nodiscard]] friend std::strong_ordering operator<=>(
        const Id& lhs,
        const Id& rhs) noexcept {
        return lhs.value_ <=> rhs.value_;
    }

private:
    explicit Id(std::string value) : value_(std::move(value)) {}

    std::string value_;
};

namespace detail {

[[nodiscard]] constexpr bool is_ascii_alphanumeric(
    const unsigned char character) noexcept {
    return (character >= static_cast<unsigned char>('A') &&
            character <= static_cast<unsigned char>('Z')) ||
           (character >= static_cast<unsigned char>('a') &&
            character <= static_cast<unsigned char>('z')) ||
           (character >= static_cast<unsigned char>('0') &&
            character <= static_cast<unsigned char>('9'));
}

[[nodiscard]] constexpr bool is_valid_id_character(
    const unsigned char character) noexcept {
    return is_ascii_alphanumeric(character) ||
           character == static_cast<unsigned char>('.') ||
           character == static_cast<unsigned char>('_') ||
           character == static_cast<unsigned char>(':') ||
           character == static_cast<unsigned char>('-');
}

}  // namespace detail

template <typename Tag>
Outcome<Id<Tag>, IdError> Id<Tag>::parse(const std::string_view text) {
    if (text.empty()) {
        return Outcome<Id, IdError>::failure(IdError::Empty);
    }

    constexpr std::size_t maximum_length = 64U;
    if (text.size() > maximum_length) {
        return Outcome<Id, IdError>::failure(IdError::TooLong);
    }

    const auto first = static_cast<unsigned char>(text.front());
    if (!detail::is_ascii_alphanumeric(first)) {
        return Outcome<Id, IdError>::failure(IdError::InvalidFirstCharacter);
    }

    for (const char character : text.substr(1U)) {
        if (!detail::is_valid_id_character(
                static_cast<unsigned char>(character))) {
            return Outcome<Id, IdError>::failure(IdError::InvalidCharacter);
        }
    }

    return Outcome<Id, IdError>::success(Id(std::string{text}));
}

struct TradeIdTag final {};
struct BookIdTag final {};
struct CounterpartyIdTag final {};
struct NettingSetIdTag final {};
struct CommandIdTag final {};
struct PostingIdTag final {};

using TradeId = Id<TradeIdTag>;
using BookId = Id<BookIdTag>;
using CounterpartyId = Id<CounterpartyIdTag>;
using NettingSetId = Id<NettingSetIdTag>;
using CommandId = Id<CommandIdTag>;
using PostingId = Id<PostingIdTag>;

}  // namespace backbook::domain
