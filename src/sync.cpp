#include <ilias/runtime/coro.hpp>
#include <ilias/runtime/token.hpp>
#include <ilias/sync/detail/queue.hpp>
#include <ilias/log.hpp>

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
    for (auto it = mAwaiters.begin(); it != mAwaiters.end(); ++it) {
        auto &awaiter = *it;
        if (awaiter.onWakeupRaw()) { // Got one
            mAwaiters.erase(it);
            return;
        }
    }
}

auto WaitQueue::wakeupAll() -> void {
    for (auto it = mAwaiters.begin(); it != mAwaiters.end(); ) {
        auto &awaiter = *it;
        if (awaiter.onWakeupRaw()) {
            it = mAwaiters.erase(it);
        }
        else {
            ++it;
        }
    }
}
auto AwaiterBase::await_suspend(runtime::CoroHandle caller) -> void {
    mCaller = caller;
    mQueue.mAwaiters.push_back(*this); // Adding self to the queue's last position
    mReg.register_<&AwaiterBase::onStopRequested>(caller.stopToken(), this);
}

auto AwaiterBase::onWakeupRaw() -> bool {
    if (mOnWakeup) {
        if (!mOnWakeup(*this)) {
            return false;
        }
    }
    // Default behavior is true
    mCaller.schedule();
    return true;
}

auto AwaiterBase::onStopRequested() -> void {
    if (!isLinked()) { //  We already got the wakeup, ignore it
        return;
    }
    unlink(); // Remove self from the queue
    mCaller.setStopped();
}

ILIAS_NS_END