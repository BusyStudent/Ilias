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
#if defined(ILIAS_CORO_TRACE)
        // TRACING: mark the current await point is unstoppable
        if (auto frame = mCtxt.topFrame(); frame) {
            frame->setMessage("unstoppable");
        }
#endif // defined(ILIAS_CORO_TRACE)
        // We just need the executor info from the context
        mCtxt.setParent(ctxt);
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
    ScheduleAwaiterBase(runtime::Executor &exec, TaskHandle<> handle) : TaskContext(handle), mHandle(handle), mExecutor(exec) {}
    ScheduleAwaiterBase(ScheduleAwaiterBase &&) = default;

    auto await_ready() -> bool { return false; }

    ILIAS_API
    auto await_suspend(runtime::CoroHandle caller) -> void;

    // TRACING: mark the await point is scheduleOn
#if defined(ILIAS_CORO_TRACE)
    auto setContext(runtime::CoroContext &ctxt) {
        if (auto frame = ctxt.topFrame(); frame) {
            frame->setMessage("scheduleOn");
        }
        this->setParent(ctxt);
    }
#endif // defined(ILIAS_CORO_TRACE)

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

    // SAFETY: Compiler pin awaiter on await start
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
    FinallyAwaiterBase(TaskHandle<> main) : mContext(main) {}
    FinallyAwaiterBase(FinallyAwaiterBase &&) = default;
    ~FinallyAwaiterBase() = default;

    ILIAS_API
    auto await_suspend(runtime::CoroHandle caller) -> std::coroutine_handle<>;
    auto await_ready() -> bool { return false; } // Always suspend, because althrough the task is ready, we need to call the finally handler

    auto setContext(runtime::CoroContext &ctxt) noexcept {
#if defined(ILIAS_CORO_TRACE)
        // TRACING: mark the current await point is finally
        if (auto frame = ctxt.topFrame(); frame) {
            frame->setMessage("finally");
        }
#endif // defined(ILIAS_CORO_TRACE)
        mContext->setParent(ctxt);
        mContext->setExecutor(ctxt.executor());
    }
protected:
    // Virtual method ...
    TaskHandle<>             (*mOnTaskCompletion)(FinallyAwaiterBase &self) = nullptr;
    std::optional<TaskContext> mContext;
private:
    auto onTaskCompletion() -> void;
    auto onFinallyCompletion() -> void;

    bool mStopped = false; // Did main ctxt receive the stop request and actually stopped ?
    runtime::CoroHandle mCaller;
    runtime::StopRegistration mReg;
};

template <typename T, typename Cleanup>
class FinallyAwaiter final : public FinallyAwaiterBase {
public:
    FinallyAwaiter(TaskHandle<T> main, Cleanup cleanup) : FinallyAwaiterBase(main), mCleanup(std::move(cleanup)) {
        mOnTaskCompletion = FinallyAwaiter::onCompletion;
    }

    auto await_resume() -> T {
        if (mException) {
            std::rethrow_exception(mException);
        }
        return unwrapOption(std::move(mValue));
    }
private:
    template <typename U>
    auto makeCleanup(Task<U> &task) -> TaskHandle<> {
        return std::exchange(task, {})._leak();
    }

    template <std::invocable U>
    auto makeCleanup(U &&fn) -> TaskHandle<> {
        if constexpr (requires { fn(mValue); }) { // Check the cleanup function can take the value reference?
            return fn(mValue)._leak();
        }
        else {
            return fn()._leak();
        }
    }

    static auto onCompletion(FinallyAwaiterBase &_self) -> TaskHandle<> {
        auto &self = static_cast<FinallyAwaiter &>(_self);
        do {
            auto &ctxt = *(self.mContext);
            auto handle = TaskHandle<T>::cast(ctxt.task());
            if (ctxt.isStopped()) {
                break; // Nothing to do
            }
            self.mException = handle.takeException();
            if (self.mException) {
                break;
            }
            self.mValue = makeOption([&]() { return handle.value(); });
            break;
        }
        while (0);
        // Prepare the cleanup task
        return self.makeCleanup(self.mCleanup);
    }

    std::exception_ptr mException;
    Cleanup   mCleanup;
    Option<T> mValue;
};

// Implement await on an stop token
class StopTokenAwaiter final {
public:
    StopTokenAwaiter(runtime::StopToken token) : mToken(std::move(token)) {}
    StopTokenAwaiter(StopTokenAwaiter &&) = default;
    ~StopTokenAwaiter() = default;

    auto await_ready() -> bool { return mToken.stop_requested(); }
    auto await_resume() -> void {}

    ILIAS_API
    auto await_suspend(runtime::CoroHandle caller) -> void;
private:
    auto onStopRequested() -> void;
    auto onRuntimeStopRequested() -> void;

    // SAFETY: Compiler pin awaiter on await start
    bool mCompleted {false}; // We use std::atomic_ref internal, make the compiler happy:(, std::atomic<T> can't move
    runtime::StopToken mToken;
    runtime::CoroHandle mCaller;
    runtime::StopRegistration mReg;
    runtime::StopRegistration mRuntimeReg;
};

} // namespace task

// Add transform
namespace runtime {
    template <typename T> requires(std::is_same_v<std::remove_cvref_t<T>, StopToken>)
    struct IntoRawAwaitableTraits<T> {
        static auto into(T &&token) -> task::StopTokenAwaiter { return {std::forward<T>(token)}; }
    };
} // namespace runtime

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
inline auto finally(T awaitable, Task<U> cleanup) -> task::FinallyAwaiter<AwaitableResult<T>, Task<U> > {
    return {toTask(std::move(awaitable))._leak(), std::move(cleanup)};
}

// Add an async cleanup handler to an awaitable
template <Awaitable T, std::invocable Fn>
[[nodiscard]]
inline auto finally(T awaitable, Fn fn) -> task::FinallyAwaiter<AwaitableResult<T>, Fn> {
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