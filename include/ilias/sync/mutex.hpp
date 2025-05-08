#pragma once

#include <ilias/cancellation_token.hpp>
#include <ilias/task/executor.hpp>
#include <ilias/task/task.hpp>
#include <ilias/log.hpp>
#include <list>

ILIAS_NS_BEGIN

class Mutex;
class MutexLock;

namespace detail {

class MutexAwaiter {
public:
    MutexAwaiter(Mutex &m) : mMutex(m) { }

    auto await_ready() const -> bool;
    
    auto await_suspend(CoroHandle caller) -> void;

    auto await_resume() -> Result<void>;

    auto onGotLock() -> void;
protected:
    auto onCancel() -> void;

    Mutex &mMutex;
    CoroHandle mCaller;
    CancellationToken::Registration mRegistration;
    std::list<MutexAwaiter*>::iterator mIt;
    bool mCanceled = false;
};

class MutexAwaiterEx : public MutexAwaiter {
public:
    using MutexAwaiter::MutexAwaiter;

    auto await_resume() -> Result<MutexLock>;
};

} // namespace detail


/**
 * @brief The mutex lock, unlock when the object is destroyed
 * 
 */
class MutexLock {
public:
    explicit MutexLock(Mutex &m);
    MutexLock(const MutexLock &) = delete;
    MutexLock(MutexLock &&);
    ~MutexLock();

    auto unlock() -> void;
    auto release() -> void;
private:
    Mutex *mMutex = nullptr;
};

/**
 * @brief The coroutine mutex, not thread-safe
 * 
 */
class Mutex {
public:
    Mutex() = default;
    
    Mutex(const Mutex &) = delete;

    ~Mutex() {
        ILIAS_CHECK(!mLocked);
        ILIAS_ASSERT(mAwaiters.empty());
    }

    /**
     * @brief Check if the mutex is locked
     * 
     * @return true 
     * @return false 
     */
    auto isLocked() const noexcept -> bool { return mLocked; }

    /**
     * @brief Try to lock the mutex
     * 
     * @return true 
     * @return false 
     */
    auto tryLock() noexcept -> bool {
        if (mLocked) {
            return false;
        }
        mLocked = true;
        return true;
    }

    /**
     * @brief Do Unlock the mutex
     * 
     * @return auto 
     */
    auto unlock() {
        ILIAS_CHECK(mLocked);
        ILIAS_TRACE("Mutex", "Unlock Mutex: {}, Awaiters in waiting list: {}", (void*) this, mAwaiters.size());
        if (!mAwaiters.empty()) {
            auto front = mAwaiters.front();
            mAwaiters.pop_front();
            front->onGotLock();
            return; //< Still locked
        }
        mLocked = false;
    }

    /**
     * @brief Lock the mutex
     * 
     * @return auto 
     */
    [[nodiscard("DO NOT FORGET TO USE co_await!!!")]]
    auto lock() noexcept {
        return detail::MutexAwaiter(*this);
    }

    /**
     * @brief Lock the mutex and return a MutexLock object
     * 
     * @return MutexLock 
     */
    auto uniqueLock() noexcept {
        return detail::MutexAwaiterEx(*this);
    }

    /**
     * @brief Lock the mutex
     * 
     * @return MutexLock 
     */
    auto operator co_await() noexcept {
        return detail::MutexAwaiter(*this);
    }
private:
    std::list<detail::MutexAwaiter *> mAwaiters;
    bool mLocked = false;
friend class detail::MutexAwaiter;
};

inline MutexLock::MutexLock(Mutex &m) : mMutex(&m) {
    ILIAS_ASSERT(mMutex->isLocked());
}

inline MutexLock::MutexLock(MutexLock &&o) : mMutex(o.mMutex) {
    o.mMutex = nullptr;
}

inline MutexLock::~MutexLock() {
    unlock();
}

inline auto MutexLock::unlock() -> void {
    if (mMutex) {
        mMutex->unlock();
        mMutex = nullptr;
    }
}

inline auto MutexLock::release() -> void {
    mMutex = nullptr;
}

inline auto detail::MutexAwaiter::await_ready() const -> bool {
    // ILIAS_TRACE("Mutex", "Try to lock Mutex: {} Awaiter: {}", (void*) &mMutex, (void*) this);
    return mMutex.tryLock();
}

inline auto detail::MutexAwaiter::await_suspend(CoroHandle caller) -> void {
    // Add self to the waiting list
    ILIAS_TRACE("Mutex", "Waiting Mutex: {} Awaiter: {}, Calling stack: {}", (void*) &mMutex, (void*) this, backtrace(caller));
    mCaller = caller;
    mRegistration = mCaller.cancellationToken().register_([this]() { onCancel(); });
    mMutex.mAwaiters.push_back(this);
    mIt = mMutex.mAwaiters.end();
    --mIt;
}

inline auto detail::MutexAwaiter::await_resume() -> Result<void> {
    if (mCanceled) {
        return Unexpected(Error::Canceled);
    }
    // ILIAS_TRACE("Mutex", "Got lock Mutex: {} Awaiter: {}", (void*) &mMutex, (void*) this);
    return {};
}

inline auto detail::MutexAwaiter::onGotLock() -> void {
    mIt = mMutex.mAwaiters.end(); //< Already removed
    mCaller.schedule();
}

inline auto detail::MutexAwaiter::onCancel() -> void {
    if (mIt == mMutex.mAwaiters.end()) { //< Already got lock
        return;
    }
    mMutex.mAwaiters.erase(mIt);
    mIt = mMutex.mAwaiters.end();
    mCanceled = true;
    mCaller.schedule();
    ILIAS_TRACE("Mutex", "Canceled Mutex: {} Awaiter: {}", (void*) &mMutex, (void*) this);
}

inline auto detail::MutexAwaiterEx::await_resume() -> Result<MutexLock> {
    if (auto ret = MutexAwaiter::await_resume(); !ret) {
        return Unexpected(ret.error());
    }
    return MutexLock {mMutex};
}


ILIAS_NS_END