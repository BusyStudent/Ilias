#include <ilias/runtime/coro.hpp>
#include <ilias/runtime/token.hpp>
#include <ilias/sync/detail/queue.hpp>
#include <ilias/log.hpp>
#include <mutex> // std::lock_guard

ILIAS_NS_BEGIN

using namespace sync;

#pragma region WaitQueue
WaitQueue::WaitQueue() noexcept = default;
WaitQueue::~WaitQueue() {
    ILIAS_ASSERT_MSG(mSem.try_acquire(), "WaitQueue destroyed with locked ?");
    if (!mAwaiters.empty()) {
        ILIAS_ERROR("Sync", "WaitQueue destroyed with awaiters, did you destroy a mutex or event still locked? / waiting?");
        ILIAS_TRAP(); // Try raise the debugger
        std::abort();
    }
}

auto WaitQueue::wakeupOne() -> void {
    auto locker = std::unique_lock {*this};
    for (auto it = mAwaiters.begin(); it != mAwaiters.end(); ++it) {
        auto &awaiter = *it;
        if (awaiter.onWakeupRaw()) { // Got one
            mAwaiters.erase(it);
            locker.unlock();

            // Schedule the coroutine
            awaiter.mCaller.schedule();
            break;
        }
    }
}

auto WaitQueue::wakeupAll() -> void {
    auto ready = intrusive::List<AwaiterBase> {};
    {
        auto locker = std::lock_guard {*this};
        for (auto it = mAwaiters.begin(); it != mAwaiters.end(); ) {
            auto &awaiter = *it;
            if (awaiter.onWakeupRaw()) {
                it = mAwaiters.erase(it); // Move to next
                ready.push_back(awaiter); // Add to the ready list
            }
            else {
                ++it;
            }
        }
    }

    // Now schedule all the coroutines
    while (!ready.empty()) {
        auto &awaiter = ready.front();
        ready.pop_front();
        awaiter.mCaller.schedule();
    }
}

auto WaitQueue::lock() -> void {
    mSem.acquire();
}

auto WaitQueue::unlock() -> void {
    mSem.release();
}

#pragma region AwaiterBase
auto AwaiterBase::await_suspend(runtime::CoroHandle caller) -> void {
    mCaller = caller;
    {
        auto locker = std::lock_guard {*mQueue};
        mQueue->mAwaiters.push_back(*this); // Adding self to the queue's last position
    }

    // Enter race now.
    mReg.register_<&AwaiterBase::onStopRequested>(caller.stopToken(), this);
}

// Use isLinked & mQueue to handle the race condition
auto AwaiterBase::onWakeupRaw() -> bool {
    if (mOnWakeup) {
        if (!mOnWakeup(*this)) {
            return false;
        }
    }
    // Decide to wakeup, set it, wakup win!
    std::atomic_ref {mQueue}.store(nullptr);
    return true;
}

auto AwaiterBase::onStopRequested() -> void {
    auto ref = std::atomic_ref {mQueue};
    if (auto queue = ref.load(); !queue) { // We already got the wakeup, ignore it
        return;
    }
    else {
        auto locker = std::lock_guard {*queue};
        if (!isLinked()) { // Already wakeup
            return;
        }
        unlink(); // Stop request win!
        ref.store(nullptr);
    }
    mCaller.setStopped();
}

ILIAS_NS_END