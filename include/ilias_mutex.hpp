#pragma once

#include "ilias_task.hpp"
#include "ilias_await.hpp"
#include <mutex> //< For unique_lock
#include <list>

ILIAS_NS_BEGIN

class MutexGuardAwaiter;
class MutexAwaiter;

/**
 * @brief The coroutine mutex, only for single thread now
 * 
 */
class Mutex {
public:
    Mutex() = default;
    Mutex(const Mutex &) = delete;
    ~Mutex() {
        ILIAS_CHECK_MSG(mWatingQueue.empty(), "Still someone waiting on the mutex, ill-formed code !!!");
        ILIAS_CHECK_MSG(!mIsLocked, "Still someone holding the mutex, ill-formed code !!!");
    }

    /**
     * @brief lock the Mutex
     * 
     * @return MutexAwaiter 
     */
    [[nodiscard("Do not forget to use co_await !!!")]]
    auto lock() -> MutexAwaiter;

    /**
     * @brief lock the mutex and return the guard
     * 
     * @return MutexGuardAwaiter 
     */
    [[nodiscard("Do not forget to use co_await !!!")]]
    auto lockGuard() -> MutexGuardAwaiter;

    /**
     * @brief Lock the mutex
     * 
     * @return auto 
     */
    auto unlock() -> void;

    /**
     * @brief Try lock the mutex
     * 
     * @return true 
     * @return false 
     */
    auto tryLock() -> bool;

    /**
     * @brief Try lock the mutex
     * 
     * @return true 
     * @return false 
     */
    auto try_lock() -> bool;

    /**
     * @brief Check the mutex is locked?
     * 
     * @return true 
     * @return false 
     */
    auto isLocked() const noexcept -> bool;
private:
    std::list<MutexAwaiter *> mWatingQueue;
    bool mIsLocked = false;
friend class MutexAwaiter;
};

class MutexAwaiter {
public:
    MutexAwaiter(Mutex &mutex) : mMutex(mutex) { }
    MutexAwaiter(const MutexAwaiter &) = delete;
    MutexAwaiter(MutexAwaiter &&other) = delete; //< Do not allow move, we use the ptr of MutexAwaiter
    ~MutexAwaiter() {
        ILIAS_CHECK_MSG(mUsed, "Do you forget to use co_await to lock the Mutex???");
    }

    auto await_ready() noexcept -> bool {
        mIter = mMutex.mWatingQueue.end();
        mUsed = true;
        return false;
    }
    auto await_suspend(auto handle) -> bool {
        auto &promise = handle.promise();
        if (promise.isCanceled()) {
            return false; //< Cancel the suspend
        }
        if (mMutex.tryLock()) {
            mLocked = true;
            return false;
        }
        mHandle = handle;
        mMutex.mWatingQueue.push_back(this);
        mIter = mMutex.mWatingQueue.rbegin().base();
        return true;
    }
    [[nodiscard("Please check the return value, it may be canceled, so lock is failed")]]
    auto await_resume() -> Result<void> {
        if (mLocked) { //< We successfully locked the mutex
            return {};
        }
        if (mIter != mMutex.mWatingQueue.end()) {
            // Canceled
            mMutex.mWatingQueue.erase(mIter);
            return Unexpected(Error::Canceled);
        }
        // Now we should get the mutex
        ILIAS_ASSERT(mMutex.isLocked());
        return {};
    }
    auto resume() {
        mIter = mMutex.mWatingQueue.end();
        mHandle.resume();
    }
    auto mutex() -> Mutex & {
        return mMutex;
    }
private:
    std::coroutine_handle<> mHandle;
    std::list<MutexAwaiter *>::iterator mIter;
    Mutex &mMutex;
    bool   mUsed = false;
    bool   mLocked = false; //< Try lock 
};

class MutexGuardAwaiter {
public:
    MutexGuardAwaiter(Mutex &mutex) : mAwaiter(mutex) { }

    auto await_ready() { return mAwaiter.await_ready(); }
    auto await_suspend(auto handle) { return mAwaiter.await_suspend(handle); }
    [[nodiscard("Please check the return value, it may be canceled, so lock is failed")]]
    auto await_resume() -> Result<std::unique_lock<Mutex> > { 
        auto v = mAwaiter.await_resume();
        if (!v) {
            return Unexpected(v.error());
        }
        return {std::unique_lock<Mutex> {mAwaiter.mutex(), std::adopt_lock} };
     }
private:
    MutexAwaiter mAwaiter;
};

inline auto Mutex::unlock() -> void {
    ILIAS_CHECK_MSG(isLocked(), "You should not call unlock() if the mutex is not locked");
    if (mWatingQueue.empty()) {
        // Non is wating it
        mIsLocked = false;
        return;
    }
    // Now resume the first one
    auto awaiter = mWatingQueue.front();
    mWatingQueue.pop_front();
    awaiter->resume();
}

inline auto Mutex::tryLock() -> bool {
    if (mIsLocked) {
        return false;
    }
    mIsLocked = true;
    return true;
}

inline auto Mutex::try_lock() -> bool {
    return tryLock();
}

inline auto Mutex::isLocked() const noexcept -> bool {
    return mIsLocked;
}

inline auto Mutex::lock() -> MutexAwaiter {
    return MutexAwaiter {*this};
}

inline auto Mutex::lockGuard() -> MutexGuardAwaiter {
    return MutexGuardAwaiter {*this};
}

ILIAS_NS_END