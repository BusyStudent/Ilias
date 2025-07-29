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


ILIAS_NS_END