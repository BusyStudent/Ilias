// INTERNAL!!!
#pragma once

#include <ilias/defines.hpp>
#include <exception>
#include <utility>

ILIAS_NS_BEGIN

namespace runtime {

#if defined(__cpp_exceptions)

/**
 * @brief The std::exception_ptr wrapper.
 * 
 */
class ExceptionPtr {
public:
    ExceptionPtr() = default;
    ExceptionPtr(std::nullptr_t) {}
    ExceptionPtr(const ExceptionPtr &) = default;
    ExceptionPtr(ExceptionPtr &&other) = default;
    ExceptionPtr(std::exception_ptr ptr) : mPtr(std::move(ptr)) {}

    // Rethrow the exception if the pointer is valid
    auto rethrowIfAny() -> void {
        if (mPtr) {
            std::rethrow_exception(mPtr);
        }
    }

    auto toStd() -> std::exception_ptr {
        return mPtr;
    }

    // Operator
    auto operator ==(const ExceptionPtr &other) const -> bool = default;
    auto operator =(ExceptionPtr &&other) -> ExceptionPtr & = default;

    // Check is valid
    explicit operator bool() const noexcept {
        return bool(mPtr);
    }

    // Get the current exception
    [[nodiscard]]
    static auto currentException() -> ExceptionPtr {
        return std::current_exception();
    }
private:
    std::exception_ptr mPtr;
};

#else

/**
 * @brief The empty exception pointer, currently disabled
 * 
 */
class ExceptionPtr {
public:
    ExceptionPtr() = default;
    ExceptionPtr(std::nullptr_t) {}
    ExceptionPtr(const ExceptionPtr &) = default;
    ExceptionPtr(ExceptionPtr &&other) = default;

    auto rethrowIfAny() {}

    auto operator <=>(const ExceptionPtr &) const = default;
    auto operator =(ExceptionPtr &&other) -> ExceptionPtr & = default;

    explicit operator bool() const noexcept {
        return false;
    }

    [[nodiscard]]
    static auto currentException() -> ExceptionPtr {
        return {};
    }
};

#endif // __cpp_exceptions

} // namespace runtime

ILIAS_NS_END