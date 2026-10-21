#include <ilias/task/spawn.hpp>
#include <ilias/task/scope.hpp>
#include <utility> // std::exchange

ILIAS_NS_BEGIN

using namespace task;

// MARK: TaskScope
TaskScopeBase::TaskScopeBase() noexcept = default;
TaskScopeBase::~TaskScopeBase() {
    ILIAS_ASSERT(mRunning.empty());
    ILIAS_ASSERT(mNumRunning == 0);
    if (!mRunning.empty()) {
        ::fprintf(stderr, "Error: TaskScope destructed with %zu running tasks\n call waitAll() before destroy\n", mNumRunning); // NOLINT
        ::abort();
    }
}

auto TaskScopeBase::insert(Rc<TaskSpawnContextBase> handle) -> StopHandle {
    ILIAS_ASSERT(handle != nullptr);
    if (!handle->isCompleted()) { // Adding to running list
        handle->ref();
        handle->setCompletionHandler<&TaskScopeBase::onTaskCompleted>(this);
        mNumRunning += 1;
        mRunning.push_back(*handle);

        // Check if we need to stop it
        if (mStopRequested) {
            handle->stop();
        }
    }
    return StopHandle{std::move(handle)};
}

auto TaskScopeBase::stop() noexcept -> bool {
    if (mStopRequested) {
        return false;
    }
    mStopRequested = true;
    mStopping = true;
    for (auto &task : mRunning) {
        task.stop();
    }
    mStopping = false;
    return true;
}

inline
auto TaskScopeBase::onTaskCompleted(TaskSpawnContextBase &ctxt) noexcept -> void {
    ILIAS_ASSERT(ctxt.isLinked()); // As same as TaskGroup
    ILIAS_ASSERT(ctxt.isCompleted());
    ILIAS_ASSERT(mNumRunning > 0);

    // Remove from the running list
    mNumRunning -= 1;

    // Because the race condition, we may cleanup up in the eventloop
    auto cleanup = [&ctxt]() noexcept {
        ctxt.unlink();
        ctxt.deref();
    };
    if (mStopping) {
        ctxt.executor().schedule(cleanup);
    }
    else {
        cleanup();
    }

    // Do the wakeup if
    if (mNumRunning != 0) {
        return;
    }
    
    // Has waiter
    auto waiter = std::exchange(mWaiter, nullptr); // Prevent double wakeup
    if (waiter.isStopRequested()) { // If the waiter is requested to stop, just set it to stopped
        waiter.setStopped();
    }
    else {
        waiter.schedule();        
    }
}

auto TaskScopeWaitAll::await_suspend(CoroHandle caller) -> void {
    ILIAS_ASSERT(mScope.mWaiter == nullptr, "The waitAll or shutdown can't be called cocurrently");
    mScope.mWaiter = caller;
    mReg.register_(caller.stopToken(), [&]() noexcept{ mScope.stop(); }); // Forward the stop to the scope
}

ILIAS_NS_END