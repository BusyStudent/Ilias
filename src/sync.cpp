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
        std::abort();
    }
}

auto WaitQueue::wakeupOne() -> void {
    if (!mAwaiters.empty()) {
        auto awaiter = mAwaiters.front();
        mAwaiters.pop_front();
        awaiter->onNotify();
    }
}

auto WaitQueue::wakeupAll() -> void {
    for (auto it = mAwaiters.begin(); it != mAwaiters.end(); ) {
        auto awaiter = *it;
        it = mAwaiters.erase(it);
        awaiter->onNotify();
    }
}

auto AwaiterBase::await_suspend(runtime::CoroHandle caller) -> void {
    mCaller = caller;
    mIt = mQueue.mAwaiters.emplace(mQueue.mAwaiters.end(), this);
    mReg.register_<&AwaiterBase::onStopRequested>(caller.stopToken(), this);
}

auto AwaiterBase::onNotify() -> void {
    mIt = mQueue.mAwaiters.end();
    mCaller.schedule();
}

auto AwaiterBase::onStopRequested() -> void {
    if (mIt == mQueue.mAwaiters.end()) { //  We already got the lock, ignore it
        return;
    }
    mQueue.mAwaiters.erase(mIt);
    mIt = mQueue.mAwaiters.end();
    mCaller.setStopped();
}

ILIAS_NS_END