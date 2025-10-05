#pragma once

#include <ilias/runtime/token.hpp>
#include <ilias/runtime/coro.hpp>
#include <ilias/task/when_any.hpp>
#include <ilias/task/task.hpp>
#include <chrono> // std::chrono::milliseconds

ILIAS_NS_BEGIN

namespace task {

template <typename T>
class UnstoppableAwaiter final {
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

// Add an sync map handler to awaitable
template <typename T, typename Fn>
class MapAwaiter final {
public:
    MapAwaiter(TaskHandle<T> handle, Fn fn) : mHandle(handle), mAwaiter(handle), mFn(std::move(fn)) {}
    MapAwaiter(MapAwaiter &&) = default;

    auto await_ready() -> bool {
        return mAwaiter.await_ready();
    }

    auto await_suspend(runtime::CoroHandle caller) {
        return mAwaiter.await_suspend(caller);
    }

    auto await_resume() {
        if constexpr(std::is_void_v<T>) {
            mAwaiter.await_resume();
            return mFn();
        }
        else {
            return mFn(mAwaiter.await_resume());
        }
    }

    // Set the context of the task, call on await_transform
    auto setContext(runtime::CoroContext &ctxt) {
        mHandle.setContext(ctxt);
    }
private:
    TaskHandle<T>  mHandle;
    TaskAwaiter<T> mAwaiter;
    Fn             mFn;
};

// Schedule task on another executor
class ScheduleAwaiterBase : private TaskContext {
public:
    ScheduleAwaiterBase(runtime::Executor &exec, TaskHandle<> handle) : TaskContext(handle), mExecutor(exec), mHandle(handle) {}
    ScheduleAwaiterBase(ScheduleAwaiterBase &&) = default;

    auto await_ready() -> bool { return false; }

    ILIAS_API
    auto await_suspend(runtime::CoroHandle caller) -> void;
protected:
    enum State : uint8_t {
        Running     = 0,
        StopPending = 1,
        StopHandled = 2,
        Completed   = 3,
    };

    auto onStopRequested() -> void;
    auto onStopInvoke() -> void;
    auto invoke() -> void;
    static auto onCompletion(runtime::CoroContext &_self) -> void;

    State mState {Running}; // We use std::atomic_ref internal, make the compiler happy:(, std::atomic<T> can't move
    TaskHandle<> mHandle;
    runtime::Executor &mExecutor;
    runtime::CoroHandle mCaller;
    runtime::StopRegistration mReg;
};

template <typename T>
class ScheduleAwaiter final : public ScheduleAwaiterBase {
public:
    ScheduleAwaiter(runtime::Executor &exec, TaskHandle<T> handle) : ScheduleAwaiterBase(exec, handle) {}

    auto await_resume() -> T {
        return TaskHandle<T>::cast(mHandle).value();
    }
};

// Add an async cleanup handler to an awaitable
class FinallyAwaiterBase {
public:
    FinallyAwaiterBase(TaskHandle<> main, TaskHandle<> finally) : mMainCtxt(main), mFinallyCtxt(finally, std::nostopstate) {}
    FinallyAwaiterBase(FinallyAwaiterBase &&) = default;
    ~FinallyAwaiterBase() = default;

    ILIAS_API
    auto await_suspend(runtime::CoroHandle caller) -> std::coroutine_handle<>;
    auto await_ready() -> bool { return false; } // Always suspend, because althrough the task is ready, we need to call the finally handler

    auto setContext(runtime::CoroContext &ctxt) noexcept {
        mMainCtxt.setExecutor(ctxt.executor());
        mFinallyCtxt.setExecutor(ctxt.executor());
    }
protected:
    TaskContext mMainCtxt;
    TaskContext mFinallyCtxt;
private:
    auto onTaskCompletion() -> void;
    auto onFinallyCompletion() -> void;

    runtime::CoroHandle mCaller;
    runtime::StopRegistration mReg;
};

template <typename T>
class FinallyAwaiter final : public FinallyAwaiterBase {
public:
    FinallyAwaiter(TaskHandle<T> main, TaskHandle<> finally) : FinallyAwaiterBase(main, finally) {}

    auto await_resume() -> T {
        return TaskHandle<T>::cast(mMainCtxt.task()).value();
    }
};

template <typename T, std::invocable Fn>
class FinallyFnAwaiter final : public FinallyAwaiterBase {
public:
    FinallyFnAwaiter(TaskHandle<T> main, Fn fn) : FinallyAwaiterBase(main, nullptr), mFn(std::move(fn)) {}

    auto await_resume() -> T {
        return TaskHandle<T>::cast(mMainCtxt.task()).value();
    }

    auto setContext(runtime::CoroContext &ctxt) { // Lazy initialization until co_await
        mFinallyCtxt.setTask(toTask(mFn())._leak());
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
    struct MapTags { T v; };
    template <typename T>
    struct FinallyTags { T v; };
} // namespace task

// Special types for just spawn a task and forget about it, useful in callback or Qt slots
class FireAndForget final {
public:
    using promise_type = Task<void>::promise_type;

    FireAndForget(Task<void> task) { spawn(std::move(task)); }
};

// Set an timeout for a task, return nullopt on timeout
template <Awaitable T>
[[nodiscard]]
inline auto setTimeout(T awaitable, std::chrono::milliseconds ms) -> Task<Option<AwaitableResult<T> > > {
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

// Map an awaitable result to another type
template <Awaitable T, typename Fn>
inline auto fmap(T awaitable, Fn fn) -> task::MapAwaiter<AwaitableResult<T>, Fn> {
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

template <typename T>
[[nodiscard]]
inline auto fmap(T v) -> task::MapTags<T> {
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

template <Awaitable T, typename U>
[[nodiscard]]
inline auto operator |(T awaitable, task::MapTags<U> tag) {
    return fmap(std::move(awaitable), std::move(tag.v));
}

ILIAS_NS_END