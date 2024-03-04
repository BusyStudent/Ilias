#pragma once

#include <cstdlib>
#include <type_traits>

#if __cplusplus >= 202002L
#include <version>
#endif
#ifdef __cpp_lib_expected
#include <expected>
#endif

#include "ilias.hpp"

ILIAS_NS_BEGIN

#ifdef __cpp_lib_expected
template <typename T, typename E>
using Expected = ::std::expected<T, E>;
template <typename E>
using Unexpected = ::std::unexpected<E>;

#else
inline namespace _ilias_fallback {
template <typename E>
class unexpected {
public:
    unexpected(const E &error)
        : mError(error)
    {
    }

    unexpected(E &&error)
        : mError(std::move(error))
    {
    }

    const E &error() const { return mError; }

private:
    const E mError;
};
template <typename T, typename E, class enable = void>
class expected;
template <typename T, typename E>
class expected<T, E, typename std::enable_if<!std::is_void<T>::value>::type> {
public:
    using ValueT = T;
    using ErrorT = E;
    using UnexpectedT = Unexpected<E>;

private:
    union {
        ValueT mValue;
        ErrorT mError;
    };

    enum {
        ValueType,
        ErrorType,
    } mType;

public:
    expected(const ValueT &value)
        : mType(ValueType)
    {
        new (&mValue) ValueT(value);
    }

    expected(ValueT &&value)
        : mType(ValueType)
    {
        new (&mValue) ValueT(std::move(value));
    }

    expected(const ErrorT &error)
        : mType(ErrorType)
    {
        new (&mError) ErrorT(error);
    }

    expected(ErrorT &&error)
        : mType(ErrorType)
    {
        new (&mError) ErrorT(std::move(error));
    }

    expected(const expected &other) CS_NOEXCEPT
        : mType(other.mType)
    {
        if (mType == ErrorType) {
            new (&mError) ErrorT(other.mError);
        } else {
            new (&mValue) ValueT(other.mValue);
        }
    }

    expected(const UnexpectedT &error)
        : mType(ErrorType)
    {
        new (&mError) ErrorT(error.error());
    }

    expected(UnexpectedT &&error)
        : mType(ErrorType)
    {
        new (&mError) ErrorT(error.error());
    }

    expected &operator=(const UnexpectedT &other) CS_NOEXCEPT
    {
        destory();
        mType = ErrorType;
        new (&mError) ErrorT(other.error());
        return *this;
    }
    expected &operator=(const expected &other) CS_NOEXCEPT
    {
        destory();
        mType = other.mType;
        if (mType == ErrorType) {
            new (&mError) ErrorT(other.mError);
        } else {
            new (&mValue) ValueT(other.mValue);
        }
        return *this;
    }
    expected &operator=(ValueT &&value)
    {
        destory();
        mType = ValueType;
        new (&mValue) ValueT(std::move(value));
        return *this;
    }
    expected &operator=(const ValueT &value)
    {
        destory();
        mType = ValueType;
        new (&mValue) ValueT(value);
        return *this;
    }
    expected &operator=(ErrorT &&error) CS_NOEXCEPT
    {
        destory();
        mType = ErrorType;
        new (&mError) ErrorT(std::move(error));
        return *this;
    }
    expected &operator=(const ErrorT &error) CS_NOEXCEPT
    {
        destory();
        mType = ErrorType;
        new (&mError) ErrorT(error);
        return *this;
    }

    operator bool() const CS_NOEXCEPT { return !mType; }
    ValueT &operator*() { return mValue; }
    const ValueT &operator*() const { return mValue; }
    ValueT *operator->() { return &mValue; }
    const ValueT *operator->() const { return &mValue; }

    bool has_value() const { return !mType; }
    ValueT &value() { return mValue; }
    const ValueT &value() const { return mValue; }
    ValueT &value_or(ValueT &value) { return mType ? value : mValue; }
    const ValueT &value_or(const ValueT &value) const { return mType ? value : mValue; }

    ErrorT &error() { return mError; }
    const ErrorT &error() const { return mError; }

    ~expected() { destory(); }

private:
    inline void destory()
    {
        if (mType == ErrorType) {
            mError.~ErrorT();
        } else {
            mValue.~ValueT();
        }
    }
};

template <typename T>
class expected<T, T, typename std::enable_if<!std::is_void<T>::value>::type> {
public:
    using ValueT = T;
    using ErrorT = T;
    using UnexpectedT = Unexpected<T>;

private:
    union {
        ValueT mValue;
        ErrorT mError;
    };

    enum {
        ValueType,
        ErrorType,
    } mType;

public:
    expected(const ValueT &value)
        : mType(ValueType)
    {
        new (&mValue) ValueT(value);
    }

    expected(ValueT &&value)
        : mType(ValueType)
    {
        new (&mValue) ValueT(std::move(value));
    }

    expected(const expected &other) CS_NOEXCEPT
        : mType(other.mType)
    {
        if (mType == ValueType) {
            new (&mValue) ValueT(other.mValue);
        } else {
            new (&mError) ErrorT(other.mError);
        }
    }

    expected(const UnexpectedT &error)
        : mType(ErrorType)
    {
        new (&mError) ErrorT(error.error());
    }

    expected(UnexpectedT &&error)
        : mType(ErrorType)
    {
        new (&mError) ErrorT(error.error());
    }

    expected operator=(const UnexpectedT &error)
    {
        destory();
        mType = ErrorType;
        new (&mError) ErrorT(error.error());
        return *this;
    }

    expected operator=(const expected &other) CS_NOEXCEPT
    {
        destory();
        if (other.mType == ErrorType) {
            new (&mError) ErrorT(other.mError);
            mType = ErrorType;
        } else {
            new (&mValue) ValueT(other.mValue);
            mType = ValueType;
        }
        return *this;
    }

    expected operator=(ValueT &&value)
    {
        destory();
        if (mType == ErrorType) {
            new (&mError) ErrorT(std::move(value));
        } else {
            new (&mValue) ValueT(std::move(value));
        }
        return *this;
    }

    expected operator=(const ValueT &value)
    {
        destory();
        if (mType == ErrorType) {
            new (&mError) ErrorT(value);
        } else {
            new (&mValue) ValueT(value);
        }
        return *this;
    }

    operator bool() const CS_NOEXCEPT { return !mType; }

    ValueT &operator*() { return mValue; }
    const ValueT &operator*() const { return mValue; }
    ValueT *operator->() { return &mValue; }
    const ValueT *operator->() const { return &mValue; }

    bool has_value() const { return !mType; }
    ValueT &value() { return mValue; }
    const ValueT &value() const { return mValue; }
    ValueT &value_or(ValueT &value) { return mType ? value : mValue; }
    const ValueT &value_or(const ValueT &value) const { return mType ? value : mValue; }

    ErrorT error() { return mError; }
    const ErrorT &error() const { return mError; }

    ~expected() { destory(); }

private:
    inline void destory()
    {
        if (mType == ErrorType) {
            mError.~ErrorT();
        } else {
            mValue.~ValueT();
        }
    }
};

template <typename T, typename E>
class expected<T, E, typename std::enable_if<std::is_void<T>::value>::type> {
public:
    using ValueT = T;
    using ErrorT = E;
    using UnexpectedT = Unexpected<E>;

private:
    ErrorT mError;

    enum {
        ValueType,
        ErrorType,
    } mType;

public:
    expected()
        : mType(ValueType)
    {
    }

    expected(const ErrorT &error)
        : mType(ErrorType)
        , mError(error)
    {
    }

    expected(ErrorT &&error)
        : mType(ErrorType)
        , mError(std::move(error))
    {
    }

    expected(const expected &other) CS_NOEXCEPT
        : mType(other.mType),
          mError(other.mError)
    {
    }

    expected(const UnexpectedT &error)
        : mType(ErrorType)
        , mError(error.error())
    {
    }

    expected(UnexpectedT &&error)
        : mType(ErrorType)
        , ErrorT(error.error());
    {
    }

    expected operator=(const UnexpectedT &error)
    {
        mType = ErrorType;
        mError = error.error();
        return *this;
    }

    expected operator=(const expected &other) CS_NOEXCEPT
    {
        if (other.mType == ErrorType) {
            mError = other.mError;
            mType = ErrorType;
        } else {
            mType = ValueType;
        }
        return *this;
    }

    expected operator=(const ErrorT &error) CS_NOEXCEPT
    {
        mType = ErrorType;
        mError = error;
        return *this;
    }

    expected operator=(ErrorT &&error) CS_NOEXCEPT
    {
        mType = ErrorType;
        mError = std::move(error);
        return *this;
    }

    operator bool() const CS_NOEXCEPT { return !mType; }

    bool has_value() const { return !mType; }
    ValueT value() const { }
    ValueT value_or() const { }

    ErrorT &error() { return mError; }
    const ErrorT &error() const { return mError; }

    ~expected() = default;
};
}

#endif

ILIAS_NS_END