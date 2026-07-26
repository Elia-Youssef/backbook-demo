#pragma once

#include <type_traits>
#include <utility>
#include <variant>

namespace backbook::domain {

// Expected business failures travel as values rather than exceptions.
template <typename T, typename E>
class [[nodiscard]] Outcome final {
    static_assert(!std::is_void_v<T>);
    static_assert(!std::is_void_v<E>);

public:
    [[nodiscard]] static Outcome success(T value) {
        return Outcome(SuccessTag{}, std::move(value));
    }

    [[nodiscard]] static Outcome failure(E error) {
        return Outcome(FailureTag{}, std::move(error));
    }

    [[nodiscard]] bool has_value() const noexcept {
        return storage_.index() == 0U;
    }

    [[nodiscard]] bool has_error() const noexcept {
        return storage_.index() == 1U;
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return has_value();
    }

    [[nodiscard]] T& value() & {
        return std::get<0>(storage_);
    }

    [[nodiscard]] const T& value() const& {
        return std::get<0>(storage_);
    }

    [[nodiscard]] T&& value() && {
        return std::get<0>(std::move(storage_));
    }

    [[nodiscard]] E& error() & {
        return std::get<1>(storage_);
    }

    [[nodiscard]] const E& error() const& {
        return std::get<1>(storage_);
    }

    [[nodiscard]] E&& error() && {
        return std::get<1>(std::move(storage_));
    }

private:
    struct SuccessTag final {};
    struct FailureTag final {};

    Outcome(SuccessTag, T value)
        : storage_(std::in_place_index<0>, std::move(value)) {}

    Outcome(FailureTag, E error)
        : storage_(std::in_place_index<1>, std::move(error)) {}

    std::variant<T, E> storage_;
};

}  // namespace backbook::domain
