/**
 * @file task.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The task class, provide the stackless coroutine support
 * @version 0.3
 * @date 2024-08-09
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/runtime/exception.hpp> // ExceptionPtr
#include <ilias/runtime/executor.hpp> // Executor
#include <ilias/runtime/tracing.hpp> // TracingSubscriber
#include <ilias/runtime/token.hpp> // StopToken
#include <ilias/runtime/await.hpp> // Awaitable
#include <ilias/runtime/coro.hpp> // CoroPromise
#include <ilias/detail/option.hpp> // Option
#include <ilias/result.hpp> // Result
#include <ilias/log.hpp>
#include <coroutine>
#include <chrono> // std::chrono::duration

// HALO attribute for clang
#if __has_cpp_attribute(clang::coro_await_elidable)
    #define ILIAS_CORO_AWAIT_ELIDABLE clang::coro_await_elidable
#else
    #define ILIAS_CORO_AWAIT_ELIDABLE
#endif // __has_cpp_attribute(clang::coro_await_elidable)

#if __has_cpp_attribute(clang::coro_await_elidable_argument)
    #define ILIAS_CORO_ELIDABLE_ARGUMENT clang::coro_await_elidable_argument
#else
    #define ILIAS_CORO_ELIDABLE_ARGUMENT
#endif // __has_cpp_attribute(clang::coro_await_elidable_argument)

ILIAS_NS_BEGIN

namespace task {

// Runtime
using runtime::StopSource;
using runtime::StopRegistration;
using runtime::CoroHandle;
using runtime::CoroPromise;
using runtime::CoroContext;
using runtime::CaptureSource;
using runtime::ExceptionPtr;

// Forward declaration
class Null {};
template <typename T>
class TaskTryAwaiter;

// MARK: TaskPromise
// The return value part of the task promise
template <typename T>
class TaskPromiseBase : public CoroPromise {
public:
    // co_return
    auto return_value(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        mValue.emplace(std::move(value));
    }

    template <typename U>
    auto return_value(U &&value) noexcept(std::is_nothrow_constructible_v<T, U>) {
        mValue.emplace(std::forward<U>(value));
    }

    auto value() {
        this->rethrowIfAny();
        return std::move(*mValue);
    }
private:
    std::optional<T> mValue;
};

template <>
class TaskPromiseBase<void> : public CoroPromise {
public:
    auto return_void() noexcept {}
    auto value() { this->rethrowIfAny(); }
};

template <typename T>
class TaskPromise final : public TaskPromiseBase<T> {
public:
    using handle_type = std::coroutine_handle<TaskPromise<T> >;

    // Build the task object here, we use capture source to capture the task creation position
    auto get_return_object(CaptureSource where = {}) noexcept -> Task<T> {
        this->mCreation = where;
        return {handle()};
    }

    auto handle() noexcept -> handle_type {
        return handle_type::from_promise(*this);
    }
};

// MARK: TaskHandle
// The task handle
template <typename T = Null>
class TaskHandle; 

// The type erased task handle
template <>
class TaskHandle<Null> : public CoroHandle {
public:
    template <typename T>
    TaskHandle(std::coroutine_handle<TaskPromise<T> > h) : CoroHandle(h) {}
    TaskHandle(std::nullptr_t) {}
    TaskHandle() = default;
    TaskHandle(const TaskHandle &) = default;
};

template <typename T>
class TaskHandle final : public TaskHandle<Null> {
public:
    using promise_type = TaskPromise<T>;

    TaskHandle(std::coroutine_handle<promise_type> h) : TaskHandle<Null>(h) {}
    TaskHandle(std::nullptr_t) {}
    TaskHandle(const TaskHandle &) = default;

    auto value() const -> T {
        return promise<promise_type>().value();
    }

    /**
     * @brief Cast the task handle to another type
     * @note It is UB if the task is not the same type
     * 
     * @param h 
     * @return TaskHandle<T> 
     */
    static auto cast(TaskHandle<> h) -> TaskHandle<T> {
        auto &promise = h.promise<promise_type>();
        auto handle = std::coroutine_handle<promise_type>::from_promise(promise);
        return handle;
    }
};

// Awaiter
class TaskAwaiterBase {
public:
    TaskAwaiterBase(TaskAwaiterBase &&other) noexcept : 
        mTask(std::exchange(other.mTask, nullptr)) 
    {

    }

    auto await_ready() const noexcept -> bool {
        mTask.resume();
        return mTask.done();
    }

    auto await_suspend(CoroHandle caller) noexcept {
        mTask.setPrevAwaiting(caller);
    }
protected:
    TaskAwaiterBase(TaskHandle<> task) noexcept : mTask(task) { }
    ~TaskAwaiterBase() {
        if (mTask) {
            mTask.destroy();
        }
    }

    TaskHandle<> mTask; // The task we use to wait for (take ownership)
};

