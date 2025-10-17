// INTERNAL!!
#pragma once

#include <ilias/detail/intrusive.hpp> // List, Node
#include <ilias/runtime/token.hpp>
#include <ilias/runtime/coro.hpp>
#include <semaphore> // std::binary_semaphore
#include <concepts> // srd::convertible_to

ILIAS_NS_BEGIN

namespace sync {

class WaitQueue;

// The part of helper awaiter for register the caller to the wait queue, user should not use it directly
class [[nodiscard]] AwaiterBase : public intrusive::Node<AwaiterBase> {
public:
    AwaiterBase(WaitQueue &queue) : mQueue(&queue) {}
    AwaiterBase(AwaiterBase &&) = default;
    ~AwaiterBase() { ILIAS_ASSERT(!isLinked()); }

    // This method will also lock the queue, so make sure the queue is not locked before suspend
    ILIAS_API
    auto await_suspend(runtime::CoroHandle caller) -> void;
private:
    /*
     * The core concurrency logic revolves around the race between a wakeup
     * (via `onWakeupRaw`) and a stop request (via `onStopRequested`).
     *
     * State Management:
     * - `mQueue` pointer: This is used as the primary atomic flag. If it's non-null,
     *   the awaiter is considered "waiting". If it's set to `nullptr`, it means a
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
    auto onWakeupRaw() -> bool; // The Lock was held while calling this function
    auto onStopRequested() -> void;

    WaitQueue *mQueue;
    runtime::CoroHandle mCaller;
    runtime::StopRegistration mReg;
    bool (*mOnWakeup)(AwaiterBase &self) = nullptr; // Additional wakeup handler for child classes, return false on wakeup condition is not satisfied
template <typename T>
friend class WaitAwaiter;
friend class WaitQueue;
};

// The wait queue used to manage, FIFO, can't destroy until all awaiters are notified
class ILIAS_API WaitQueue {
public:
    WaitQueue() noexcept;
    WaitQueue(const WaitQueue &) = delete;
    ~WaitQueue();

    // Lock / Unlock the queue
    auto lock() -> void;
    auto unlock() -> void;

    // Wakeup one, no-op on empty queue, it will skip the awaiter (if not satisfied the predicate) in the queue
    // Don't call this function if the queue is locked, it will ```DEADLOCK```
    auto wakeupOne() -> void;
    auto wakeupAll() -> void;
    auto operator =(const WaitQueue &) = delete;
private:
    intrusive::List<AwaiterBase> mAwaiters;
    std::binary_semaphore        mSem {1}; // Used as a mutex to protect the mAwaiters
friend class AwaiterBase;
};

template <typename T>
class WaitAwaiter : public AwaiterBase {
public:
    WaitAwaiter(WaitQueue &queue) : AwaiterBase(queue) {
        // Check if T has onWakeup
        if constexpr (requires(T &t) { { t.onWakeup() } -> std::convertible_to<bool>; }) { // return predicate
            mOnWakeup = proxy;
        }
        else if constexpr (requires(T &t) { { t.onWakeup() } -> std::same_as<void>; }) { // no predicate
            mOnWakeup = proxy2;
        }
        else if constexpr (requires(T &t) { t.onWakeup(); }) { // illegal return type
            static_assert(std::is_same_v<void, T>, "T::onWakeup() must return bool or void");
        }
    }
private:
    template <char = 0>
    static auto proxy(AwaiterBase &self) -> bool {
        return static_cast<T &>(self).onWakeup(); // Call onWakeup
    }

    template <char = 0>
    static auto proxy2(AwaiterBase &self) -> bool {
        static_cast<T &>(self).onWakeup(); // Call onWakeup
        return true;
    }
};

} // namespace sync

ILIAS_NS_END