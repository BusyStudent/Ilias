// INTERNAL!!!
#pragma once

#include <ilias/defines.hpp>
#include <stdexcept>
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
    ExceptionPtr(const std::exception_ptr &ptr) : mPtr(ptr) {} // Emm? exception ptr is copy only?

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

    // Get the current exception, only can be called from within a catch block
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
        std::terminate(); // No exceptions when build, but still call this?, so terminate
    }
};

#endif // __cpp_exceptions

} // namespace runtime

ILIAS_NS_END