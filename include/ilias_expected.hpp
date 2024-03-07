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
class Unexpected {
public:
    Unexpected(const E &error)
        : mError(error)
    {
    }

    Unexpected(E &&error)
        : mError(std::move(error))
    {
    }

    const E &error() const { return mError; }

private:
    const E mError;
};
template <typename T, typename E, class enable = void>
class Expected;
template <typename T, typename E>
class Expected<T, E, typename std::enable_if<!std::is_void<T>::value>::type> {
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
    Expected(const ValueT &value)
        : mType(ValueType)
    {
        new (&mValue) ValueT(value);
    }

    Expected(ValueT &&value)
        : mType(ValueType)
    {
        new (&mValue) ValueT(std::move(value));
    }

    Expected(const ErrorT &error)
        : mType(ErrorType)
    {
        new (&mError) ErrorT(error);
    }

    Expected(ErrorT &&error)
        : mType(ErrorType)
    {
        new (&mError) ErrorT(std::move(error));
    }

    Expected(const Expected &other) 
        : mType(other.mType)
    {
        if (mType == ErrorType) {
            new (&mError) ErrorT(other.mError);
        } else {
            new (&mValue) ValueT(other.mValue);
        }
    }

    Expected(const UnexpectedT &error)
        : mType(ErrorType)
    {
        new (&mError) ErrorT(error.error());
    }

    Expected(UnexpectedT &&error)
        : mType(ErrorType)
    {
        new (&mError) ErrorT(error.error());
    }

    Expected &operator=(const UnexpectedT &other) 
    {
        destory();
        mType = ErrorType;
        new (&mError) ErrorT(other.error());
        return *this;
    }
    Expected &operator=(const Expected &other) 
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
    Expected &operator=(ValueT &&value)
    {
        destory();
        mType = ValueType;
        new (&mValue) ValueT(std::move(value));
        return *this;
    }
    Expected &operator=(const ValueT &value)
    {
        destory();
        mType = ValueType;
        new (&mValue) ValueT(value);
        return *this;
    }
    Expected &operator=(ErrorT &&error) 
    {
        destory();
        mType = ErrorType;
        new (&mError) ErrorT(std::move(error));
        return *this;
    }
    Expected &operator=(const ErrorT &error) 
    {
        destory();
        mType = ErrorType;
        new (&mError) ErrorT(error);
        return *this;
    }

    operator bool() const  { return !mType; }
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

    ~Expected() { destory(); }

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
class Expected<T, T, typename std::enable_if<!std::is_void<T>::value>::type> {
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
    Expected(const ValueT &value)
        : mType(ValueType)
    {
        new (&mValue) ValueT(value);
    }

    Expected(ValueT &&value)
        : mType(ValueType)
    {
        new (&mValue) ValueT(std::move(value));
    }

    Expected(const Expected &other) 
        : mType(other.mType)
    {
        if (mType == ValueType) {
            new (&mValue) ValueT(other.mValue);
        } else {
            new (&mError) ErrorT(other.mError);
        }
    }

    Expected(const UnexpectedT &error)
        : mType(ErrorType)
    {
        new (&mError) ErrorT(error.error());
    }

    Expected(UnexpectedT &&error)
        : mType(ErrorType)
    {
        new (&mError) ErrorT(error.error());
    }

    Expected operator=(const UnexpectedT &error)
    {
        destory();
        mType = ErrorType;
        new (&mError) ErrorT(error.error());
        return *this;
    }

    Expected operator=(const Expected &other) 
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

    Expected operator=(ValueT &&value)
    {
        destory();
        if (mType == ErrorType) {
            new (&mError) ErrorT(std::move(value));
        } else {
            new (&mValue) ValueT(std::move(value));
        }
        return *this;
    }

    Expected operator=(const ValueT &value)
    {
        destory();
        if (mType == ErrorType) {
            new (&mError) ErrorT(value);
        } else {
            new (&mValue) ValueT(value);
        }
        return *this;
    }

    operator bool() const  { return !mType; }

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

    ~Expected() { destory(); }

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
class Expected<T, E, typename std::enable_if<std::is_void<T>::value>::type> {
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
    Expected()
        : mType(ValueType)
    {
    }

    Expected(const ErrorT &error)
        : mType(ErrorType)
        , mError(error)
    {
    }

    Expected(ErrorT &&error)
        : mType(ErrorType)
        , mError(std::move(error))
    {
    }

    Expected(const Expected &other) 
        : mType(other.mType),
          mError(other.mError)
    {
    }

    Expected(const UnexpectedT &error)
        : mType(ErrorType)
        , mError(error.error())
    {
    }

    Expected(UnexpectedT &&error)
        : mType(ErrorType)
        , mError(error.error())
    {
    }

    Expected operator=(const UnexpectedT &error)
    {
        mType = ErrorType;
        mError = error.error();
        return *this;
    }

    Expected operator=(const Expected &other) 
    {
        if (other.mType == ErrorType) {
            mError = other.mError;
            mType = ErrorType;
        } else {
            mType = ValueType;
        }
        return *this;
    }

    Expected operator=(const ErrorT &error) 
    {
        mType = ErrorType;
        mError = error;
        return *this;
    }

    Expected operator=(ErrorT &&error) 
    {
        mType = ErrorType;
        mError = std::move(error);
        return *this;
    }

    operator bool() const  { return !mType; }

    bool has_value() const { return !mType; }
    ValueT value() const { }
    ValueT value_or() const { }

    ErrorT &error() { return mError; }
    const ErrorT &error() const { return mError; }

    ~Expected() = default;
};
}

#endif

ILIAS_NS_END