template <typename T>
class TaskAwaiter final : public TaskAwaiterBase {
public:
    TaskAwaiter(TaskHandle<T> task) noexcept : TaskAwaiterBase(task) { }

    /**
     * @brief Get the result of the task
     * 
     * @return T>
     */
    auto await_resume() const -> T {
        ILIAS_ASSERT(mTask.done(), "The task is not done, maybe call resume() twice");
        return TaskHandle<T>::cast(mTask).value();
    }
};

// Environment for the task, it bind the task with this (take ownership)
class TaskContext : public CoroContext {
public:
    template <typename ...Super>
    TaskContext(TaskHandle<> task, Super ...super) : CoroContext(super...), mTask(task) {
        if (mTask) {
            mTask.setContext(*this);
        }
    }

    TaskContext(TaskContext &&other) noexcept : CoroContext(std::move(other)) {
        mTask = std::exchange(other.mTask, nullptr);
        if (mTask) { // rebind the context
            mTask.setContext(*this);
        }
    }

    ~TaskContext() {
        if (mTask) {
            mTask.destroy();
        }
    }

    // Bind a new task on it, destroy the previous one
    auto setTask(TaskHandle<> newTask) noexcept {
        if (mTask) {
            mTask.destroy();
        }
        if (newTask) {
            newTask.setContext(*this);
        }
        mTask = newTask;
    }

    // Get the task in it
    auto task() const noexcept {
        return mTask;
    }
protected:
    TaskHandle<> mTask; // The task we use to wait for (take ownership)
};

// Environment for the blocking wait task (borrow the task ownship)
class TaskBlockingContext final : private CoroContext {
public:
    TaskBlockingContext(TaskHandle<> task, CaptureSource source) : CoroContext(std::nostopstate), mTask(task), mSource(source) {
        auto executor = runtime::Executor::currentThread();
        ILIAS_ASSERT(executor, "The current thread has no executor");

        mTask.setCompletionHandler(TaskBlockingContext::onComplete);
        mTask.setContext(*this);
        this->setExecutor(*executor);
        this->pushFrame("wait", source); // TRACING: trace the blocking point
    }
    TaskBlockingContext(const TaskBlockingContext &) = delete;

    auto enter() -> void {
        this->tracingSpawn(mSource); // TRACING: blocking wait is also spawn
        mTask.resume();
        if (!mTask.done()) {
            executor().run(mStopExecutor.get_token());            
        }
        ILIAS_ASSERT(mTask.done(), "??? INTERNAL BUG");
    }

    template <typename T>
    auto value() -> T {
        return TaskHandle<T>::cast(mTask).value();
    }
private:
    static auto onComplete(CoroContext &_self) -> void { // Break the event loop
        _self.tracingComplete(); // TRACING: completion
        static_cast<TaskBlockingContext &>(_self).mStopExecutor.request_stop();
    }
    
    TaskHandle<> mTask; // The task we use to wait for (borrow)
    StopSource   mStopExecutor; // The stop source of the executor
    [[ILIAS_NO_UNIQUE_ADDRESS]]
    CaptureSource mSource; // The source location of the blocking wait
};

// Awaiter for blocking
template <std::invocable Fn>
class TaskBlockingAwaiter final : public runtime::CallableImpl<TaskBlockingAwaiter<Fn> > {
public:
    using T = std::invoke_result_t<Fn>;

    TaskBlockingAwaiter(Fn fn) : mFn(std::move(fn)) {}
    TaskBlockingAwaiter(TaskBlockingAwaiter &&) = default;

    auto await_ready() const noexcept { return false; }
    auto await_suspend(CoroHandle caller) {
        mHandle = caller;
        runtime::threadpool::submit(*this);
    }
    auto await_resume() -> T {
        mException.rethrowIfAny();
        if constexpr (std::is_same_v<T, void>) {
            return;
        }
        else {
            return std::move(*mValue);
        }
    }
    auto operator()() noexcept {
        ILIAS_TRY_EXCEPTION {
            mValue = makeOption([&]() { return mFn(); });
        }
        ILIAS_CATCH (...) {
            mException = ExceptionPtr::currentException();
        }
        mHandle.schedule();
    }
private:
    [[ILIAS_NO_UNIQUE_ADDRESS]]
    ExceptionPtr mException;
    CoroHandle mHandle;
    Option<T> mValue;
    Fn mFn; // The function to call
};

// Functor for the toTask(xxx) & xxx() | toTask
class ToTask {
public:
    template <Awaitable T>
    static auto impl([[ILIAS_CORO_ELIDABLE_ARGUMENT]] T awaitable) -> Task<AwaitableResult<T> > {
        co_return co_await std::move(awaitable);
    }

    template <typename T>
    static auto impl([[ILIAS_CORO_ELIDABLE_ARGUMENT]] Task<T> task) -> Task<T> {
        return task;
    }

