#include <ilias/runtime/coro.hpp>
#include <ilias/runtime/token.hpp>
#include <ilias/sync/detail/queue.hpp>
#include <ilias/log.hpp>
#include <mutex> // std::lock_guard

ILIAS_NS_BEGIN

using namespace sync;

// MARK: WaitQueue
WaitQueue::WaitQueue() noexcept = default;
WaitQueue::~WaitQueue() {
    if (!mWaiters.empty()) {
        ILIAS_ERROR("Sync", "WaitQueue destroyed with waiters, did you destroy a mutex or event still locked? / waiting?");
        ILIAS_TRAP(); // Try raise the debugger
        std::abort();
    }
}

auto WaitQueue::wakeupOne() -> void {
    auto locker = std::unique_lock {*this};
    for (auto it = mWaiters.begin(); it != mWaiters.end(); ++it) {
        auto &waiter = *it;
        if (waiter.onWakeupRaw()) { // Got one
            mWaiters.erase(it);
            locker.unlock();

            // Schedule the waiter
            waiter.resume();
            break;
        }
    }
}

auto WaitQueue::wakeupAll() -> void {
    auto ready = intrusive::List<WaiterBase> {};
    {
        auto locker = std::lock_guard {*this};
        for (auto it = mWaiters.begin(); it != mWaiters.end(); ) {
            auto &waiter = *it;
            if (waiter.onWakeupRaw()) {
                it = mWaiters.erase(it); // Move to next
                ready.push_back(waiter); // Add to the ready list
            }
            else {
                ++it;
            }
        }
    }

    // Now schedule all the waiters
    while (!ready.empty()) {
        auto &waiter = ready.front();
        ready.pop_front();
        waiter.resume();
    }
}

auto WaitQueue::lock() -> void {
    mMutex.lock();
}

auto WaitQueue::unlock() -> void {
    mMutex.unlock();
}

// MARK: WaiterBase
// Use isLinked & mQueue to handle the race condition
auto WaiterBase::onWakeupRaw() -> bool {
    if (!mOnWakeup(*this)) {
        return false;
    }
    // Decide to wakeup, set it, wakup win!
    std::atomic_ref {mWaiting}.store(false);
    return true;
}

inline
auto WaiterBase::resume() -> void {
    auto blocking = std::atomic_ref {mBlocking}; // Is someone blocking wait on it?
    if (blocking.exchange(false)) { // A caller is use blockingWait on it
        blocking.notify_one();
    }
    else { // Is awaiter
        mCaller.schedule();
    }
}

// MARK: AwaiterBase
auto AwaiterBase::await_suspend(runtime::CoroHandle caller) -> bool {
    mCaller = caller;
    {
        auto locker = std::lock_guard {mQueue};
        if (mOnWakeup(*this)) { // Check condition
            return false; // Condition is true, don't wait
        }
        mQueue.mWaiters.push_back(*this); // Adding self to the queue's last position
    }

    // Enter race now.
    mReg.register_<&AwaiterBase::onStopRequested>(caller.stopToken(), this);
    return true;
}

inline
auto AwaiterBase::onStopRequested() -> void {
    auto ref = std::atomic_ref {mWaiting};
    if (!ref.load()) { // We already got the wakeup, ignore it
        return;
    }
    else {
        auto locker = std::lock_guard {mQueue};
        if (!isLinked()) { // Already wakeup
            return;
        }
        unlink(); // Stop request win!
        ref.store(false);
    }
    mCaller.setStopped();
}

ILIAS_NS_END