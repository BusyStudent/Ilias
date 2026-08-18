#include <ilias/task/when_all.hpp>
#include <ilias/log.hpp>

ILIAS_NS_BEGIN

using namespace task;

auto WhenAllAwaiterBase::await_ready() -> bool {
    // Start all first
#if defined(ILIAS_CORO_TRACE)
    // TRACING: mark the current await point we are on whenAll
    if (auto frame = mContext.tracing().topFrame(); frame) {
        frame->setMessage("whenAll");
    }
#endif // defined(ILIAS_CORO_TRACE)
    mLeft = mTasks.size();
    for (auto &ctxt: mTasks) {
        ctxt.setUserdata(this);
        ctxt.setExecutor(mContext.executor());
        ctxt.setStoppedHandler(&onTaskCompleted);
        ctxt.task().setCompletionHandler(&onTaskCompleted);

        // TRACING: a subtask is started
        ctxt.tracing().setParent(mContext.tracing());
        ctxt.tracing().spawn(mSource);
        ctxt.task().resume();
    }
    return mLeft == 0;
}

auto WhenAllAwaiterBase::await_suspend(CoroHandle caller) -> void {
    mCaller = caller;
    mReg.register_(caller.stopToken(), [this]() {
        // Forward the stop if needed
        mStopRequested = true;
        for (auto &ctxt : mTasks) {
            ctxt.stop();
        }
    });
}

inline auto WhenAllAwaiterBase::onTaskCompleted(CoroContext &_ctxt) noexcept -> void {
    auto &ctxt = static_cast<TaskContext &>(_ctxt);
    auto &self = *static_cast<WhenAllAwaiterBase *>(ctxt.userdata());
    self.mLeft -= 1;

    // TRACING: a subtask is completed
    ctxt.tracing().complete();
    if (self.mLeft != 0) {
        return; // Still has some imcomplete tasks
    }
    if (self.mStopRequested) { // The stop was requested, and all tasks are completed, we enter the stopped state
        self.mCaller.setStopped();
        return;
    }
    if (self.mCaller) {
        ctxt.task().setPrevAwaiting(self.mCaller); // Use the current completed task to resume the caller
    }
}

ILIAS_NS_END