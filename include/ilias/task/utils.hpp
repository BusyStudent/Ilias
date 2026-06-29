#pragma once

#include <ilias/runtime/exception.hpp>
#include <ilias/runtime/token.hpp>
#include <ilias/runtime/coro.hpp>
#include <ilias/task/when_any.hpp>
#include <ilias/task/task.hpp>
#include <chrono> // std::chrono::nanoseconds

ILIAS_NS_BEGIN

namespace task {

// MARK: Unstoppable
template <typename T>
class [[ILIAS_CORO_AWAIT_ELIDABLE]] UnstoppableAwaiter final {
public:
    UnstoppableAwaiter(TaskHandle<T> handle) : mHandle(handle), mAwaiter(handle) {}
    UnstoppableAwaiter(UnstoppableAwaiter &&) = default;

    auto await_ready() -> bool {
        mCtxt.tracingSpawn(mSource);
        mHandle.setContext(mCtxt); // Use the unstoppable context
        return mAwaiter.await_ready();
    }

    auto await_suspend(runtime::CoroHandle caller) {
        return mAwaiter.await_suspend(caller);
    }

    auto await_resume() -> T {
        mCtxt.tracingComplete();
        return mAwaiter.await_resume();
    }

    // Set the context of the task, call on await_transform
    auto setContext(runtime::CoroContext &ctxt, runtime::CaptureSource sorce) {
#if defined(ILIAS_CORO_TRACE)
        // TRACING: mark the current await point is unstoppable
        if (auto frame = mCtxt.topFrame(); frame) {
            frame->setMessage("unstoppable");
        }
#endif // defined(ILIAS_CORO_TRACE)
        // We just need the executor info from the context
        mCtxt.setParent(ctxt);
        mCtxt.setExecutor(ctxt.executor());
        mSource = sorce;
    }
private:
    [[ILIAS_NO_UNIQUE_ADDRESS]]
    runtime::CaptureSource mSource; // Await point source location
    runtime::CoroContext mCtxt {std::nostopstate};
    TaskHandle<T>  mHandle;
    TaskAwaiter<T> mAwaiter;
};

// MARK: Map
// Add an sync map handler to awaitable
template <typename T, typename Fn>
class [[ILIAS_CORO_AWAIT_ELIDABLE]] MapAwaiter final {
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

// MARK: Schedule
// Schedule task on another executor
class [[ILIAS_CORO_AWAIT_ELIDABLE]] ScheduleAwaiterBase : private TaskContext {
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
class [[ILIAS_CORO_AWAIT_ELIDABLE]] ScheduleAwaiter final : public ScheduleAwaiterBase {
public:
    ScheduleAwaiter(runtime::Executor &exec, TaskHandle<T> handle) : ScheduleAwaiterBase(exec, handle) {}

    auto await_resume() -> T {
        return TaskHandle<T>::cast(mHandle).value();
    }
};

// MARK: Finally
// Add an async cleanup handler to an awaitable
class [[ILIAS_CORO_AWAIT_ELIDABLE]] FinallyAwaiterBase {
public:
    FinallyAwaiterBase(TaskHandle<> main) : mContext(main) {}
    FinallyAwaiterBase(FinallyAwaiterBase &&) = default;
    ~FinallyAwaiterBase() = default;

    ILIAS_API
    auto await_suspend(runtime::CoroHandle caller) -> std::coroutine_handle<>;
    auto await_ready() -> bool { return false; } // Always suspend, because althrough the task is ready, we need to call the finally handler

    auto setContext(runtime::CoroContext &ctxt, runtime::CaptureSource source) noexcept {
#if defined(ILIAS_CORO_TRACE)
        // TRACING: mark the current await point is finally
        if (auto frame = ctxt.topFrame(); frame) {
            frame->setMessage("finally");
        }
#endif // defined(ILIAS_CORO_TRACE)
        mSource = source;
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
    [[ILIAS_NO_UNIQUE_ADDRESS]]
    runtime::CaptureSource mSource; // Await point source location
    runtime::CoroHandle    mCaller;
    runtime::StopRegistration mReg;
};

// The finally awaiter
// Cleanup is a function or Task<T> to run after the main task is completed
template <typename T, typename Cleanup>
class [[ILIAS_CORO_AWAIT_ELIDABLE]] FinallyAwaiter final : public FinallyAwaiterBase {
public:
    FinallyAwaiter(TaskHandle<T> main, Cleanup cleanup) : FinallyAwaiterBase(main), mCleanup(std::move(cleanup)) {
        mOnTaskCompletion = &FinallyAwaiter::onCompletionHelper;
    }

