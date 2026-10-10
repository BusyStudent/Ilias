#include <ilias/task/spawn.hpp>
#include <ilias/task/group.hpp>
#include <utility> // std::exchange

ILIAS_NS_BEGIN

using namespace task;

// MARK: TaskGroup
TaskGroupBase::TaskGroupBase() noexcept = default;

TaskGroupBase::TaskGroupBase(TaskGroupBase &&other) noexcept : 
    mRunning(std::move(other.mRunning)),
    mCompleted(std::move(other.mCompleted)),
    mStopRequested(std::exchange(other.mStopRequested, false)),
    mNumRunning(std::exchange(other.mNumRunning, 0)),
    mNumCompleted(std::exchange(other.mNumCompleted, 0)),
    mNotify(std::exchange(other.mNotify, nullptr))
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
    while (completionSize() != 0) {
        auto _ = nextCompletion();
    }
    ILIAS_ASSERT(mNumCompleted == 0);
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

inline
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

auto TaskGroupBase::nextCompletion() noexcept -> Rc<TaskSpawnContextBase> {
    ILIAS_ASSERT(completionSize() != 0, "No completion, invalid call?");
    auto &front = mCompleted.front();
    auto ptr = Rc<TaskSpawnContextBase>{&front};
    mCompleted.pop_front();
    mNumCompleted -= 1;
    ptr->deref(); // We remove the task out of the group, so we need to decrease the ref
    return ptr;
}

inline 
auto TaskGroupBase::notifyCompletion() -> void {
    auto notify = std::exchange(mNotify, nullptr);
    if (notify) {
        notify();
    }
}


// Awaiter internal part
auto TaskGroupWaitNextBase::await_suspend(CoroHandle caller) noexcept -> void {
    ILIAS_ASSERT(mGroup.mNotify == nullptr, "User should not call group.next() | shutdown() | waitAll() concurrently");
    mCaller = caller;
    
    // onCompletion
    mGroup.mNotify = [this]() noexcept {
        if (mGot) {
            return;
        }
        // Completion Win
        mGot = true;
        mCaller.resume();
    };

    // StopRequested
    mReg.register_(caller.stopToken(), [this]() noexcept {
        if (mGot) {
            return;
        }
        // Stop Win
        mGroup.mNotify = nullptr; // Unregister the the notify
        mGot = true;
        mCaller.setStopped();
    });
}

auto TaskGroupWaitAllBase::await_suspend(CoroHandle caller) noexcept -> void {
    ILIAS_ASSERT(mGroup.mNotify == nullptr, "User should not call group.next() | shutdown() | waitAll() concurrently");
    mCaller = caller;

    // onCompletion
    mGroup.mNotify = [this]() noexcept { onCompletion(); };

    // StopRequested
    mReg.register_(caller.stopToken(), [this]() noexcept {
        mShutdown = true;
        mGroup.stop(); // forward the stop to the group
    });
}

auto TaskGroupWaitAllBase::onCompletion() noexcept -> void {
    // Wait all completion
    if (mShutdown) { // Shutdown
        auto _ = mGroup.nextCompletion();
    }
    if (mGroup.mNumRunning != 0) { // Still running
        mGroup.mNotify = [this]() { onCompletion(); }; // Continue wait
        return;
    }

    if (mCaller.isStopRequested()) { // Enter the stop state, we need wait all task completed and then stop
        mCaller.setStopped();
        return;
    }
    mCaller.resume();
}

ILIAS_NS_END