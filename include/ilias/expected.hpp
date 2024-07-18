#pragma once

/**
 * @file expected.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief fallback implementation of std::expected
 * @version 0.1
 * @date 2024-07-18
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#include <exception>
#include <functional>
#include <type_traits>
#include <utility>
#include <version>

#include "ilias.hpp"

#if defined(__cpp_lib_expected)
#include <expected>
#define ILIAS_STD_EXPECTED_HPP
#endif

ILIAS_NS_BEGIN

#if defined(ILIAS_STD_EXPECTED_HPP)

template <typename T, typename E>
using Expected = ::std::expected<T, E>;
template <typename E>
using Unexpected = ::std::unexpected<E>;
template <typename E = void>
using BadExpectedAccess = ::std::bad_expected_access<E>;
using Unexpect_t        = std::unexpect_t;

#else

struct Unexpect_t {
    explicit Unexpect_t() = default;
};

#if defined(__cpp_exceptions)

template <typename E>
class BadExpectedAccess;

template <>
class BadExpectedAccess<void> : public std::exception {
public:
    BadExpectedAccess() = default;
    const char *what() const noexcept { return "Expected value is not set"; }
};

template <typename E>
class BadExpectedAccess : public BadExpectedAccess<void> {
public:
    BadExpectedAccess(const E &error) : mError(error) {}

    BadExpectedAccess(E &&error) : mError(std::move(error)) {}

    const E &error() const { return mError; }

private:
    const E mError;
};

#endif

template <typename E>
class Unexpected {
public:
    Unexpected(const E &error) : mError(error) {}

    Unexpected(E &&error) : mError(std::move(error)) {}

    const E &error() const { return mError; }

private:
    const E mError;
};

template <typename T, typename E>
class Expected {
public:
    using value_type = T;
    using error_type = E;

public:
    Expected() : mHasValue(true) { new (&mValue) T(); }
    Expected(const Expected &other) : mHasValue(other.mHasValue) {
        if (other.mHasValue) {
            new (&mValue) T(other.mValue);
        }
        else {
            new (&mError) E(other.mError);
        }
    }
    Expected(Expected &&other) : mHasValue(other.mHasValue) {
        if (other.mHasValue) {
            new (&mValue) T(std::move(other.mValue));
        }
        else {
            new (&mError) E(std::move(other.mError));
        }
    }
    template <class U, class V>
    explicit(!std::is_convertible_v<U, T> && !std::is_convertible_v<V, E>) Expected(const Expected<U, V> &other)
        : mHasValue(other.mHasValue) {
        if (other.mHasValue) {
            new (&mValue) T(other.mValue);
        }
        else {
            new (&mError) E(other.mError);
        }
    }
    template <class U, class V>
    explicit(!std::is_convertible_v<U, T> && !std::is_convertible_v<V, E>) Expected(Expected<U, V> &&other)
        : mHasValue(other.mHasValue) {
        if (other.mHasValue) {
            new (&mValue) T(std::move(other.mValue));
        }
        else {
            new (&mError) E(std::move(other.mError));
        }
    }
    template <class U = T>
    explicit(!std::is_convertible_v<U, T>) Expected(U &&v) : mHasValue(true) {
        new (&mValue) T(std::move(v));
    }
    template <class G>
    explicit(!std::is_convertible_v<const G &, E>) Expected(const Unexpected<G> &e) : mHasValue(false) {
        new (&mError) E(e.error());
    }
    template <class G>
    explicit(!std::is_convertible_v<G, E>) Expected(Unexpected<G> &&e) : mHasValue(false) {
        new (&mError) E(e.error());
    }
    template <class... Args>
    explicit Expected(std::in_place_t, Args &&...args) : mHasValue(true) {
        new (&mValue) T(std::forward<Args>(args)...);
    }
    template <class U, class... Args>
    explicit Expected(std::in_place_t, std::initializer_list<U> il, Args &&...args) : mHasValue(true) {
        new (&mValue) T(il, std::forward<Args>(args)...);
    }
    template <class... Args>
    explicit Expected(Unexpect_t, Args &&...args) : mHasValue(false) {
        new (&mError) E(std::forward<Args>(args)...);
    }
    template <class U, class... Args>
    explicit Expected(Unexpect_t, std::initializer_list<U> il, Args &&...args) : mHasValue(false) {
        new (&mError) E(il, std::forward<Args>(args)...);
    }
    ~Expected() { destroy(); }
    Expected &operator=(const Expected &other) {
        if (this == &other) {
            return *this;
        }
        destroy();
        mHasValue = other.mHasValue;
        if (other.mHasValue) {
            new (&mValue) T(other.mValue);
        }
        else {
            new (&mError) E(other.mError);
        }
        return *this;
    }
    Expected &operator=(Expected &&other) {
        if (this == &other) {
            return *this;
        }
        destroy();
        mHasValue = other.mHasValue;
        if (other.mHasValue) {
            new (&mValue) T(std::move(other.mValue));
        }
        else {
            new (&mError) E(std::move(other.mError));
        }
        return *this;
    }
    template <class U = T>
        requires std::is_convertible_v<U, T>
    Expected &operator=(U &&v) {
        destroy();
        new (&mValue) T(std::forward<U>(v));
        mHasValue = true;
        return *this;
    }
    template <class G>
    Expected &operator=(const Unexpected<G> &other) {
        destroy();
        new (&mError) E(other.error());
        mHasValue = false;
        return *this;
    }
    template <class G>
    Expected &operator=(Unexpected<G> &&other) {
        destroy();
        new (&mError) E(std::move(other.error()));
        mHasValue = false;
        return *this;
    }
    const T *operator->() const noexcept {
        if (mHasValue) {
            return &mValue;
        }
        ILIAS_THROW(BadExpectedAccess<E>(error()));
    }
    T *operator->() noexcept {
        if (mHasValue) {
            return &mValue;
        }
        ::abort();
    }
    const T &operator*() const & noexcept {
        if (mHasValue) {
            return mValue;
        }
        ::abort();
    }
    T &operator*() & noexcept {
        if (mHasValue) {
            return mValue;
        }
        ::abort();
    }
    const T &&operator*() const && noexcept {
        if (mHasValue) {
            return std::move(mValue);
        }
        ::abort();
    }
    T &&operator*() && noexcept {
        if (mHasValue) {
            return std::move(mValue);
        }
        ::abort();
    }
    explicit operator bool() const noexcept { return mHasValue; }
    bool     has_value() const noexcept { return mHasValue; }
    T       &value()       &{
        if (mHasValue) {
            return mValue;
        }
        ILIAS_THROW(BadExpectedAccess<E>(error()));
    }
    const T &value() const & {
        if (mHasValue) {
            return mValue;
        }
        ILIAS_THROW(BadExpectedAccess<E>(error()));
    }
    T &&value() && {
        if (mHasValue) {
            return std::move(mValue);
        }
        ILIAS_THROW(BadExpectedAccess<E>(error()));
    }
    const T &&value() const && {
        if (mHasValue) {
            return std::move(mValue);
        }
        ILIAS_THROW(BadExpectedAccess<E>(error()));
    }
    const E &error() const & noexcept {
        if (mHasValue) {
            ILIAS_ASSERT(false);
        }
        return mError;
    }
    E &error() & noexcept {
        if (mHasValue) {
            ILIAS_ASSERT(false);
        }
        return mError;
    }
    const E &&error() const && noexcept {
        if (mHasValue) {
            ILIAS_ASSERT(false);
        }
        return std::move(mError);
    }
    E &&error() && noexcept {
        {
            if (mHasValue) {
                ILIAS_ASSERT(false);
            }
            return std::move(mError);
        }
    }
    template <class U>
    E error_or(U &&default_error) const & {
        static_assert(std::is_convertible_v<U, E>);
        if (!has_value()) {
            return mError;
        }
        return std::forward<U>(default_error);
    }
    template <class U>
    E error_or(U &&default_error) && {
        static_assert(std::is_convertible_v<U, E>);
        if (!has_value()) {
            return std::move(mError);
        }
        return std::forward<U>(default_error);
    }
    template <class U>
    T value_or(U &&default_value) const & {
        if (has_value()) {
            return mValue;
        }
        return std::forward<U>(default_value);
    }
    template <class U>
    T value_or(U &&default_value) && {
        if (has_value()) {
            return std::move(mValue);
        }
        return std::forward<U>(default_value);
    }
    template <class F>
    auto and_then(F &&f) & {
        if (has_value()) {
            return std::invoke(std::forward<F>(f), mValue);
        }
        using U = std::remove_cvref_t<std::invoke_result_t<F, decltype(mValue)>>;
        return U(Unexpect_t {}, error());
    }
    template <class F>
    auto and_then(F &&f) const & {
        if (has_value()) {
            return std::invoke(std::forward<F>(f), mValue);
        }
        using U = std::remove_cvref_t<std::invoke_result_t<F, decltype(mValue)>>;
        return U(Unexpect_t {}, error());
    }
    template <class F>
    auto and_then(F &&f) && {
        if (has_value()) {
            return std::invoke(std::forward<F>(f), std::move(mValue));
        }
        using U = std::remove_cvref_t<std::invoke_result_t<F, decltype(mValue)>>;
        return U(Unexpect_t {}, std::move(mError));
    }
    template <class F>
    auto and_then(F &&f) const && {
        if (has_value()) {
            return std::invoke(std::forward<F>(f), std::move(mValue));
        }
        using U = std::remove_cvref_t<std::invoke_result_t<F, decltype(mValue)>>;
        return U(Unexpect_t {}, std::move(mError));
    }
    template <class F>
    auto transform(F &&f) & {
        using U = std::remove_cvref_t<std::invoke_result_t<F, decltype(mValue)>>;
        if (mHasValue) {
            if constexpr (std::is_void_v<U>) {
                std::invoke(std::forward<F>(f), mValue);
                return Expected<U, E>();
            }
            else {
                return Expected<U, E>(std::invoke(std::forward<F>(f), mValue));
            }
        }
        else {
            return Expected<U, E>(Unexpect_t {}, mError);
        }
    }
    template <class F>
    auto transform(F &&f) const & {
        using U = std::remove_cvref_t<std::invoke_result_t<F, decltype(mValue)>>;
        if (mHasValue) {
            if constexpr (std::is_void_v<U>) {
                std::invoke(std::forward<F>(f), mValue);
                return Expected<U, E>();
            }
            else {
                return Expected<U, E>(std::invoke(std::forward<F>(f), mValue));
            }
        }
        else {
            return Expected<U, E>(Unexpect_t {}, mError);
        }
    }
    template <class F>
    auto transform(F &&f) && {
        using U = std::remove_cvref_t<std::invoke_result_t<F, decltype(mValue)>>;
        if (mHasValue) {
            if constexpr (std::is_void_v<U>) {
                std::invoke(std::forward<F>(f), std::move(mValue));
                return Expected<U, E>();
            }
            else {
                return Expected<U, E>(std::invoke(std::forward<F>(f), std::move(mValue)));
            }
        }
        else {
            return Expected<U, E>(Unexpect_t {}, std::move(mError));
        }
    }
    template <class F>
    auto transform(F &&f) const && {
        using U = std::remove_cvref_t<std::invoke_result_t<F, decltype(mValue)>>;
        if (mHasValue) {
            if constexpr (std::is_void_v<U>) {
                std::invoke(std::forward<F>(f), std::move(mValue));
                return Expected<U, E>();
            }
            else {
                return Expected<U, E>(std::invoke(std::forward<F>(f), std::move(mValue)));
            }
        }
        else {
            return Expected<U, E>(Unexpect_t {}, std::move(mError));
        }
    }
    template <class F>
    auto or_else(F &&f) & {
        using G = std::remove_cvref_t<std::invoke_result_t<F, decltype(mError)>>;
        static_assert(std::is_same<typename G::value_type, T>::value, "or_else: error type must be same as value type");
        if (mHasValue) {
            return G(std::in_place, mValue);
        }
        else {
            return std::invoke(std::forward<F>(f), mError);
        }
    }
    template <class F>
    auto or_else(F &&f) const & {
        using G = std::remove_cvref_t<std::invoke_result_t<F, decltype(mError)>>;
        static_assert(std::is_same<typename G::value_type, T>::value, "or_else: error type must be same as value type");
        if (mHasValue) {
            return G(std::in_place, mValue);
        }
        else {
            return std::invoke(std::forward<F>(f), mError);
        }
    }
    template <class F>
    auto or_else(F &&f) && {
        using G = std::remove_cvref_t<std::invoke_result_t<F, decltype(mError)>>;
        static_assert(std::is_same<typename G::value_type, T>::value, "or_else: error type must be same as value type");
        if (mHasValue) {
            return G(std::in_place, std::move(mValue));
        }
        else {
            return std::invoke(std::forward<F>(f), std::move(mError));
        }
    }
    template <class F>
    auto or_else(F &&f) const && {
        using G = std::remove_cvref_t<std::invoke_result_t<F, decltype(mError)>>;
        static_assert(std::is_same<typename G::value_type, T>::value, "or_else: error type must be same as value type");
        if (mHasValue) {
            return G(std::in_place, std::move(mValue));
        }
        else {
            return std::invoke(std::forward<F>(f), std::move(mError));
        }
    }
    template <class F>
    auto transform_error(F &&f) & {
        using G = std::remove_cvref_t<std::invoke_result_t<F, E>>;
        static_assert(std::is_same<typename G::value_type, T>::value,
                      "transform_error: error type must be same as value type");
        if (mHasValue) {
            return G(std::in_place, mValue);
        }
        else {
            return std::invoke(std::forward<F>(f), mError);
        }
    }
    template <class F>
    auto transform_error(F &&f) const & {
        using G = std::remove_cvref_t<std::invoke_result_t<F, E>>;
        static_assert(std::is_same<typename G::value_type, T>::value,
                      "transform_error: error type must be same as value type");
        if (mHasValue) {
            return G(std::in_place, mValue);
        }
        else {
            return std::invoke(std::forward<F>(f), mError);
        }
    }
    template <class F>
    auto transform_error(F &&f) && {
        using G = std::remove_cvref_t<std::invoke_result_t<F, E>>;
        static_assert(std::is_same<typename G::value_type, T>::value,
                      "transform_error: error type must be same as value type");
        if (mHasValue) {
            return G(std::in_place, std::move(mValue));
        }
        else {
            return std::invoke(std::forward<F>(f), std::move(mError));
        }
    }
    template <class F>
    auto transform_error(F &&f) const && {
        using G = std::remove_cvref_t<std::invoke_result_t<F, E>>;
        static_assert(std::is_same<typename G::value_type, T>::value,
                      "transform_error: error type must be same as value type");
        if (mHasValue) {
            return G(std::in_place, std::move(mValue));
        }
        else {
            return std::invoke(std::forward<F>(f), std::move(mError));
        }
    }
    template <class... Args>
        requires std::is_constructible_v<T, Args...>
    T &emplace(Args &&...args) noexcept {
        destroy();
        mHasValue = true;
        return std::construct_at(std::addressof(mValue), std::forward<Args>(args)...);
    }
    template <class U, class... Args>
        requires std::is_constructible_v<T, std::initializer_list<U>, Args...>
    T &emplace(std::initializer_list<U> il, Args &&...args) noexcept {
        destroy();
        mHasValue = true;
        return std::construct_at(std::addressof(mValue), il, std::forward<Args>(args)...);
    }

private:
    void destroy() noexcept {
        if (mHasValue) {
            if constexpr (!std::is_void_v<T>) {
                mValue.~T();
            }
        }
        else {
            mError.~E();
        }
    }
    union {
        T mValue;
        E mError;
    };
    bool mHasValue;
};

template <typename T, typename E>
    requires std::is_void_v<T>
class Expected<T, E> {
public:
    using value_type = T;
    using error_type = E;

public:
    Expected() : mHasValue(true) {}
    Expected(const Expected &other) : mHasValue(other.mHasValue) {
        if (other.mHasValue) {
        }
        else {
            mError = other.mError;
        }
    }
    Expected(Expected &&other) : mHasValue(other.mHasValue), mError(std::move(other.mError)) {}
    template <class U, class V>
    explicit(!std::is_convertible_v<U, T> && !std::is_convertible_v<V, E>) Expected(const Expected<U, V> &other)
        : mHasValue(other.mHasValue), mError(other.mError) {}
    template <class U, class V>
    explicit(!std::is_convertible_v<U, T> && !std::is_convertible_v<V, E>) Expected(Expected<U, V> &&other)
        : mHasValue(other.mHasValue), mError(std::move(other.mError)) {}
    template <class G>
    explicit(!std::is_convertible_v<const G &, E>) Expected(const Unexpected<G> &e)
        : mHasValue(false), mError(e.error()) {}
    template <class G>
    explicit(!std::is_convertible_v<G, E>) Expected(Unexpected<G> &&e)
        : mHasValue(false), mError(std::move(e.error())) {}
    template <class... Args>
    explicit Expected(Unexpect_t, Args &&...args) : mHasValue(false), mError(std::forward<Args>(args)...) {}
    template <class U, class... Args>
    explicit Expected(Unexpect_t, std::initializer_list<U> il, Args &&...args)
        : mHasValue(false), mError(il, std::forward<Args>(args)...) {}
    ~Expected() = default;
    Expected &operator=(const Expected &other) {
        if (this == &other) {
            return *this;
        }
        mHasValue = other.mHasValue;
        if (!other.mHasValue) {
            mError = other.mError;
        }
        return *this;
    }
    Expected &operator=(Expected &&other) {
        if (this == &other) {
            return *this;
        }
        mHasValue = other.mHasValue;
        if (!other.mHasValue) {
            mError = std::move(other.mError);
        }
        return *this;
    }
    template <class G, char = 0>
    Expected &operator=(const Unexpected<G> &other) {
        mError    = other.mError;
        mHasValue = false;
        return *this;
    }
    template <class G, char = 0>
    Expected &operator=(Unexpected<G> &&other) {
        mError    = std::move(other.error());
        mHasValue = false;
        return *this;
    }
    void operator*() const noexcept {
        if (!mHasValue) {
            ILIAS_THROW(BadExpectedAccess<E>(mError));
        }
    };
    explicit operator bool() const noexcept { return mHasValue; }
    bool     has_value() const noexcept { return mHasValue; }
    void     value() const     &{
        if (!mHasValue) {
            ILIAS_THROW(BadExpectedAccess<E>(mError));
        }
    }
    void value() && {
        if (!mHasValue) {
            ILIAS_THROW(BadExpectedAccess<E>(mError));
        }
    }
    const E &error() const & noexcept {
        if (mHasValue) {
            ILIAS_ASSERT(false);
        }
        return mError;
    }
    E &error() & noexcept {
        if (mHasValue) {
            ILIAS_ASSERT(false);
        }
        return mError;
    }
    const E &&error() const && noexcept {
        if (mHasValue) {
            ILIAS_ASSERT(false);
        }
        return std::move(mError);
    }
    E &&error() && noexcept {
        if (mHasValue) {
            ILIAS_ASSERT(false);
        }
        return std::move(mError);
    }
    template <class U>
    E error_or(U &&default_error) const & {
        static_assert(std::is_convertible_v<U, E>);
        if (!has_value()) {
            return mError;
        }
        return std::forward<U>(default_error);
    }
    template <class U>
    E error_or(U &&default_error) && {
        static_assert(std::is_convertible_v<U, E>);
        if (!has_value()) {
            return std::move(mError);
        }
        return std::forward<U>(default_error);
    }
    template <class F>
    auto and_then(F &&f) & {
        if (has_value()) {
            return std::invoke(std::forward<F>(f));
        }
        using U = std::remove_cvref_t<std::invoke_result_t<F>>;
        return U(Unexpect_t {}, error());
    }
    template <class F>
    auto and_then(F &&f) const & {
        if (has_value()) {
            return std::invoke(std::forward<F>(f));
        }
        using U = std::remove_cvref_t<std::invoke_result_t<F>>;
        return U(Unexpect_t {}, error());
    }
    template <class F>
    auto and_then(F &&f) && {
        if (has_value()) {
            return std::invoke(std::forward<F>(f));
        }
        using U = std::remove_cvref_t<std::invoke_result_t<F>>;
        return U(Unexpect_t {}, std::move(mError));
    }
    template <class F>
    auto and_then(F &&f) const && {
        if (has_value()) {
            return std::invoke(std::forward<F>(f));
        }
        using U = std::remove_cvref_t<std::invoke_result_t<F>>;
        return U(Unexpect_t {}, std::move(mError));
    }
    template <class F>
    auto transform(F &&f) & {
        using U = std::remove_cvref_t<std::invoke_result_t<F>>;
        if (mHasValue) {
            if constexpr (std::is_void_v<U>) {
                std::invoke(std::forward<F>(f));
                return Expected<U, E>();
            }
            else {
                return Expected<U, E>(std::invoke(std::forward<F>(f)));
            }
        }
        else {
            return Expected<U, E>(Unexpect_t {}, mError);
        }
    }
    template <class F>
    auto transform(F &&f) const & {
        using U = std::remove_cvref_t<std::invoke_result_t<F>>;
        if (mHasValue) {
            if constexpr (std::is_void_v<U>) {
                std::invoke(std::forward<F>(f));
                return Expected<U, E>();
            }
            else {
                return Expected<U, E>(std::invoke(std::forward<F>(f)));
            }
        }
        else {
            return Expected<U, E>(Unexpect_t {}, mError);
        }
    }
    template <class F>
    auto transform(F &&f) && {
        using U = std::remove_cvref_t<std::invoke_result_t<F>>;
        if (mHasValue) {
            if constexpr (std::is_void_v<U>) {
                std::invoke(std::forward<F>(f));
                return Expected<U, E>();
            }
            else {
                return Expected<U, E>(std::invoke(std::forward<F>(f)));
            }
        }
        else {
            return Expected<U, E>(Unexpect_t {}, std::move(mError));
        }
    }
    template <class F>
    auto transform(F &&f) const && {
        using U = std::remove_cvref_t<std::invoke_result_t<F>>;
        if (mHasValue) {
            if constexpr (std::is_void_v<U>) {
                std::invoke(std::forward<F>(f));
                return Expected<U, E>();
            }
            else {
                return Expected<U, E>(std::invoke(std::forward<F>(f)));
            }
        }
        else {
            return Expected<U, E>(Unexpect_t {}, std::move(mError));
        }
    }
    template <class F>
    auto or_else(F &&f) & {
        using G = std::remove_cvref_t<std::invoke_result_t<F, decltype(mError)>>;
        static_assert(std::is_same<typename G::value_type, T>::value, "or_else: error type must be same as value type");
        if (mHasValue) {
            return G();
        }
        else {
            return std::invoke(std::forward<F>(f), mError);
        }
    }
    template <class F>
    auto or_else(F &&f) const & {
        using G = std::remove_cvref_t<std::invoke_result_t<F, decltype(mError)>>;
        static_assert(std::is_same<typename G::value_type, T>::value, "or_else: error type must be same as value type");
        if (mHasValue) {
            return G();
        }
        else {
            return std::invoke(std::forward<F>(f), mError);
        }
    }
    template <class F>
    auto or_else(F &&f) && {
        using G = std::remove_cvref_t<std::invoke_result_t<F, decltype(mError)>>;
        static_assert(std::is_same<typename G::value_type, T>::value, "or_else: error type must be same as value type");

        if (mHasValue) {
            return G();
        }
        else {
            return std::invoke(std::forward<F>(f), std::move(mError));
        }
    }
    template <class F>
    auto or_else(F &&f) const && {
        using G = std::remove_cvref_t<std::invoke_result_t<F, decltype(mError)>>;
        static_assert(std::is_same<typename G::value_type, T>::value, "or_else: error type must be same as value type");
        if (mHasValue) {
            return G();
        }
        else {
            return std::invoke(std::forward<F>(f), std::move(mError));
        }
    }
    template <class F>
    auto transform_error(F &&f) & {
        using G = std::remove_cvref_t<std::invoke_result_t<F, E>>;
        static_assert(std::is_same<typename G::value_type, T>::value,
                      "transform_error: error type must be same as value type");
        if (mHasValue) {
            return G();
        }
        else {
            return std::invoke(std::forward<F>(f), mError);
        }
    }
    template <class F>
    auto transform_error(F &&f) const & {
        using G = std::remove_cvref_t<std::invoke_result_t<F, E>>;
        static_assert(std::is_same<typename G::value_type, T>::value,
                      "transform_error: error type must be same as value type");
        if (mHasValue) {
            return G();
        }
        else {
            return std::invoke(std::forward<F>(f), mError);
        }
    }
    template <class F>
    auto transform_error(F &&f) && {
        using G = std::remove_cvref_t<std::invoke_result_t<F, E>>;
        static_assert(std::is_same<typename G::value_type, T>::value,
                      "transform_error: error type must be same as value type");
        if (mHasValue) {
            return G();
        }
        else {
            return std::invoke(std::forward<F>(f), std::move(mError));
        }
    }
    template <class F>
    auto transform_error(F &&f) const && {
        using G = std::remove_cvref_t<std::invoke_result_t<F, E>>;
        static_assert(std::is_same<typename G::value_type, T>::value,
                      "transform_error: error type must be same as value type");
        if (mHasValue) {
            return G();
        }
        else {
            return std::invoke(std::forward<F>(f), std::move(mError));
        }
    }
    void emplace() noexcept { mHasValue = true; }

private:
    E    mError;
    bool mHasValue;
};

#endif

/**
 * @brief A helper class for wrapping result T and error Error
 *
 * @tparam T
 */
template <typename T = void>
class Result final : public Expected<T, Error> {
public:
    using Expected<T, Error>::Expected;
    using Expected<T, Error>::operator=;
};

ILIAS_NS_END