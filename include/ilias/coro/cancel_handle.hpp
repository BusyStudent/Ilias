#pragma once

#include "coro_handle.hpp"
#include "promise.hpp"

ILIAS_NS_BEGIN

/**
 * @brief Handle used to observe the running task
 * 
 */
class CancelHandle {
public:
    explicit CancelHandle(CoroHandle handle) : mCoro(handle) { }
    CancelHandle(const CancelHandle &) = delete;
    CancelHandle(CancelHandle &&other) : mCoro(other.mCoro) { other.mCoro = nullptr; }
    CancelHandle(std::nullptr_t) { }
    CancelHandle() = default;
    ~CancelHandle() { clear(); }

    auto clear() -> void;
    auto cancel() -> CancelStatus;
    auto isDone() const -> bool;
    auto isCanceled() const -> bool;
    auto operator =(CancelHandle &&h) -> CancelHandle &;
    auto operator =(std::nullptr_t) -> CancelHandle &;

    explicit operator bool() const noexcept { return bool(mCoro); }
protected:
    CoroHandle mCoro = nullptr;
};

// CancelHandle
inline auto CancelHandle::clear() -> void {
    if (!mCoro) {
        return;
    }
    if (!mCoro.done()) {
        // Still not done, we detach it
        mCoro->setDestroyOnDone();
    }
    else {
        mCoro.destroy(); //< Done, we destroy 
    }
    mCoro = nullptr;
}
inline auto CancelHandle::cancel() -> CancelStatus {
    if (mCoro) {
        return mCoro.cancel();
    }
    return CancelStatus::Done;
}
inline auto CancelHandle::isDone() const -> bool {
    return mCoro.done();
}
inline auto CancelHandle::isCanceled() const -> bool {
    return mCoro.isCanceled();
}
inline auto CancelHandle::operator =(CancelHandle &&other) -> CancelHandle & {
    if (this == &other) {
        return *this;
    }
    clear();
    mCoro = other.mCoro;
    other.mCoro = nullptr;
    return *this;
}
inline auto CancelHandle::operator =(std::nullptr_t) -> CancelHandle & {
    clear();
    return *this;
}

ILIAS_NS_END