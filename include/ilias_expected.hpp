#pragma once

#include <type_traits>

#if __cplusplus >= 202002L
#include <version>
#endif
#ifdef __cpp_lib_expected
#include <expected>
#define ILIAS_STD_EXPECTED_HPP
#endif

#include "ilias.hpp"

ILIAS_NS_BEGIN

#ifdef ILIAS_STD_EXPECTED_HPP
template <typename T, typename E>
using Expected = ::std::expected<T, E>;
template <typename E>
using Unexpected = ::std::unexpected<E>;

#else
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
    using value_type = T;
    using error_type = E;
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

    Expected(Expected &&other) 
        : mType(std::move(other.mType))
    {
        if (mType == ErrorType) {
            new (&mError) ErrorT(std::move(other.mError));
        } else {
            new (&mValue) ValueT(std::move(other.mValue));
        }
    }

    template <typename U>
    Expected(const Unexpected<U> &error)
        : mType(ErrorType)
    {
        new (&mError) ErrorT(error.error());
    }

    template <typename U>
    Expected(Unexpected<U> &&error)
        : mType(ErrorType)
    {
        new (&mError) ErrorT(error.error());
    }

    template <typename U>
    Expected &operator=(const Unexpected<U> &other) 
    {
        destory();
        mType = ErrorType;
        new (&mError) ErrorT(other.error());
        return *this;
    }

    template <typename U>
    Expected &operator=(Unexpected<U> &&other) 
    {
        destory();
        mType = ErrorType;
        new (&mError) ErrorT(std::move(other.error()));
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

    Expected &operator=(Expected &&other) 
    {
        destory();
        mType = std::move(other.mType);
        if (mType == ErrorType) {
            new (&mError) ErrorT(std::move(other.mError));
        } else {
            new (&mValue) ValueT(std::move(other.mValue));
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
    using value_type = T;
    using error_type = T;
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

    Expected(Expected &&other) 
        : mType(std::move(other.mType))
    {
        if (mType == ValueType) {
            new (&mValue) ValueT(std::move(other.mValue));
        } else {
            new (&mError) ErrorT(std::move(other.mError));
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

    Expected operator=(UnexpectedT &&error)
    {
        destory();
        mType = ErrorType;
        new (&mError) ErrorT(std::move(error.error()));
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

    Expected operator=(Expected &&other) 
    {
        destory();
        mType = std::move(other.mType);
        if (mType == ErrorType) {
            new (&mError) ErrorT(std::move(other.mError));
        } else {
            new (&mValue) ValueT(std::move(other.mValue));
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
    using value_type = T;
    using error_type = E;
private:
    enum {
        ValueType,
        ErrorType,
    } mType;

    ErrorT mError;
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

    Expected(Expected &&other) 
        : mType(std::move(other.mType)),
          mError(std::move(other.mError))
    {
    }

    template <typename U>
    Expected(const Unexpected<U> &error)
        : mType(ErrorType)
        , mError(error.error())
    {
    }

    template <typename U>
    Expected(Unexpected<U> &&error)
        : mType(ErrorType)
        , mError(error.error())
    {
    }

    template <typename U>
    Expected operator=(const Unexpected<U> &error)
    {
        mType = ErrorType;
        mError = error.error();
        return *this;
    }

    template <typename U>
    Expected operator=(Unexpected<U> &&error)
    {
        mType = ErrorType;
        mError = std::move(error.error());
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

    Expected operator=(Expected &&other) 
    {
        mType = std::move(other.mType);
        if (mType == ErrorType) {
            mError = std::move(other.mError);
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