    // Impl toTask(xxx())
    template <Awaitable T>
    auto operator()([[ILIAS_CORO_ELIDABLE_ARGUMENT]] T awaitable) const {
        return impl(std::move(awaitable));
    }

    // Impl xxx() | toTask
    template <Awaitable T>
    friend auto operator |([[ILIAS_CORO_ELIDABLE_ARGUMENT]] T awaitable, const ToTask &) {
        return impl(std::move(awaitable));
    }
};

// Functor for blockingWait(xxx) && xxx() | blockingWait
class BlockingWait {
public:
    // Impl blockingWait(xxx())
    template <Awaitable T>
    auto operator()(T awaitable) const {
        return ToTask::impl(std::move(awaitable)).wait();
    }

    // Impl xxx() | blockingWait
    template <Awaitable T>
    friend auto operator |(T awaitable, const BlockingWait &) {
        return ToTask::impl(std::move(awaitable)).wait();
    }
};

} // namespace task

/**
 * @brief The lazy task class, it take the ownership of the coroutine
 * 
 * @tparam T The return type of the task (default: void)
 */
template <typename T>
class [[nodiscard]] [[ILIAS_CORO_AWAIT_ELIDABLE]] Task final {
public:
    using promise_type = task::TaskPromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;
    using value_type = T;
   
    Task() = default;
    Task(const Task &) = delete; // Disable copy
    Task(std::nullptr_t) noexcept {}
    Task(Task &&other) noexcept : mHandle(other._leak()) {}
    ~Task() { clear(); }

    /**
     * @brief Clear the inernal coroutine handle
     * 
     */
    auto clear() noexcept -> void {
        if (mHandle) {
            mHandle.destroy();
            mHandle = nullptr;
        }
    }

    /**
     * @brief Run the task and block until the task is done, return the value
     * 
     * @return T 
     */
    auto wait(runtime::CaptureSource source = {}) -> T {
        ILIAS_ASSERT(mHandle, "Task is null");
        ILIAS_ASSERT(!mHandle.done(), "Task is done, can't wait again");
        auto context = task::TaskBlockingContext {mHandle, source};
        context.enter();
        return context.value<T>();
    }

    /**
     * @brief Leak the std coroutine handle
     * @note It is internal function, you should not use it outside the library
     * 
     * @return handle_type 
     */
    auto _leak() noexcept -> handle_type {
        return std::exchange(mHandle, nullptr);
    }

    /**
     * @brief Get the std coroutine handle
     * @note It is internal function, you should not use it it outside the library
     * 
     * @return handle_type 
     */
    auto _handle() const noexcept -> handle_type {
        return mHandle;
    }

    /**
     * @brief Set the context of the task, call on await_transform
     * @note It is internal function, called by runtime
     * 
     * @param context 
     */
    auto setContext(runtime::CoroContext &context) noexcept -> void {
        ILIAS_ASSERT(mHandle, "Task is null");
        auto handle = task::TaskHandle<T> {mHandle};
        handle.setContext(context);
    }

    // Swap with other task
    auto swap(Task<T> &other) -> void {
        return std::swap(mHandle, other.mHandle);
    }

    auto operator =(Task<T> &&other) noexcept -> Task & {
        swap(other);
        return *this;
    }

    auto operator co_await() && noexcept -> task::TaskAwaiter<T> {
        ILIAS_ASSERT(mHandle, "Task is null");
        return task::TaskAwaiter<T> {_leak()};
    }

    explicit operator bool() const noexcept {
        return bool(mHandle);
    }
private:
    Task(handle_type handle) noexcept : mHandle(handle) {}

    handle_type mHandle;
friend class task::TaskPromise<T>;
};

// Awaiting a blocking task by using given callable
template <std::invocable Fn>
[[nodiscard]]
inline auto blocking(Fn fn) {
    return task::TaskBlockingAwaiter<Fn> {std::move(fn)};
}

// Sleep for a duration
[[nodiscard]]
inline auto sleep(std::chrono::nanoseconds duration) -> Task<void> {
    if (duration.count() < 0) {
        duration = std::chrono::nanoseconds::zero();
    }
    return runtime::Executor::currentThread()->sleep(duration);
}

// Sleep until a time point
template <typename Clock, typename Duration>
[[nodiscard]]
inline auto sleepUntil(std::chrono::time_point<Clock, Duration> timepoint) -> Task<void> {
    return sleep(timepoint - Clock::now());
}

// Convert any awaitable types to Task<T>
constexpr inline task::ToTask toTask{};

// Blocking wait an awaitable complete
constexpr inline task::BlockingWait blockingWait{};

ILIAS_NS_END

// Interop with std...
template <typename T>
struct std::hash<ilias::task::TaskHandle<T> > {
    auto operator()(const ilias::task::TaskHandle<T> &handle) const -> std::size_t {
        return std::hash<ilias::runtime::CoroHandle>{}(handle);
    }
};
