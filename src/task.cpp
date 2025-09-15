#include <ilias/task.hpp>
#include <atomic> // std::atomic_ref

ILIAS_NS_BEGIN

using namespace task;

#pragma region TaskGroup
TaskGroupBase::TaskGroupBase() {
    
}

TaskGroupBase::TaskGroupBase(TaskGroupBase &&other) noexcept : 
    mPending(std::move(other.mPending)),
    mCompleted(std::move(other.mCompleted)),
    mStopRequested(other.mStopRequested),
    mAwaiter(other.mAwaiter)
{
    // Rebind the completion handlers
    for (auto &task : mPending) {
        task->setCompletionHandler<&TaskGroupBase::onTaskCompleted>(this);
    }
}

TaskGroupBase::~TaskGroupBase() {
    // Unbind the completion handlers detach the tasks and send stop signal
    for (auto &task : mPending) { 
        task->setCompletionHandler(nullptr);
        task->stop();
    }
}

auto TaskGroupBase::size() const noexcept -> size_t {
    return mPending.size() + mCompleted.size();
}

auto TaskGroupBase::insert(std::shared_ptr<TaskSpawnContext> task) -> StopHandle {
    ILIAS_ASSERT(task != nullptr);
    if (mStopRequested) {
        task->stop();
    }
    if (task->isCompleted()) { // Already completed
        mCompleted.emplace_back(task);
        notifyCompletion();
    }
    else { // Still Running
        task->setCompletionHandler<&TaskGroupBase::onTaskCompleted>(this);
        auto [_, emplace] = mPending.emplace(task);
        ILIAS_ASSERT(emplace);
    }
    return StopHandle(std::move(task));
}

auto TaskGroupBase::onTaskCompleted(TaskSpawnContext &ctxt) -> void {
    auto iter = mPending.find(&ctxt);
    if (iter == mPending.end()) {
        ::abort();
    }
    // Move to the completed set
    auto ptr = *iter;
    mPending.erase(iter);
    mCompleted.emplace_back(std::move(ptr));
    notifyCompletion();
}

auto TaskGroupBase::stop() -> void {
    if (mStopRequested) { // Already notified
        return;
    }
    mStopRequested = true;

    // The stop may immediately stop the task, and then onTaskCompleted was called, the mPending will be changed in iteration, so we need to copy it
    // TODO: Think a better way?
    std::vector<TaskSpawnContext *> pending;
    pending.reserve(mPending.size());
    for (auto &task : mPending) {
        pending.emplace_back(task.get());
    }
    for (auto &task : pending) {
        task->stop();
    }
}

auto TaskGroupBase::hasCompletion() const noexcept -> bool {
    return !mCompleted.empty();
}

auto TaskGroupBase::nextCompletion() noexcept -> std::shared_ptr<TaskSpawnContext> {
    ILIAS_ASSERT_MSG(hasCompletion(), "No completion, invalid call?");
    auto ptr = std::move(mCompleted.front());
    mCompleted.pop_front();
    return ptr;
}

auto TaskGroupBase::notifyCompletion() -> void {
    auto awaiter = std::exchange(mAwaiter, nullptr);
    if (awaiter) {
        awaiter->onCompletion();
    }
}


// Awiater internal part
auto TaskGroupAwaiterBase::await_suspend(CoroHandle caller) -> void {
    ILIAS_ASSERT_MSG(mGroup.mAwaiter == nullptr, "User should not call group.next() | shutdown() | waitAll() concurrently");
    mCaller = caller;
    mGroup.mAwaiter = this;
    mReg.register_<&TaskGroupAwaiterBase::onStopRequested>(caller.stopToken(), this);
}

auto TaskGroupAwaiterBase::onCompletion() -> void {
    if (mStopRequested) {
        auto _ = mGroup.nextCompletion(); // Drop the completion
        // Check all the task has been completed
        if (mGroup.mPending.size() == 0) {
            mCaller.setStopped();
            return;
        }
        // Continue to wait for the completion
        mGroup.mAwaiter = this;
        return;
    }
    mGot = true;
    mCaller.schedule();
}

auto TaskGroupAwaiterBase::onStopRequested() -> void {
    if (mGot) {
        return;
    }
    mStopRequested = true;
    mGroup.stop();
}

// Make compile faster?
template class ILIAS_API TaskGroup<void>;

#pragma region TaskScope
TaskScope::TaskScope() = default;
TaskScope::~TaskScope() {
    ILIAS_ASSERT(mGroup.empty());
}

auto TaskScope::waitAll(runtime::StopToken token) -> Task<void> {
    auto callback = runtime::StopCallback(token, [this]() {
        mGroup.stop(); // Forward the stop
    });
    while (!mGroup.empty()) {
        auto _ = co_await mGroup.next();
    }
}

#pragma region ScheduleAwaiter
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

auto ScheduleAwaiterBase::onCompletion(runtime::CoroContext &_self) -> void  { // In the executor thread
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

#pragma region FinallyAwaiter
auto FinallyAwaiterBase::await_suspend(CoroHandle caller) -> std::coroutine_handle<> {
    auto mainHandle = mMainCtxt.task();
    auto finallyHandle = mFinallyCtxt.task();

    // Bind the ctxt to self first
    mMainCtxt.setUserdata(this);
    mFinallyCtxt.setUserdata(this);

    // The callbacks
    auto mainCallback = [](runtime::CoroContext &ctxt) {
        return static_cast<FinallyAwaiterBase*>(ctxt.userdata())->onTaskCompletion();
    };
    auto finallyCallback = [](runtime::CoroContext &ctxt) {
        return static_cast<FinallyAwaiterBase*>(ctxt.userdata())->onFinallyCompletion();
    };

    mCaller = caller;
    mReg.register_<&TaskContext::stop>(caller.stopToken(), &mMainCtxt); // Forward the stop to the handle task
    mainHandle.setContext(mMainCtxt);
    mainHandle.setCompletionHandler(mainCallback);
    mMainCtxt.setStoppedHandler(mainCallback);

    finallyHandle.setContext(mFinallyCtxt);
    finallyHandle.setCompletionHandler(finallyCallback);
    return mainHandle.toStd(); // Switch into it, caller -> task -> finally -> (caller or caller.setStopped())
}

auto FinallyAwaiterBase::onTaskCompletion() -> void {
    if (mMainCtxt.isStopped()) { // The main task is stopped, we should call the finally on event loop
        mFinallyCtxt.task().schedule();
    }
    else { // Otherwise, let the min call the finally. short-cut :)
        mMainCtxt.task().setPrevAwaiting(mFinallyCtxt.task());
    }
}

auto FinallyAwaiterBase::onFinallyCompletion() -> void {
    if (mMainCtxt.isStopped()) { // Forward the stop completion to the caller
        mCaller.setStopped();
    }
    else { // Switch into caller
        mFinallyCtxt.task().setPrevAwaiting(mCaller);
    }
}

ILIAS_NS_END