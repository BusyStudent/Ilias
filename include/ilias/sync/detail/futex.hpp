// INTERNAL!!
#pragma once

#include <ilias/defines.hpp>
#include <atomic> // std::atomic
#include <mutex> // std::mutex

ILIAS_NS_BEGIN

namespace sync {

#if !defined(ILIAS_SYNC_STD_MUTEX)
// For optimization the size purposes, the std::mutex is too large.
class FutexMutex final{
public:
    constexpr FutexMutex() = default;
    constexpr FutexMutex(const FutexMutex &) = delete;

#if !defined(NDEBUG)
    ~FutexMutex() {
        ILIAS_ASSERT(mState.load() == Unlocked);
    }
#endif // defined(NDEBUG)

    auto lock() noexcept -> void {
        auto expected = Unlocked;

        // Fast path, try to acquire the lock
        if (mState.compare_exchange_weak(expected, Locked, std::memory_order_acquire)) {
            return;
        }

        while(true) {
            if (expected == Locked) { // Lock, try to add waiting mark
                if (mState.compare_exchange_weak(expected, LockedWithWaiters, std::memory_order_acquire)) {
                    expected = LockedWithWaiters; // Successfully update the mark
                }
            }

            if (expected == LockedWithWaiters) { // Only do wait if we actually updated the mark
                mState.wait(expected, std::memory_order_relaxed);
            }

            // Try again
            expected = Unlocked;

            if (mState.compare_exchange_weak(expected, Locked, std::memory_order_acquire)) {
                // Got it
                return;
            }
        }
    }

    auto unlock() noexcept -> void {
        auto prev = mState.exchange(Unlocked, std::memory_order_release);
        ILIAS_ASSERT(prev != Unlocked); // Unlock an unlocked mutex is Prohhibited
        if (prev == LockedWithWaiters) {
            // Must use notify_all(). With notify_one(), a woken thread (T2) can acquire the lock,
            // and then unlock without notifying anyone, leaving other waiting threads (T3) abandoned.
            mState.notify_all();
        }
    }
private:
    enum State : uint8_t {
        Unlocked          = 0,
        Locked            = 1,
        LockedWithWaiters = 2,
    };
    std::atomic<State> mState {Unlocked};
};
#else
// Fallback to std::mutex
using FutexMutex = std::mutex;
#endif // defined(ILIAS_SYNC_STD_MUTEX)

} // namespace sync

ILIAS_NS_END