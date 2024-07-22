#pragma once

#include "promise.hpp"
#include <coroutine>

ILIAS_NS_BEGIN

template <typename T>
concept _IsPromise = std::is_base_of_v<CoroPromise, T>;

/**
 * @brief A coro handle wrapper erase the promise type
 * 
 */
class CoroHandle {
public:
    CoroHandle() = default;
    template <_IsPromise T>
    CoroHandle(std::coroutine_handle<T> handle) : mCoro(&handle.promise()) { }
    CoroHandle(std::coroutine_handle<>) = delete;
    CoroHandle(std::nullptr_t) { }
    CoroHandle(const CoroHandle &) = default;
    ~CoroHandle() = default;

    /**
     * @brief Get the promise of the coro
     * 
     * @tparam T 
     * @return T& 
     */
    template <_IsPromise T = CoroPromise>
    auto promise() const -> T & {
        return static_cast<T &>(*mCoro);
    }

    /**
     * @brief Get or cast the raw handle of the coro
     * 
     * @tparam T 
     * @return std::coroutine_handle<T> 
     */
    template <typename T = void>
    auto stdHandle() const -> std::coroutine_handle<T> {
        if (!mCoro) return nullptr;
        auto addr = mCoro->handle().address();
        return std::coroutine_handle<T>::from_address(addr);
    }

    // --- Obverse
    auto isCanceled() const { return mCoro->isCanceled(); }

    auto isResumeable() const { return mCoro->isResumable(); }

    auto isSuspended() const { return mCoro->isSuspended(); }

    auto isStarted() const { return mCoro->isStarted(); }

    auto done() const { return stdHandle().done(); }

    auto resume() const { ILIAS_ASSERT(isResumeable());  return stdHandle().resume(); }

    auto cancel() const { return mCoro->cancel(); }

    auto destroy() const { return stdHandle().destroy(); }

    auto operator <=>(const CoroHandle &) const = default;

    auto operator = (const CoroHandle &) -> CoroHandle & = default;

    auto operator = (std::nullptr_t) -> CoroHandle & { mCoro = nullptr; return *this; }
    
    auto operator ->() const -> CoroPromise * { return mCoro; }

    /**
     * @brief Check if the coro is valid
     * 
     * @return true 
     * @return false 
     */
    operator bool() const { return mCoro != nullptr; }
private:
    CoroPromise *mCoro = nullptr;
};

ILIAS_NS_END