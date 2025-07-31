#pragma once

#include <ilias/runtime/token.hpp>
#include <ilias/runtime/coro.hpp>
#include <ilias/task/when_any.hpp>
#include <ilias/task/task.hpp>
#include <atomic>
#include <chrono>

ILIAS_NS_BEGIN

namespace task {

template <typename T>
class UnstoppableAwaiter {
public:
    UnstoppableAwaiter(TaskHandle<T> handle) : mHandle(handle), mAwaiter(handle) {}
    UnstoppableAwaiter(UnstoppableAwaiter &&) = default;

    auto await_ready() -> bool {
        mHandle.setContext(mCtxt); // Use the unstoppable context
        return mAwaiter.await_ready();
    }

    auto await_suspend(runtime::CoroHandle caller) {
        return mAwaiter.await_suspend(caller);
    }

    auto await_resume() -> T {
        return mAwaiter.await_resume();
    }

    // Set the context of the task, call on await_transform
    auto setContext(runtime::CoroContext &ctxt) {
        // We just need the executor info from the context
        mCtxt.setExecutor(ctxt.executor());
    }
private:
    runtime::CoroContext mCtxt {std::nostopstate};
    TaskHandle<T>  mHandle;
    TaskAwaiter<T> mAwaiter;
};

// Schedule task on another executor
template <typename T>
class ScheduleAwaiter : private TaskContext {
public:
    ScheduleAwaiter(runtime::Executor &exec, TaskHandle<T> handle) : TaskContext(handle), mExecutor(exec), mHandle(handle) {}
    ScheduleAwaiter(ScheduleAwaiter &&) = default;

    auto await_ready() -> bool { return false; }

    auto await_suspend(runtime::CoroHandle caller) {
        mCaller = caller;

        // Start the task on another executor
        this->setExecutor(mExecutor);
        this->setStoppedHandler(ScheduleAwaiter::onCompletion);
        mHandle.setCompletionHandler(ScheduleAwaiter::onCompletion);
        mHandle.setContext(*this);
        mHandle.schedule();
        mReg.register_<&ScheduleAwaiter::onStopRequested>(caller.stopToken(), this);
    }

    auto await_resume() -> T {
        return mHandle.value();
    }
private:
    auto onStopRequested() -> void { // Currently in caller thread
        if (mFlag.test_and_set()) { // Prevent call stop after completion
            return;
        }
        mExecutor.post([](void *self) { // Forward the stop request in executor thread
            static_cast<ScheduleAwaiter *>(self)->stop();
        }, this);
    }

    static auto onCompletion(runtime::CoroContext &_self) -> void { // In the executor thread
        auto &self = static_cast<ScheduleAwaiter &>(_self);
        self.mFlag.test_and_set();
        self.mExecutor.post(invoke, &self);
    }

    static auto invoke(void *_self) -> void { // In the caller thread
        auto &self = *static_cast<ScheduleAwaiter *>(_self);
        if (self.isStopped()) { // Foreard the stop request to the caller
            self.mCaller.setStopped();
        }
        else {
            self.mCaller.resume();
        }
    }

    TaskHandle<T> mHandle;
    std::atomic_flag mFlag;
    runtime::Executor &mExecutor;
    runtime::CoroHandle mCaller;
    runtime::StopRegistration mReg;
};

} // namespace task

// Set an timeout for a task, return nullopt on timeout
template <Awaitable T>
[[nodiscard]]
inline auto setTimeout(T awaitable, std::chrono::milliseconds ms) -> Task<task::Option<AwaitableResult<T> > > {
    auto [res, timeout] = co_await whenAny(std::move(awaitable), sleep(ms));
    if (timeout) {
        co_return std::nullopt;
    }
    co_return std::move(*res);
}

// Make a awaitable execute on another executor
template <Awaitable T>
[[nodiscard]]
inline auto scheduleOn(T awaitable, runtime::Executor &exec) -> task::ScheduleAwaiter<AwaitableResult<T> > {
    return {exec, toTask(std::move(awaitable))._leak()};
}


// Make a awaitable execute on an unstoppable context
template <Awaitable T>
[[nodiscard]]
inline auto unstoppable(T awaitable) -> task::UnstoppableAwaiter<AwaitableResult<T> > {
    return {toTask(std::move(awaitable))._leak()};
}

ILIAS_NS_END