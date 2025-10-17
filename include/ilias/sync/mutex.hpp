#pragma once

#include <ilias/sync/detail/queue.hpp> // WaitQueue, WaitAwaiter
#include <ilias/runtime/coro.hpp>
#include <ilias/log.hpp>
#include <optional> // std::optional
#include <atomic> // std::atomic<bool>

ILIAS_NS_BEGIN

class Mutex;
class MutexGuard;

namespace sync {

class [[nodiscard]] MutexAwaiter : public WaitAwaiter<MutexAwaiter> {
public:
    MutexAwaiter(Mutex &m);

    auto await_ready() -> bool;
    auto await_resume() -> MutexGuard;
    auto onWakeup() -> bool; // Do try lock here
private:
    auto tryLock() -> bool;

    Mutex &mMutex;
friend class Mutex;
};

} // namespace sync


/**
 * @brief The mutex lock guard, unlock when the object is destroyed
 * 
 */
class [[nodiscard]] MutexGuard {
public:
    explicit MutexGuard(Mutex &m);
    MutexGuard(const MutexGuard &) = delete;
    MutexGuard(MutexGuard &&);
    ~MutexGuard();

    auto unlock() -> void;
    auto release() -> void;
private:
    Mutex *mMutex = nullptr;
};

/**
 * @brief The locked guard, user can access the value by it
 * 
 * @tparam T 
 */
template <typename T>
class [[nodiscard]] LockedGuard {
public:
    explicit LockedGuard(MutexGuard guard, T &value) : mGuard(std::move(guard)), mValue(&value) {};
    LockedGuard(const LockedGuard &) = delete;
    LockedGuard(LockedGuard &&) = default;
    ~LockedGuard() = default;

    auto operator ->() const noexcept -> T * { return mValue; }
    auto operator *() const noexcept -> T & { return *mValue; }
    auto get() const noexcept -> T * { return mValue; }

    auto unlock() -> void { mGuard.unlock(); mValue = nullptr; }
    auto release() -> void { mGuard.release(); mValue = nullptr; }
private:
    MutexGuard mGuard;
    T         *mValue = nullptr;
};

/**
 * @brief The coroutine mutex, it is thread-safe
 * 
 */
class Mutex {
public:
    Mutex(const Mutex &) = delete;
    Mutex(Mutex &&) = delete;
    Mutex() = default;
    ~Mutex() = default;

    /**
     * @brief Check if the mutex is locked, internal use for assert
     * 
     * @return true 
     * @return false 
     */
    auto isLocked() const noexcept -> bool { return mLocked.load(std::memory_order_relaxed); }

    /**
     * @brief Try to lock the mutex
     * 
     * @return std::optional<MutexGuard> 
     */
    [[nodiscard]]
    auto tryLock() noexcept -> std::optional<MutexGuard> {
        bool expected = false;
        if (mLocked.compare_exchange_strong(
                expected, 
                true, 
                std::memory_order_acquire, 
                std::memory_order_relaxed
            )) 
        {
            return MutexGuard(*this);
        }
        return std::nullopt;
    }

    /**
     * @brief Manually unlock the mutex, if will crash if the mutex is not locked
     * @note It is not recommend to use this function directly, use RAII lock instead
     * 
     */
    auto unlockRaw() -> void {
        auto locked = mLocked.exchange(false, std::memory_order_release);
        ILIAS_ASSERT_MSG(locked, "Unlock a unlocked mutex");
        mQueue.wakeupOne();
    }

    /**
     * @brief Lock the mutex, return the lock guard
     * 
     * @return MutexGuard 
     */
    [[nodiscard]]
    auto lock() noexcept {
        return sync::MutexAwaiter(*this);
    }

    /**
     * @brief Lock the mutex, return the lock guard
     * 
     * @return MutexGuard 
     */
    [[nodiscard]]
    auto operator co_await() noexcept {
        return lock();
    }
private:
    sync::WaitQueue   mQueue;
    std::atomic<bool> mLocked {false};
friend class sync::MutexAwaiter;
};

/**
 * @brief The locked value, protect the value by mutex
 * 
 * @tparam T 
 */
template <typename T>
class Locked {
public:
    template <typename... Args> requires (std::constructible_from<T, Args...>)
    Locked(Args &&... args) : mValue(std::forward<Args>(args)...) {}
    Locked(const Locked &) = delete;
    ~Locked() = default;

    auto isLocked() const noexcept -> bool { return mMutex.isLocked(); }

    /**
     * @brief Try to lock the mutex, if the mutex is locked, return nullopt
     * 
     * @return std::optional<LockedGuard<T> > 
     */
    [[nodiscard]]
    auto tryLock() noexcept -> std::optional<LockedGuard<T> > {
        auto guard = mMutex.tryLock();
        if (!guard) {
            return std::nullopt;
        }
        return LockedGuard<T>(std::move(*guard), mValue);
    }

    /**
     * @brief Lock the value, return the lock guard, access the value by the guard
     * 
     * @return LockedGuard<T> 
     */
    [[nodiscard]]
    auto lock() noexcept {
        struct Awaiter final : sync::MutexAwaiter {
            Awaiter(Locked &l) : sync::MutexAwaiter(l.mMutex), mValue(l.mValue) {}

            auto await_resume() -> LockedGuard<T> {
                return LockedGuard<T>(sync::MutexAwaiter::await_resume(), mValue);
            }

            T &mValue;
        };
        return Awaiter(*this);
    }

    [[nodiscard]]
    auto operator co_await() noexcept {
        return lock();
    }
private:
    Mutex mMutex;
    T     mValue;
};

// Implementation
inline MutexGuard::MutexGuard(Mutex &m) : mMutex(&m) {
    ILIAS_ASSERT(mMutex->isLocked());
}

inline MutexGuard::MutexGuard(MutexGuard &&o) : mMutex(o.mMutex) {
    o.mMutex = nullptr;
}

inline MutexGuard::~MutexGuard() {
    unlock();
}

inline auto MutexGuard::unlock() -> void {
    if (mMutex) {
        mMutex->unlockRaw();
        mMutex = nullptr;
    }
}

inline auto MutexGuard::release() -> void {
    mMutex = nullptr;
}

inline sync::MutexAwaiter::MutexAwaiter(Mutex &m) : WaitAwaiter(m.mQueue), mMutex(m) {

}

inline auto sync::MutexAwaiter::await_ready() -> bool {
    return tryLock();
}

inline auto sync::MutexAwaiter::await_resume() -> MutexGuard {
    ILIAS_ASSERT(mMutex.isLocked()); // We own the lock
    return MutexGuard(mMutex);
}

inline auto sync::MutexAwaiter::tryLock() -> bool {
    auto lock = mMutex.tryLock();
    if (lock) {
        lock->release(); // Move the lock ownship to the awaiter
        return true;
    }
    return false;
}

inline auto sync::MutexAwaiter::onWakeup() -> bool {
    return tryLock();
}


ILIAS_NS_END