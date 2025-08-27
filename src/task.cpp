#include <ilias/task.hpp>

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
        task->setCompletionHandler(onTaskCompleted, this);
    }
}

TaskGroupBase::~TaskGroupBase() {
    // Unbind the completion handlers detach the tasks and send stop signal
    for (auto &task : mPending) { 
        task->setCompletionHandler(nullptr, nullptr);
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
        task->setCompletionHandler(onTaskCompleted, this);
        auto [_, emplace] = mPending.emplace(task);
        ILIAS_ASSERT(emplace);
    }
    return StopHandle(std::move(task));
}

auto TaskGroupBase::onTaskCompleted(TaskSpawnContext &ctxt, void *_self) -> void {
    auto &self = *static_cast<TaskGroupBase *>(_self);
    auto iter = self.mPending.find(&ctxt);
    if (iter == self.mPending.end()) {
        ::abort();
    }
    // Move to the completed set
    auto ptr = *iter;
    self.mPending.erase(iter);
    self.mCompleted.emplace_back(std::move(ptr));
    self.notifyCompletion();
}

auto TaskGroupBase::stop() -> void {
    if (mStopRequested) { // Already notified
        return;
    }
    mStopRequested = true;
    for (auto &task : mPending) {
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
    auto ptr = std::exchange(mAwaiter, nullptr);
    auto awaiter = static_cast<TaskGroupAwaiterBase *>(ptr);
    if (awaiter) {
        awaiter->onCompletion();
    }
}


// Awiater internal part
auto TaskGroupAwaiterBase::await_suspend(CoroHandle caller) -> void {
    ILIAS_ASSERT_MSG(mGroup.mAwaiter == nullptr, "User should not call group.next() | shutdown() | waitAll() concurrently");
    mCaller = caller;
    mReg.register_<&TaskGroupAwaiterBase::onStopRequested>(caller.stopToken(), this);
    mGroup.mAwaiter = this;
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
    if (mState.compare_exchange_strong(epxected, State::StopPending)) { // We can send the stop
        mExecutor.schedule([this]() { onStopInvoke(); });
    }
}

auto ScheduleAwaiterBase::onStopInvoke() -> void { // Currently in executor thread
    auto expected = State::StopPending;
    if (mState.compare_exchange_strong(expected, State::StopHandled)) {
        this->stop(); // Forward the stop request to the task
        return;
    }
    if (expected == State::Completed) { // Will, the task has been completed, we take the responsibility to resume the caller
        mExecutor.schedule([this]() { invoke(); });
    }
}

auto ScheduleAwaiterBase::onCompletion(runtime::CoroContext &_self) -> void  { // In the executor thread
    auto &self = static_cast<ScheduleAwaiterBase &>(_self);
    auto old = self.mState.exchange(State::Completed);
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
    ILIAS_ASSERT(mState == State::Completed);
    if (this->isStopped()) { // Foreard the stop request to the caller
        mCaller.setStopped();
    }
    else {
        mCaller.resume();
    }
}

#pragma region FinallyAwaiter
auto FinallyAwaiterBase::await_suspend(CoroHandle caller) -> std::coroutine_handle<> {
    auto mainContext = static_cast<TaskContextMain*>(this);
    auto mainHandle = this->mainHandle();
    auto finallyContext = static_cast<TaskContextFinally*>(this);
    auto finallyHandle = this->finallyHandle();

    mCaller = caller;
    mReg.register_<&TaskContext::stop>(caller.stopToken(), mainContext); // Forward the stop to the handle task
    mainHandle.setContext(*mainContext);
    mainHandle.setCompletionHandler(FinallyAwaiterBase::onTaskCompletion);
    mainContext->setStoppedHandler(FinallyAwaiterBase::onTaskCompletion);

    finallyHandle.setContext(*finallyContext);
    finallyHandle.setCompletionHandler(FinallyAwaiterBase::onFinallyCompletion);
    return mainHandle.toStd(); // Switch into it, caller -> task -> finally -> (caller or caller.setStopped())
}

auto FinallyAwaiterBase::onTaskCompletion(runtime::CoroContext &_self) -> void {
    auto &self = static_cast<FinallyAwaiterBase &>(static_cast<TaskContextMain &>(_self)); // Switch into finally
    if (self.TaskContextMain::isStopped()) {
        self.finallyHandle().schedule();
    }
    else {
        self.mainHandle().setPrevAwaiting(self.finallyHandle());
    }
}

auto FinallyAwaiterBase::onFinallyCompletion(runtime::CoroContext &_self) -> void {
    auto &self = static_cast<FinallyAwaiterBase &>(static_cast<TaskContextFinally &>(_self)); // Switch into caller
    if (self.TaskContextMain::isStopped()) {
        self.mCaller.setStopped();
    }
    else {
        self.finallyHandle().setPrevAwaiting(self.mCaller);
    }
}

ILIAS_NS_END