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
    CoroHandle(std::coroutine_handle<T> handle) : mHandle(handle), mCoro(&handle.promise()) { }
    // FIXME: ? Impl noop coroutine as null promise ?
    CoroHandle(std::noop_coroutine_handle handle) : mHandle(handle), mCoro(nullptr) { }
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
        ILIAS_ASSERT(mCoro);
        return static_cast<T &>(*mCoro);
    }

    /**
     * @brief Get the promise pointer of the coro, may be null
     * 
     * @tparam T 
     * @return T* 
     */
    template <_IsPromise T = CoroPromise>
    auto promisePtr() const -> T * {
        return static_cast<T *>(mCoro);
    }

    /**
     * @brief Get or cast the raw handle of the coro
     * 
     * @tparam T 
     * @return std::coroutine_handle<T> 
     */
    template <typename T = void>
    auto stdHandle() const -> std::coroutine_handle<T> {
        if (!mHandle) return nullptr;
        auto addr = mHandle.address();
        return std::coroutine_handle<T>::from_address(addr);
    }

    // --- Obverse
    auto isCanceled() const { return promise().isCanceled(); }

    auto isResumeable() const { return promise().isResumable(); }

    auto isSuspended() const { return promise().isSuspended(); }

    auto isStarted() const { return promise().isStarted(); }

    auto isNoop() const { return mCoro == nullptr; }

    auto isDone() const { return stdHandle().done(); }

    auto done() const { return stdHandle().done(); }

    auto resume() const { ILIAS_ASSERT(!isNoop() && isResumeable());  return stdHandle().resume(); }

    auto cancel() const { return promise().cancel(); }

    auto destroy() const { return stdHandle().destroy(); }

    auto operator <=>(const CoroHandle &) const = default;

    auto operator = (const CoroHandle &) -> CoroHandle & = default;

    auto operator = (std::nullptr_t) -> CoroHandle & { mCoro = nullptr; mHandle = nullptr; return *this; }
    
    auto operator ->() const -> CoroPromise * { ILIAS_ASSERT(mCoro); return mCoro; }

    /**
     * @brief Create a coroutine handle from a coro promise
     * 
     * @param p 
     * @return CoroHandle 
     */
    static auto fromPromise(CoroPromise *p) -> CoroHandle {
        CoroHandle h;
        h.mHandle = p->handle();
        h.mCoro = p;
        return h;
    }

    /**
     * @brief Check if the coro is valid
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const { return bool(mHandle); }
private:
    std::coroutine_handle<> mHandle = nullptr; //< The type erased hand;e
    CoroPromise            *mCoro = nullptr; //< The coro's promise
};

ILIAS_NS_END

/**
 * @brief Hash function for coro handle
 * 
 * @tparam  
 */
template <>
struct std::hash<ILIAS_NAMESPACE::CoroHandle> {
    auto operator ()(const ILIAS_NAMESPACE::CoroHandle &h) const noexcept {
        using type1 = decltype(h.promisePtr());
        using type2 = decltype(h.stdHandle());
        auto hash1 = std::hash<type1>{}(h.promisePtr());
        auto hash2 = std::hash<type2>{}(h.stdHandle());
        // Combine the two hashes
        return hash1 ^ (hash2 << 1);
    }
};