    auto await_resume() -> T {
        mException.rethrowIfAny();
        return unwrapOption(std::move(mValue));
    }
private:
    template <typename U>
    auto makeCleanup(Task<U> &task) -> TaskHandle<> {
        return task._leak();
    }

    template <std::invocable U>
    auto makeCleanup(U &&fn) -> TaskHandle<> {
        return fn()._leak();
    }

    auto onCompletion() -> TaskHandle<> {
        do {
            ILIAS_ASSERT(mContext, "The main task should be alive");
            auto handle = TaskHandle<T>::cast(mContext->task());
            if (mContext->isStopped()) {
                break; // Nothing to do
            }
            mException = handle.takeException();
            if (mException) {
                break;
            }
            mValue = makeOption([&]() { return handle.value(); });
            break;
        }
        while (0);

        // Prepare the cleanup task
        return makeCleanup(mCleanup);
    }

    static auto onCompletionHelper(FinallyAwaiterBase &_self) -> TaskHandle<> {
        return static_cast<FinallyAwaiter &>(_self).onCompletion();
    }

    ExceptionPtr mException;
    Cleanup   mCleanup;
    Option<T> mValue;
};

// MARK: StopToken
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

// Implement suspend foreever, only wait for the stop requested
class SuspendAlwaysAwaiter {
public:
    auto await_ready() -> bool { return false; }
    auto await_suspend(runtime::CoroHandle caller) -> void {
        mCaller = caller;
        mReg.register_<&SuspendAlwaysAwaiter::onStopRequested>(caller.stopToken(), this);
    }
    auto await_resume() -> void { ILIAS_UNREACHABLE(); }
private:
    auto onStopRequested() -> void {
        mCaller.setStopped();
    }

    runtime::CoroHandle mCaller;
    runtime::StopRegistration mReg;
};

// MARK: Functors
// Functor for timeout
class Timeout {
public:
    struct Tags { std::chrono::nanoseconds ns; };

    template <Awaitable T>
    static auto impl([[ILIAS_CORO_ELIDABLE_ARGUMENT]] T awaitable, std::chrono::nanoseconds ns) -> Task<Option<AwaitableResult<T> > > {
        auto [res, timeout] = co_await whenAny(std::move(awaitable), sleep(ns));
        if (timeout) {
            co_return std::nullopt;
        }
        co_return std::move(*res);
    }

    // Impl timeout(xxx(), 10ms)
    template <Awaitable T>
    [[nodiscard]]
    auto operator()([[ILIAS_CORO_ELIDABLE_ARGUMENT]] T awaitable, std::chrono::nanoseconds ns) const {
        return impl(std::move(awaitable), ns);
    }

    // Impl xxx() | timeout(10ms)
    [[nodiscard]]
    auto operator ()(std::chrono::nanoseconds ns) const -> Tags {
        return {ns};
    }

    template <Awaitable T>
    [[nodiscard]]
    friend auto operator |([[ILIAS_CORO_ELIDABLE_ARGUMENT]] T awaitable, Tags timeout) {
        return impl(std::move(awaitable), timeout.ns);
    }
};

// Functor for unstoppable
class Unstoppable {
public:
    // Impl unstoppable(xxx())
    template <Awaitable T>
    [[nodiscard]]
    auto operator()([[ILIAS_CORO_ELIDABLE_ARGUMENT]] T awaitable) const -> UnstoppableAwaiter<AwaitableResult<T> > {
        return {toTask(std::move(awaitable))._leak()};
    }

    // Impl xxx() | unstoppable
    template <Awaitable T>
    [[nodiscard]]
    friend auto operator |([[ILIAS_CORO_ELIDABLE_ARGUMENT]] T awaitable, Unstoppable self) {
        return self(std::move(awaitable));
    }
};

// Functor for scheduleOn
class ScheduleOn {
public:
    struct Tags { runtime::Executor &exec; };

