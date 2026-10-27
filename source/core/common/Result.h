#pragma once

#include <optional>
#include <string>
#include <utility>
#include <variant>

struct Error {
    std::string message;
};

template <typename T>
class [[nodiscard]] Result {
public:
    Result(T value) : storage(std::in_place_index<0>, std::move(value)) {}
    Result(Error error) : storage(std::in_place_index<1>, std::move(error)) {}

    bool hasValue() const { return storage.index() == 0; }
    explicit operator bool() const { return hasValue(); }

    T& value() { return std::get<0>(storage); }
    const T& value() const { return std::get<0>(storage); }
    T& operator*() { return value(); }
    const T& operator*() const { return value(); }
    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }
    const std::string& error() const { return std::get<1>(storage).message; }

private:
    std::variant<T, Error> storage;
};

template <>
class [[nodiscard]] Result<void> {
public:
    Result() = default;
    Result(Error error) : failure(std::move(error)) {}

    bool hasValue() const { return !failure.has_value(); }
    explicit operator bool() const { return hasValue(); }
    const std::string& error() const { return failure->message; }

private:
    std::optional<Error> failure;
};
