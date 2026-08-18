#include <ilias/task/utils.hpp>
#include <utility> // std::exchange
#include <atomic> // std::atomic_ref

ILIAS_NS_BEGIN

using namespace task;

// MARK: ScheduleAwaiter
auto ScheduleAwaiterBase::await_suspend(CoroHandle caller) -> void { // Currently in caller thread
    ILIAS_TRACE("Task", "Schedule a task on executor {}", static_cast<void*>(&mExecutor));
    mCaller = caller;

    // Start the task on another executor
    this->setExecutor(mExecutor);
    this->setStoppedHandler(ScheduleAwaiterBase::onCompletion);
    mHandle.setCompletionHandler(ScheduleAwaiterBase::onCompletion);
    mHandle.setContext(*this);
    mHandle.schedule();
    mReg.register_<&ScheduleAwaiterBase::onStopRequested>(caller.stopToken(), this);
}

auto ScheduleAwaiterBase::onStopRequested() -> void { // Currently in caller thread
    auto epxected = State::Running;
    if (std::atomic_ref(mState).compare_exchange_strong(epxected, State::StopPending)) { // We can send the stop
        mExecutor.schedule([this]() { onStopInvoke(); });
    }
}

auto ScheduleAwaiterBase::onStopInvoke() -> void { // Currently in executor thread
    auto expected = State::StopPending;
    if (std::atomic_ref(mState).compare_exchange_strong(expected, State::StopHandled)) {
        this->stop(); // Forward the stop request to the task
        return;
    }
    if (expected == State::Completed) { // Will, the task has been completed, we take the responsibility to resume the caller
        mExecutor.schedule([this]() { invoke(); });
    }
}

auto ScheduleAwaiterBase::onCompletion(runtime::CoroContext &_self) noexcept -> void  { // In the executor thread
    auto &self = static_cast<ScheduleAwaiterBase &>(_self);
    auto old = std::atomic_ref(self.mState).exchange(State::Completed);
    if (old == State::StopPending) { // Stop is pending, let the onStopInvoke handle this
        return;
    }
    auto invokeLater = [&self]() {
        self.mCaller.executor().schedule([&self]() { self.invoke(); });
    };
    self.mExecutor.schedule(invokeLater); // Currently, we are on the Coroutine::final_suspend, it is not safe to resume the caller directly
}

auto ScheduleAwaiterBase::invoke() -> void  { // In the caller thread
    ILIAS_TRACE("Task", "Task on executor {} completed", static_cast<void*>(&mExecutor));
    ILIAS_ASSERT(std::atomic_ref(mState).load() == State::Completed);
    if (this->isStopped()) { // Foreard the stop request to the caller
        mCaller.setStopped();
    }
    else {
        mCaller.resume();
    }
}

// MARK: FinallyAwaiter
auto FinallyAwaiterBase::await_suspend(CoroHandle caller) -> std::coroutine_handle<> {
    auto mainHandle = mContext->task();

    // The callbacks
    auto mainCallback = [](runtime::CoroContext &ctxt) noexcept {
        return static_cast<FinallyAwaiterBase*>(ctxt.userdata())->onTaskCompletion();
    };
    mCaller = caller;
    mReg.register_<&TaskContext::stop>(caller.stopToken(), &*mContext); // Forward the stop to the handle task

    // Bind the ctxt to self first
    mContext->setUserdata(this);
    mContext->setStoppedHandler(mainCallback);
    mainHandle.setContext(*mContext);
    mainHandle.setCompletionHandler(mainCallback);
    mContext->tracing().spawn(mSource);
    return mainHandle.toStd(); // Switch into it, caller -> task -> finally -> (caller or caller.setStopped())
}

auto FinallyAwaiterBase::onTaskCompletion() -> void {
    mContext->executor().schedule([this]() {
        auto finallyCallback = [](runtime::CoroContext &ctxt) noexcept {
            return static_cast<FinallyAwaiterBase*>(ctxt.userdata())->onFinallyCompletion();
        };

        // Store the context info
        auto &executor = mContext->executor();
#if defined(ILIAS_CORO_TRACE)
        auto parent = mContext->tracing().parent();
#endif // ILIAS_CORO_TRACE
        mStopped = mContext->isStopped();
        mReg.reset();

        // Call the child to store the result
        mContext->tracing().complete();
        auto handle = mOnTaskCompletion(*this);
        mContext.reset(); // Destroy the task， quit the scope
        
        // Invoke the finally
        mContext.emplace(handle, std::nostopstate);
        mContext->setUserdata(this);
        mContext->setExecutor(executor);
        handle.setContext(*mContext);
        handle.setCompletionHandler(finallyCallback);

#if defined(ILIAS_CORO_TRACE)
        mContext->tracing().setParent(*parent);
        mContext->tracing().spawn(mSource);
#endif // ILIAS_CORO_TRACE
        handle.resume();
    });
}

auto FinallyAwaiterBase::onFinallyCompletion() -> void {
    mContext->tracing().complete();
    if (mStopped) { // Forward the stop completion to the caller
        mCaller.setStopped();
    }
    else { // Switch into caller
        mContext->task().setPrevAwaiting(mCaller);
    }
}

// MARK: StopTokenAwaiter
auto StopTokenAwaiter::await_suspend(CoroHandle caller) -> void {
    mCaller = caller;
    mReg.register_<&StopTokenAwaiter::onStopRequested>(mToken, this);
    mRuntimeReg.register_<&StopTokenAwaiter::onRuntimeStopRequested>(caller.stopToken(), this);
}

auto StopTokenAwaiter::onStopRequested() -> void {
    auto prev = std::atomic_ref {mCompleted}.exchange(true);
    if (!prev) {
        mCaller.schedule();
    }
}

auto StopTokenAwaiter::onRuntimeStopRequested() -> void {
    auto prev = std::atomic_ref {mCompleted}.exchange(true);
    if (!prev) {
        mCaller.setStopped();
    }
}

ILIAS_NS_END