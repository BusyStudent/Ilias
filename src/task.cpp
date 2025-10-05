#include <ilias/task.hpp>
#include <utility> // std::exchange
#include <atomic> // std::atomic_ref

ILIAS_NS_BEGIN

using namespace task;

#pragma region TaskGroup
TaskGroupBase::TaskGroupBase() {
    
}

TaskGroupBase::TaskGroupBase(TaskGroupBase &&other) noexcept : 
    mRunning(std::move(other.mRunning)),
    mCompleted(std::move(other.mCompleted)),
    mStopRequested(std::exchange(other.mStopRequested, false)),
    mNumRunning(std::exchange(other.mNumRunning, 0)),
    mNumCompleted(std::exchange(other.mNumCompleted, 0)),
    mAwaiter(std::exchange(other.mAwaiter, nullptr))
{
    // Rebind the completion handlers
    for (auto &task : mRunning) {
        task.setCompletionHandler<&TaskGroupBase::onTaskCompleted>(this);
    }
}

TaskGroupBase::~TaskGroupBase() {
    // Unbind the completion handlers detach the tasks and send stop signal
    for (auto iter = mRunning.begin(); iter != mRunning.end();) {
        auto &task = *iter;
        task.deref(); // Decrease the ref, the group will not own the task anymore, in pair on insert() method
        task.setCompletionHandler(nullptr);
        task.stop();
        iter = mRunning.erase(iter);
    }

    // Release all the tasks in the completed list
    while (hasCompletion()) {
        auto _ = nextCompletion();
    }
    ILIAS_ASSERT(mNumCompleted == 0);
}

auto TaskGroupBase::size() const noexcept -> size_t {
    return mNumRunning + mNumCompleted;
}

auto TaskGroupBase::insert(Rc<TaskSpawnContext> task) -> StopHandle {
    ILIAS_ASSERT(task != nullptr);
    task->ref(); // Increase the ref, the group will share the ownership of the task
    if (mStopRequested) {
        task->stop();
    }
    if (task->isCompleted()) { // Already completed
        mCompleted.push_back(*task);
        mNumCompleted += 1;
        notifyCompletion();
    }
    else { // Still Running, add it to the running lust and bind the completion handler
        task->setCompletionHandler<&TaskGroupBase::onTaskCompleted>(this);
        mNumRunning += 1;
        mRunning.push_back(*task);
    }
    return StopHandle(std::move(task));
}

auto TaskGroupBase::onTaskCompleted(TaskSpawnContext &ctxt) -> void {
    ILIAS_ASSERT(ctxt.isLinked()); // Should be linked the running list
    ILIAS_ASSERT(ctxt.isCompleted()); // Should be completed
    ILIAS_ASSERT(mNumRunning > 0); // Should have at least one running task

    // Remove the task from the running list
    ctxt.unlink();
    mNumRunning -= 1;

    // Add to the completed list
    mNumCompleted += 1;
    mCompleted.push_back(ctxt);

    // In debug check the size, the intrusive list.size() is O(n)
#if !defined(NDEBUG)
    ILIAS_ASSERT(mNumRunning == mRunning.size());
    ILIAS_ASSERT(mNumCompleted == mCompleted.size());
#endif // defined(NDEBUG)

    notifyCompletion();
}

auto TaskGroupBase::stop() -> void {
    if (mStopRequested) { // Already notified
        return;
    }
    mStopRequested = true;

    // The stop may immediately stop the task, and then onTaskCompleted was called, the mRunning will be changed in iteration, so we need to copy it
    // TODO: Think a better way?
    std::vector<TaskSpawnContext *> running;
    running.reserve(mNumRunning);
    for (auto &task : mRunning) {
        running.emplace_back(&task);
    }
    ILIAS_ASSERT(running.size() == mNumRunning);
    for (auto &task : running) {
        task->stop();
    }
}

auto TaskGroupBase::hasCompletion() const noexcept -> bool {
    return !mCompleted.empty();
}

auto TaskGroupBase::nextCompletion() noexcept -> Rc<TaskSpawnContext> {
    ILIAS_ASSERT_MSG(hasCompletion(), "No completion, invalid call?");
    auto &front = mCompleted.front();
    auto ptr = Rc<TaskSpawnContext>{&front};
    mCompleted.pop_front();
    mNumCompleted -= 1;
    ptr->deref(); // We remove the task out of the group, so we need to decrease the ref
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
        if (mGroup.mNumRunning == 0) {
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
template class TaskGroup<void>;

#pragma region TaskScope
TaskScope::TaskScope() = default;
TaskScope::~TaskScope() {
    ILIAS_ASSERT(mRunning.empty());
    ILIAS_ASSERT(mNumRunning == 0);
    if (!mRunning.empty()) {
        ::fprintf(stderr, "Error: TaskScope destructed with %zu running tasks\n call waitAll() before destroy", mNumRunning);
        ::abort();
    }
}

auto TaskScope::cleanup(std::optional<runtime::StopToken> token) -> Task<void> {
    // Forward the stop to the children
    if (!token) { // If stop token is not provided, get from the current context
        token = co_await this_coro::stopToken();
    }
    auto proxy = [this]() { stop(); };
    auto cb1 = runtime::StopCallback(*token, proxy);

    struct Awaiter {
        TaskScope &self;

        auto await_ready() const noexcept { // All completed
            return self.mNumRunning == 0;
        }

        auto await_suspend(runtime::CoroHandle caller) const noexcept {
            self.mWaiter = caller;
        }

        auto await_resume() const noexcept {}
    };
    co_return co_await Awaiter { *this };
}

auto TaskScope::insertImpl(Rc<TaskSpawnContext> task) -> StopHandle {
    ILIAS_ASSERT(task != nullptr);
    if (!task->isCompleted()) { // Adding to running list
        task->ref();
        task->setCompletionHandler<&TaskScope::onTaskCompleted>(this);
        mNumRunning += 1;
        mRunning.push_back(*task);

        // Check if we need to stop it
        if (mStopRequested) {
            task->stop();
        }
    }
    return StopHandle(std::move(task));
}

auto TaskScope::onTaskCompleted(TaskSpawnContext &ctxt) -> void {
    ILIAS_ASSERT(ctxt.isLinked()); // As same as TaskGroup
    ILIAS_ASSERT(ctxt.isCompleted());
    ILIAS_ASSERT(mNumRunning > 0);

    // Remove from the running list
    mNumRunning -= 1;

    // Because the race condition, we may cleanup up in the eventloop
    auto cleanup = [&ctxt]() {
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
    if (!mWaiter) {
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

auto TaskScope::stop() noexcept -> void {
    if (mStopRequested) {
        return;
    }
    mStopRequested = true;
    mStopping = true;
    for (auto &task : mRunning) {
        task.stop();
    }
    mStopping = false;
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