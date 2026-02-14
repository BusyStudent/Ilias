// INTERNAL!!
#pragma once

#include <ilias/sync/detail/futex.hpp> // FutexMutex
#include <ilias/detail/intrusive.hpp> // List, Node
#include <ilias/runtime/token.hpp>
#include <ilias/runtime/coro.hpp>
#include <ilias/log.hpp>
#include <concepts> // std::convertible_to
#include <atomic> // std::atomic_ref

ILIAS_NS_BEGIN

namespace sync {

class WaitQueue;

// The common base class for all waiters, (support blocking & async)
class WaiterBase : public intrusive::Node<WaiterBase> {
public:
    WaiterBase(WaitQueue &queue) : mQueue(queue) {}
    WaiterBase(WaiterBase &&) = default;
    ~WaiterBase() {
        ILIAS_ASSERT(!isLinked(), "WaiterBase is destroyed while still in queue, why?, INTERNAL BUG!!");
    }
private:
    auto onWakeupRaw() -> bool; // The Lock was held while calling this function
    auto resume() -> void;

    WaitQueue &mQueue;
    bool     (*mOnWakeup)(WaiterBase &self) = nullptr; // Check the wakup condition, return true if the waiter should be resumed
    bool       mWaiting  = true;
    bool       mBlocking = false; // True on this waiter is blocking on the queue, use this var to wakeup
    runtime::CoroHandle mCaller; // Used by AwaiterBase
template <typename T>
friend class WaitAwaiter;
friend class WaitQueue;
friend class AwaiterBase;
};

// The part of helper awaiter for register the caller to the wait queue, user should not use it directly
class [[nodiscard]] AwaiterBase : public WaiterBase {
public:
    AwaiterBase(WaitQueue &queue) : WaiterBase(queue) {}
    AwaiterBase(AwaiterBase &&) = default;

    // This method will also lock the queue, so make sure the queue is not locked before suspend
    ILIAS_API
    auto await_suspend(runtime::CoroHandle caller) -> bool;
private:
    /*
     * The core concurrency logic revolves around the race between a wakeup
     * (via `onWakeupRaw`) and a stop request (via `onStopRequested`).
     *
     * State Management:
     * - `mWaiting` member: This is used as the primary atomic flag. If it's true,
     *   the awaiter is considered "waiting". If it's set to `false`, it means a
     *   decision has been made (either wakeup or stop has "claimed" the awaiter).
     * - `isLinked()`: This is the ground truth for whether the awaiter is in the
     *   WaitQueue's list. It can only be safely modified while holding the
     *   WaitQueue's lock.
     *
     * The solution uses a "check-lock-check" pattern in `onStopRequested` to
     * safely resolve the race, which is necessary because the `mOnWakeup`
     * predicate can have side effects (like try locking a mutex) that must be
     * atomic with the decision to wake up.
    */
    auto onStopRequested() -> void;

    runtime::StopRegistration mReg;
};

// The wait queue used to manage, FIFO, can't destroy until all waiters are notified
class ILIAS_API WaitQueue {
public:
    WaitQueue() noexcept;
    WaitQueue(const WaitQueue &) = delete;
    ~WaitQueue();

    // Lock / Unlock the queue
    auto lock() -> void;
    auto unlock() -> void;

    // Wakeup one, no-op on empty queue, it will skip the waiter (if not satisfied the predicate) in the queue
    // Precondition: the queue must not be ```locked```
    auto wakeupOne() -> void;
    auto wakeupAll() -> void;
    auto operator =(const WaitQueue &) = delete;

    // Blocking wait, it will block the current thread until the predicate is satisfied
    // Precondition: the queue must not be ```locked```
    template <std::predicate Fn>
    auto blockingWait(Fn pred) -> void;
private:
    intrusive::List<WaiterBase>  mWaiters;
    FutexMutex                   mMutex; // Protect the mWaiters, it is smaller
friend class AwaiterBase;
};

/**
 * @brief Use CRTP to create a waiter
 * 
 * @tparam T must have onWakeup method, return bool (return false on wakeup condition is not satisfied)
 */
template <typename T>
class WaitAwaiter : public AwaiterBase {
public:
    WaitAwaiter(WaitQueue &queue) : AwaiterBase(queue) {
        // Check if T has onWakeup
        if constexpr (requires(T &t) { { t.onWakeup() } -> std::convertible_to<bool>; }) { // return predicate
            mOnWakeup = proxy;
        }
        else { // Illegal return type or not exist
            static_assert(std::is_same_v<void, T>, "T::onWakeup() must exist and return bool");
        }
    }

    // Default, we check the predicate in the suspend method
    auto await_ready() -> bool {
        return false;
    }
private:
    static auto proxy(WaiterBase &self) -> bool {
        return static_cast<T &>(self).onWakeup(); // Call onWakeup
    }
};

// Implementation
template <std::predicate Fn>
auto WaitQueue::blockingWait(Fn pred) -> void {
    struct Waiter : public WaiterBase {
        Waiter(WaitQueue &queue, Fn fn) : WaiterBase(queue), mFn(std::move(fn)) {
            mOnWakeup = proxy;
            mBlocking = true;
        }

        auto suspend() -> void {
            if (mFn()) { // Fast path, check the predicate
                return;
            }
            else {
                auto locker = std::lock_guard {mQueue};
                if (mFn()) { // Atomic check and suspend
                    return;
                }
                mQueue.mWaiters.push_back(*this);
            }

            std::atomic_ref {mBlocking}.wait(true); // Wait it to be set to false
        }

        static auto proxy(WaiterBase &self) -> bool {
            return static_cast<Waiter &>(self).mFn();
        }

        Fn mFn;
    };

// LCOV_EXCL_START
#if !defined(NDEBUG)
    if (runtime::Executor::currentThread() != nullptr) { // Current thread has executor
        static constinit bool once = false;
        if (!once) {
            once = true;
            ILIAS_WARN("Sync", "Current thread have executor, The blockingWait may cause deadlock");
        }
    }
#endif // NDEBUG
// LCOV_EXCL_STOP

    auto waiter = Waiter {*this, std::move(pred)};
    waiter.suspend();
}

} // namespace sync

ILIAS_NS_END