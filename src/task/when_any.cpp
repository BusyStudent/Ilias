#include <ilias/task/when_any.hpp>
#include <ilias/log.hpp>

ILIAS_NS_BEGIN

using namespace task;

auto WhenAnyAwaiterBase::await_ready() -> bool {
    // Start all first
#if defined(ILIAS_CORO_TRACE)
    // TRACING: mark the current await point we are on whenAny
    if (auto frame = mContext.tracing().topFrame(); frame) {
        frame->setMessage("whenAny");
    }
#endif // defined(ILIAS_CORO_TRACE)
    mLeft = 0;
    for (auto &ctxt: mTasks) {
        ctxt.setUserdata(this);
        ctxt.setExecutor(mContext.executor());
        ctxt.setStoppedHandler(&onTaskCompleted);
        ctxt.task().setCompletionHandler(&onTaskCompleted);

        // TRACING: a subtask is started
        ctxt.tracing().setParent(mContext.tracing());
        ctxt.tracing().spawn(mSource);
        mLeft += 1;
        mStarted += 1;
        ctxt.task().resume();

        if (mGot) {
            break;
        }
    }
    return mGot && mLeft == 0; // All completed and one of them is got
}

auto WhenAnyAwaiterBase::await_suspend(CoroHandle caller) -> void {
    mSuspended = true;
    mCaller = caller;
    mReg.register_(caller.stopToken(), [this]() {
        // Forward the stop if needed
        mStopRequested = true;
        stopAll();
    });
}

inline auto WhenAnyAwaiterBase::stopAll() -> void {
    for (size_t idx = 0; idx < mStarted; ++idx) { // Stop the started tasks
        mTasks[idx].stop();
    }
}

inline auto WhenAnyAwaiterBase::onTaskCompleted(CoroContext &_ctxt) noexcept -> void {
    auto &ctxt = static_cast<TaskContext &>(_ctxt);
    auto &self = *static_cast<WhenAnyAwaiterBase *>(ctxt.userdata());

    // TRACING: a subtask is completed
    ctxt.tracing().complete();
    if (!ctxt.isStopped()) { // Only not stopped task can be got (value produced)
        if (self.mGot == nullptr) {
            self.mGot = &ctxt; // The first completed task
            self.stopAll(); // Stop all other tasks
        }
    }

    self.mLeft -= 1;
    if (!self.mSuspended) { // Still in await_ready
        return;
    }
    if (self.mLeft != 0) {
        return; // Still has some imcomplete tasks
    }
    if (self.mStopRequested && !self.mGot) { // The stop was requested, and all tasks are completed, no value produced, we enter the stopped state
        self.mCaller.setStopped();
        return;
    }
    if (self.mCaller) {
        self.mCaller.schedule();
        self.mCaller = nullptr;
    }
}

ILIAS_NS_END