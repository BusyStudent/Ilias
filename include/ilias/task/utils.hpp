#pragma once

#include <ilias/runtime/token.hpp>
#include <ilias/runtime/coro.hpp>
#include <ilias/task/when_any.hpp>
#include <ilias/task/task.hpp>
#include <atomic> // std::atomic_flag
#include <chrono> // std::chrono::milliseconds

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

// Add an async cleanup handler to an awaitable
class TaskContextMain : public TaskContext {};
class TaskContextFinally : public TaskContext { 
public: 
    using TaskContext::TaskContext;
};

class FinallyAwaiterBase : protected TaskContextMain, protected TaskContextFinally {
public:
    FinallyAwaiterBase(TaskHandle<> main, TaskHandle<> finally) : TaskContextMain(main), TaskContextFinally(finally, std::nostopstate) {}
    FinallyAwaiterBase(TaskHandle<> main, std::nullptr_t finally) : TaskContextMain(main), TaskContextFinally(finally, std::nostopstate) {} // For FinallyFnAwaiter, lazy initialization
    FinallyAwaiterBase(FinallyAwaiterBase &&) = default;
    ~FinallyAwaiterBase() = default;

    ILIAS_API
    auto await_suspend(runtime::CoroHandle caller) -> std::coroutine_handle<>;
    auto await_ready() -> bool { return false; } // Always suspend, because althrough the task is ready, we need to call the finally handler

    auto setContext(runtime::CoroContext &ctxt) noexcept {
        TaskContextMain::setExecutor(ctxt.executor());
        TaskContextFinally::setExecutor(ctxt.executor());
    }
protected:
    auto mainHandle() -> TaskHandle<> {
        return TaskContextMain::mTask;
    }
    auto finallyHandle() -> TaskHandle<> {
        return TaskContextFinally::mTask;
    }
private:
    static auto onTaskCompletion(runtime::CoroContext &_self) -> void;
    static auto onFinallyCompletion(runtime::CoroContext &_self) -> void;

    runtime::CoroHandle mCaller;
    runtime::StopRegistration mReg;
};

template <typename T>
class FinallyAwaiter final : public FinallyAwaiterBase {
public:
    FinallyAwaiter(TaskHandle<T> main, TaskHandle<> finally) : FinallyAwaiterBase(main, finally) {}

    auto await_resume() -> T {
        return TaskHandle<T>::cast(mainHandle()).value();
    }
};

template <typename T, std::invocable Fn>
class FinallyFnAwaiter final : public FinallyAwaiterBase {
public:
    FinallyFnAwaiter(TaskHandle<T> main, Fn fn) : FinallyAwaiterBase(main, nullptr), mFn(std::move(fn)) {}

    auto await_resume() -> T {
        return TaskHandle<T>::cast(mainHandle()).value();
    }

    auto setContext(runtime::CoroContext &ctxt) { // Lazy initialization until co_await
        TaskContextFinally::mTask = toTask(mFn())._leak();
        FinallyAwaiterBase::setContext(ctxt);
    }
private:
    Fn mFn;
};

} // namespace task

// Dispatch tags
namespace task {
    struct SetTimeoutTags { std::chrono::milliseconds ms; };
    struct ScheduleOnTags { runtime::Executor &exec; };
    struct UnstoppableTags {};
    template <typename T>
    struct FinallyTags { T v; };
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

// Add an async cleanup task to an awaitable
template <Awaitable T, typename U>
[[nodiscard]]
inline auto finally(T awaitable, Task<U> finally) -> task::FinallyAwaiter<AwaitableResult<T> > {
    return {toTask(std::move(awaitable))._leak(), finally._leak()};
}

// Add an async cleanup handler to an awaitable
template <Awaitable T, std::invocable Fn>
[[nodiscard]]
inline auto finally(T awaitable, Fn fn) -> task::FinallyFnAwaiter<AwaitableResult<T>, Fn> {
    return {toTask(std::move(awaitable))._leak(), std::move(fn)};
}

// Tags invoke here
[[nodiscard]]
inline auto setTimeout(std::chrono::milliseconds ms) -> task::SetTimeoutTags {
    return {ms};
}

[[nodiscard]]
inline auto scheduleOn(runtime::Executor &exec) -> task::ScheduleOnTags {
    return {exec};
}

[[nodiscard]]
inline auto unstoppable() -> task::UnstoppableTags {
    return {};
}

template <typename T>
[[nodiscard]]
inline auto finally(T v) -> task::FinallyTags<T> {
    return {std::move(v)};
}

template <Awaitable T>
[[nodiscard]]
inline auto operator |(T awaitable, task::SetTimeoutTags tag) {
    return setTimeout(std::move(awaitable), tag.ms);
}

template <Awaitable T>
[[nodiscard]]
inline auto operator |(T awaitable, task::ScheduleOnTags tag) {
    return scheduleOn(std::move(awaitable), tag.exec);
}

template <Awaitable T>
[[nodiscard]]
inline auto operator |(T awaitable, task::UnstoppableTags) {
    return unstoppable(std::move(awaitable));
}

template <Awaitable T, typename U>
[[nodiscard]]
inline auto operator |(T awaitable, task::FinallyTags<U> tag) {
    return finally(std::move(awaitable), std::move(tag.v));
}

ILIAS_NS_END