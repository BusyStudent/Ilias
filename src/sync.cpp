#include <ilias/runtime/coro.hpp>
#include <ilias/runtime/token.hpp>
#include <ilias/sync/detail/queue.hpp>
#include <ilias/log.hpp>

ILIAS_NS_BEGIN

using namespace sync;

#pragma region WaitQueue
WaitQueue::WaitQueue() noexcept = default;
WaitQueue::~WaitQueue() {
    if (!mAwaiters.empty()) {
        ILIAS_ERROR("Sync", "WaitQueue destroyed with awaiters, did you destroy a mutex or event still locked? / waiting?");
        ILIAS_TRAP(); // Try raise the debugger
        std::abort();
    }
}

auto WaitQueue::wakeupOne() -> void {
    if (!mAwaiters.empty()) {
        auto &awaiter = mAwaiters.front();
        mAwaiters.pop_front();
        awaiter.onWakeupRaw();
    }
}

auto WaitQueue::wakeupAll() -> void {
    for (auto it = mAwaiters.begin(); it != mAwaiters.end(); ) {
        auto &awaiter = *it;
        it = mAwaiters.erase(it);
        awaiter.onWakeupRaw();
    }
}
auto AwaiterBase::await_suspend(runtime::CoroHandle caller) -> void {
    mCaller = caller;
    mQueue.mAwaiters.push_back(*this); // Adding self to the queue's last position
    mReg.register_<&AwaiterBase::onStopRequested>(caller.stopToken(), this);
}

auto AwaiterBase::onWakeupRaw() -> void {
    mCaller.schedule();
    if (mOnWakeup) {
        mOnWakeup(*this);
    }
}

auto AwaiterBase::onStopRequested() -> void {
    if (!isLinked()) { //  We already got the wakeup, ignore it
        return;
    }
    unlink(); // Remove self from the queue
    mCaller.setStopped();
}

ILIAS_NS_END