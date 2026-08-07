#include <ilias/task/spawn.hpp>
#include <ilias/task/group.hpp>
#include <utility> // std::exchange

ILIAS_NS_BEGIN

using namespace task;

// MARK: TaskGroup
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

auto TaskGroupBase::insert(Rc<TaskSpawnContextBase> task) -> StopHandle {
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

auto TaskGroupBase::onTaskCompleted(TaskSpawnContextBase &ctxt) -> void {
    ILIAS_ASSERT(ctxt.isLinked(), "Should be linked the running list");
    ILIAS_ASSERT(ctxt.isCompleted(), "Should be completed");
    ILIAS_ASSERT(mNumRunning > 0, "Should have at least one running task");

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
    std::vector<TaskSpawnContextBase *> running;
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

auto TaskGroupBase::nextCompletion() noexcept -> Rc<TaskSpawnContextBase> {
    ILIAS_ASSERT(hasCompletion(), "No completion, invalid call?");
    auto &front = mCompleted.front();
    auto ptr = Rc<TaskSpawnContextBase>{&front};
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
    ILIAS_ASSERT(mGroup.mAwaiter == nullptr, "User should not call group.next() | shutdown() | waitAll() concurrently");
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



ILIAS_NS_END