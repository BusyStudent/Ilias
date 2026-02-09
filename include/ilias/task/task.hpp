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

#include <ilias/runtime/executor.hpp> // Executor
#include <ilias/runtime/token.hpp> // StopToken
#include <ilias/runtime/await.hpp> // Awaitable
#include <ilias/runtime/coro.hpp> // CoroPromise
#include <ilias/detail/option.hpp> // Option
#include <ilias/log.hpp>
#include <coroutine>
#include <chrono> // std::chrono::duration

ILIAS_NS_BEGIN

namespace task {

// Runtime
using runtime::StopSource;
using runtime::StopRegistration;
using runtime::CoroHandle;
using runtime::CoroPromise;
using runtime::CoroContext;
using runtime::CaptureSource;

// The return value part of the task promise
template <typename T>
class TaskPromiseBase : public CoroPromise {
public:
    auto return_value(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        mValue.emplace(std::move(value));
    }

    template <typename U>
    auto return_value(U &&value) noexcept(std::is_nothrow_constructible_v<T, U>) {
        mValue.emplace(std::forward<U>(value));
    }

    auto value() {
        rethrowIfNeeded();
        return std::move(*mValue);
    }
private:
    std::optional<T> mValue;
};

template <>
class TaskPromiseBase<void> : public CoroPromise {
public:
    auto return_void() noexcept {}
    auto value() { rethrowIfNeeded(); }
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


// The task handle
class Null {};
template <typename T = Null>
class TaskHandle; // Forward declaration

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

    auto takeException() const -> std::exception_ptr {
        return promise<promise_type>().takeException();
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
        return TaskHandle<T>(handle);
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

// Environment for the task, it bind the task with this
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

// Environment for the blocking wait task
class TaskBlockingContext final : private TaskContext {
public:
    TaskBlockingContext(TaskHandle<> task, CaptureSource source) : TaskContext(task, std::nostopstate) {
        auto executor = runtime::Executor::currentThread();
        ILIAS_ASSERT(executor, "The current thread has no executor");

        mTask.setCompletionHandler(TaskBlockingContext::onComplete);
        this->setExecutor(*executor);
        this->pushFrame("wait", source); // TRACING: trace the blocking point
    }
    TaskBlockingContext(const TaskBlockingContext &) = delete;

    auto enter() -> void {
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
        static_cast<TaskBlockingContext &>(_self).mStopExecutor.request_stop();
    }
    
    StopSource mStopExecutor; // The stop source of the executor
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
        if (mException) {
            std::rethrow_exception(mException);
        }
        if constexpr (std::is_same_v<T, void>) {
            return;
        }
        else {
            return std::move(*mValue);
        }
    }
    auto operator()() noexcept {
        ILIAS_TRY {
            mValue = makeOption([&]() { return mFn(); });
        }
        ILIAS_CATCH (...) {
            mException = std::current_exception();
        }
        mHandle.schedule();
    }
private:
    std::exception_ptr mException;
    Option<T> mValue;
    CoroHandle mHandle;
    Fn mFn; // The function to call
};

// Tags here
struct ToTaskTags {};

} // namespace task

/**
 * @brief The lazy task class, it take the ownership of the coroutine
 * 
 * @tparam T The return type of the task (default: void)
 */
template <typename T>
class [[nodiscard]] Task final {
public:
    using promise_type = task::TaskPromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;
    using value_type = T;

    Task() noexcept = default;
    Task(std::nullptr_t) noexcept {}
    Task(const Task &) = delete;
    Task(Task &&other) noexcept : mHandle(other._leak()) {}
    ~Task() noexcept { 
        if (mHandle) {
            mHandle.destroy();
        }
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

    // Set the context of the task, call on await_transform
    auto setContext(runtime::CoroContext &context) noexcept -> void {
        ILIAS_ASSERT(mHandle, "Task is null");
        auto handle = task::TaskHandle<T>(mHandle);
        handle.setContext(context);
    }

    /**
     * @brief Run the task and block until the task is done, return the value
     * 
     * @return T 
     */
    auto wait(runtime::CaptureSource source = {}) -> T {
        ILIAS_ASSERT(mHandle, "Task is null");
        auto context = task::TaskBlockingContext(_leak(), source);
        context.enter();
        return context.value<T>();
    }

    // Swap with other task
    auto swap(Task<T> &other) -> void {
        return std::swap(mHandle, other.mHandle);
    }

    auto operator =(Task<T> &&other) noexcept -> Task & {
        if (&other == this) {
            return *this;
        }
        swap(other);
        return *this;
    }

    explicit operator bool() const noexcept {
        return bool(mHandle);
    }

    auto operator co_await() && noexcept -> task::TaskAwaiter<T> {
        ILIAS_ASSERT(mHandle, "Task is null");
        return task::TaskAwaiter<T>(_leak());
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
    return task::TaskBlockingAwaiter<decltype(fn)>(std::move(fn));
}

// Sleep for a duration
[[nodiscard]]
inline auto sleep(std::chrono::milliseconds duration) -> Task<void> {
    auto count = duration.count();
    auto ucount = uint64_t(count < 0 ? 0 : count);
    return runtime::Executor::currentThread()->sleep(ucount);
}

// Abstraction for awaitable
template <Awaitable T>
inline auto toTask(T awaitable) -> Task<AwaitableResult<T> > {
    co_return co_await std::move(awaitable);
}

template <typename T>
inline auto toTask(Task<T> task) -> Task<T> {
    return task;
}

inline auto toTask() -> task::ToTaskTags {
    return {};
}

// Blocking wait an awaitable complete
template <Awaitable T>
inline auto blockingWait(T awaitable, runtime::CaptureSource source = {}) -> AwaitableResult<T> {
    return toTask(std::move(awaitable)).wait(source);
}

// Tags invoke
template <Awaitable T>
inline auto operator |(T awaitable, task::ToTaskTags) {
    return toTask(std::move(awaitable));
}

ILIAS_NS_END

// Interop with std...
template <typename T>
struct std::hash<ilias::task::TaskHandle<T> > {
    auto operator()(const ilias::task::TaskHandle<T> &handle) const -> std::size_t {
        return std::hash<ilias::runtime::CoroHandle>()(handle);
    }
};
