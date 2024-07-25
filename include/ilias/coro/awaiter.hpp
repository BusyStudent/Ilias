#pragma once

#include "../detail/expected.hpp"
#include "promise.hpp"
#include "coro_handle.hpp"

ILIAS_NS_BEGIN

template <typename T>
concept Awaiter = requires(T t) {
    t.ready();
    t.suspend(CoroHandle{});
    t.resume();
    t.cancel();
};

template <typename T>
inline bool _AwaiterReadyNothrow = noexcept(decltype(std::declval<T>().ready())());
template <typename T> 
inline bool _AwaiterSuspendNothrow = noexcept(decltype(std::declval<T>().suspend(CoroHandle{}))());
template <typename T> 
inline bool _AwaiterResumeNothrow = noexcept(decltype(std::declval<T>().resume())());
template <typename T> 
inline bool _AwaiterCancelNothrow = noexcept(decltype(std::declval<T>().cancel())());

/**
 * @brief Impl the Awaiter, add the cancel interface
 * 
 * @tparam T , Must be Awaitable
 */
template <typename T>
class AwaiterImpl {
public:
    /**
     * @brief std check awaiter is ready, we just forward it to T.ready()
     * 
     * @return true 
     * @return false 
     */
    auto await_ready() -> bool { 
        return _self().ready(); 
    }

    /**
     * @brief std check awaiter is suspended, we forward it to T.suspend() and wrap the handle to corohandle, 
     *  and set suspend flags
     * 
     * @param handle 
     * @return auto 
     */
    auto await_suspend(CoroHandle handle) {
        using type = decltype(_self().suspend(handle));
        mHandle = handle; //< Store the handle object
        mHandle->setSuspended(true);

        if constexpr (std::is_same_v<type, void> || std::is_same_v<type, bool>) {
            // Void or Bool
            return _self().suspend(handle);
        }
        else if constexpr (std::is_same_v<type, CoroHandle>) {
            // our coro handle, cast to std version
            return _self().suspend(handle).stdHandle();
        }
        else {
            // std::coro handle, cast to type erased
            return std::coroutine_handle<>(_self().suspend(handle));
        }
    }

    /**
     * @brief std call on resume, we clear the suspend flags and call T.cancel() if the coro is canceled, then call T.resume()
     * 
     * @return auto 
     */
    auto await_resume() {
        if (mHandle && mHandle->isCanceled()) {
            // Do Cancel
            _self().cancel();
        }
        if (mHandle) {
            // Unset the flags
            mHandle->setSuspended(false);
        }
        return _self().resume();
    }
private:
    template <Awaiter U = T>
    auto _self() -> U & {
        return static_cast<U &>(*this);
    }
    CoroHandle mHandle; //< Null on no-suspend
};

/**
 * @brief Alternative to std::suspend_always, just suspend the coro always, user can get notify if the coro is canceled
 * 
 */
class SuspendAlways final : public AwaiterImpl<SuspendAlways> {
public:
    auto ready() const noexcept -> bool { return false; }
    auto suspend(CoroHandle) const noexcept -> void { }
    auto resume() const noexcept -> Result<> { return mResult; }
    auto cancel() noexcept -> void { mResult = Unexpected(Error::Canceled); }
private:
    Result<> mResult;
};

/**
 * @brief Alternative to std::suspend_never, just never suspend the coro, user can get notify if the coro is canceled
 * 
 */
class SuspendNever final : public AwaiterImpl<SuspendNever> {
public:
    auto ready() const noexcept -> bool { return true; }
    auto suspend(CoroHandle) const noexcept -> void { }
    auto resume() const noexcept -> Result<> { return mResult; }
    auto cancel() noexcept -> void { mResult = Unexpected(Error::Canceled); }
private:
    Result<> mResult;
};

static_assert(Awaiter<SuspendAlways>);
static_assert(Awaiter<SuspendNever>);

ILIAS_NS_END