    // Impl scheduleOn(xxx(), exec)
    template <Awaitable T>
    [[nodiscard]]
    auto operator()([[ILIAS_CORO_ELIDABLE_ARGUMENT]] T awaitable, runtime::Executor &exec) const -> ScheduleAwaiter<AwaitableResult<T> > {
        return {exec, toTask(std::move(awaitable))._leak()};
    }

    // Impl xxx() | scheduleOn(exec)
    [[nodiscard]]
    auto operator()(runtime::Executor &exec) const -> Tags {
        return {exec};
    }

    template <Awaitable T>
    [[nodiscard]]
    friend auto operator |([[ILIAS_CORO_ELIDABLE_ARGUMENT]] T awaitable, Tags scheduleOn) -> ScheduleAwaiter<AwaitableResult<T> > {
        return ScheduleOn{}(std::move(awaitable), scheduleOn.exec);
    }
};

// Functor for finally
class Finally {
public:
    template <typename T>
    struct Tags { T v; };

    // Impl finally(xxx(), cleanup())
    template <Awaitable T, Awaitable Cleanup>
    [[nodiscard]]
    auto operator ()(
        [[ILIAS_CORO_ELIDABLE_ARGUMENT]] T awaitable, 
        [[ILIAS_CORO_ELIDABLE_ARGUMENT]] Cleanup cleanup) const -> FinallyAwaiter<AwaitableResult<T>, Task<AwaitableResult<Cleanup> > >
    {
        return {
            toTask(std::move(awaitable))._leak(),
            toTask(std::move(cleanup))
        };
    }

    // Impl finally(xxx(), cleanup)
    template <Awaitable T, std::invocable Fn>
    [[nodiscard]]
    auto operator ()([[ILIAS_CORO_ELIDABLE_ARGUMENT]] T awaitable, Fn fn) const -> FinallyAwaiter<AwaitableResult<T>, Fn> {
        return {
            toTask(std::move(awaitable))._leak(),
            std::move(fn)
        };
    }

    // Impl xxx() | finally(something)
    template <typename T>
    [[nodiscard]]
    auto operator ()(T v) const -> Tags<T> {
        return {std::move(v)};
    }

    template <Awaitable T, typename U>
    [[nodiscard]]
    friend auto operator |([[ILIAS_CORO_ELIDABLE_ARGUMENT]] T awaitable, Tags<U> tag) -> FinallyAwaiter<AwaitableResult<T>, U> {
        return Finally{}(std::move(awaitable), std::move(tag.v));
    }
};

// Functor for map
class Map {
public:
    template <typename T>
    struct Tags { T v; };

    // Impl map(xxx(), fn)
    template <Awaitable T, typename Fn>
    [[nodiscard]]
    auto operator ()([[ILIAS_CORO_ELIDABLE_ARGUMENT]] T awaitable, Fn fn) const -> MapAwaiter<AwaitableResult<T>, Fn> {
        return {toTask(std::move(awaitable))._leak(), std::move(fn)};
    }

    // Impl xxx() | map(fn)
    template <typename T>
    [[nodiscard]]
    auto operator ()(T v) const -> Tags<T> {
        return {std::move(v)};
    }

    template <Awaitable T, typename U>
    [[nodiscard]]
    friend auto operator |([[ILIAS_CORO_ELIDABLE_ARGUMENT]] T awaitable, Tags<U> tag) -> MapAwaiter<AwaitableResult<T>, U> {
        return Map{}(std::move(awaitable), std::move(tag.v));
    }
    
};

} // namespace task

// impl co_await source.get_token()
template <typename T> requires(std::is_same_v<std::remove_cvref_t<T>, runtime::StopToken>)
struct runtime::IntoRawAwaitableTraits<T> {
    static auto into(T &&token) -> task::StopTokenAwaiter { return {std::forward<T>(token)}; }
};

// Public interface
// Set an timeout for a task, return nullopt on timeout
constexpr inline task::Timeout timeout{};

// Make a awaitable execute on another executor
constexpr inline task::ScheduleOn scheduleOn{};

// Make a awaitable execute on an unstoppable context
constexpr inline task::Unstoppable unstoppable{};

// Add an async cleanup task or handler to an awaitable
constexpr inline task::Finally finally{};

// Map an awaitable result to another type
constexpr inline task::Map fmap{};

ILIAS_NS_END