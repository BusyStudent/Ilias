#include <ilias/task/spawn.hpp>
#include <ilias/task/scope.hpp>
#include <utility> // std::exchange

ILIAS_NS_BEGIN

using namespace task;

// MARK: TaskScope
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

auto TaskScope::insertImpl(Rc<TaskSpawnContextBase> task) -> StopHandle {
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

auto TaskScope::onTaskCompleted(TaskSpawnContextBase &ctxt) -> void {
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

ILIAS_NS_END