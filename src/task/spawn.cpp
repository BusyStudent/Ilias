#include <ilias/task/spawn.hpp>
#include <ilias/task/task.hpp>

ILIAS_NS_BEGIN

using namespace task;

// MARK: TaskSpawn
TaskSpawnContextBase::TaskSpawnContextBase(TaskHandle<> task, CaptureSource source) : TaskContext(task) {
    auto executor = runtime::Executor::currentThread();
    ILIAS_ASSERT(executor, "The current thread has no executor");

    // Bind the task to self
    auto handler = [](CoroContext &_self) noexcept -> void{
        auto &self = static_cast<TaskSpawnContextBase &>(_self);
        self.executor().schedule([self = &self]() { self->onComplete(); });
    };
    mTask.setCompletionHandler(handler);
    this->setStoppedHandler(handler);
    this->setExecutor(*executor);

    // TRACING: trace the spawn point
    this->tracing().pushFrame("spawn", source);
    this->tracing().spawn(source);

    this->ref(); // Ref it, we will deref it when it completed
    mTask.schedule(); // Schedule the task in the executor
}

auto TaskSpawnContextBase::onComplete() -> void {
    // Done..
    mCompleted = true;
    // Call the child class to store the result
    this->mManager(*this, Ops::SetValue);
    this->setTask(nullptr); // Drop the task, to make sure when the task is completed, the argument is released

    if (mCompletionHandler) { // Notify we are completed
        mCompletionHandler(*this);
        mCompletionHandler = nullptr;
    }
    // TRACING: trace the completion point
    this->tracing().complete();

    if (this->use_count() == 1) { // We are the last one, only can be deref in the event loop
        executor().schedule([this]() { 
            this->deref();
        });
    }
    else { // For avoid the derefSelf call after quit, we can deref the self
        deref();
    }
}

ILIAS_NS_END