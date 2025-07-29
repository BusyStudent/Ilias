#pragma once

#include <ilias/sync/detail/queue.hpp> // WaitQueue, WaitAwaiter
#include <ilias/runtime/coro.hpp>
#include <ilias/log.hpp>
#include <optional> // std::optional

ILIAS_NS_BEGIN

class Mutex;
class MutexLock;

namespace sync {

class [[nodiscard]] MutexAwaiter : public WaitAwaiter<MutexAwaiter> {
public:
    MutexAwaiter(Mutex &m);

    auto await_ready() const -> bool;
    auto await_resume() -> MutexLock;
private:
    Mutex &mMutex;
friend class Mutex;
};

} // namespace sync


/**
 * @brief The mutex lock guard, unlock when the object is destroyed
 * 
 */
class [[nodiscard]] MutexLock {
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
 * @brief The coroutine mutex, not thread-safe, it only moveable or destroyable when no one await on it
 * 
 */
class Mutex {
public:
    Mutex(const Mutex &) = delete;
    Mutex(Mutex &&) = delete;
    Mutex() = default;
    ~Mutex() = default;

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
    [[nodiscard]]
    auto tryLock() noexcept -> std::optional<MutexLock> {
        if (mLocked) {
            return std::nullopt;
        }
        mLocked = true;
        return MutexLock(*this);
    }

    /**
     * @brief Manually unlock the mutex, if will crash if the mutex is not locked
     * @note It is not recommend to use this function directly, use RAII lock instead
     * 
     */
    auto unlockRaw() -> void {
        ILIAS_ASSERT_MSG(mLocked, "Unlock a unlocked mutex");
        mLocked = false;
        mQueue.wakeupOne();
    }

    /**
     * @brief Lock the mutex, return the lock guard
     * 
     * @return MutexLock 
     */
    [[nodiscard]]
    auto lock() noexcept {
        return sync::MutexAwaiter(*this);
    }

    /**
     * @brief Lock the mutex, return the Lock
     * 
     * @return MutexLock 
     */
    auto operator co_await() noexcept {
        return sync::MutexAwaiter(*this);
    }
private:
    sync::WaitQueue mQueue;
    bool mLocked = false;
friend class sync::MutexAwaiter;
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
        mMutex->unlockRaw();
        mMutex = nullptr;
    }
}

inline auto MutexLock::release() -> void {
    mMutex = nullptr;
}

inline sync::MutexAwaiter::MutexAwaiter(Mutex &m) : WaitAwaiter(m.mQueue), mMutex(m) {

}

inline auto sync::MutexAwaiter::await_ready() const -> bool {
    return !mMutex.isLocked(); // If the mutex is not locked, we can get the lock immediately
}

inline auto sync::MutexAwaiter::await_resume() -> MutexLock {
    return *mMutex.tryLock();
}


ILIAS_NS_END