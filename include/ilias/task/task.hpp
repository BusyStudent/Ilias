/**
 * @file task.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The task class, provide the coroutine support
 * @version 0.3
 * @date 2024-08-09
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/runtime/executor.hpp>
#include <ilias/runtime/await.hpp>
#include <ilias/runtime/coro.hpp>
#include <ilias/log.hpp>
#include <coroutine>
#include <optional> // std::optional
#include <variant> // std::monostate
#include <chrono> // std::chrono::duration

ILIAS_NS_BEGIN

namespace task::option {

template <typename T>
struct ReplaceVoid {
    using type = T;  
};

template <>
struct ReplaceVoid<void> {
    using type = std::monostate;
};

// For replace the fucking void to std::monostate :(
template <typename T>
using Option = std::optional<typename ReplaceVoid<T>::type>;

// Create an option with 'value'
template <std::invocable Fn>
auto makeOption(Fn &&fn) -> Option<std::invoke_result_t<Fn> > {
    if  constexpr (std::is_same_v<std::invoke_result_t<Fn>, void>) {
        fn();
        return std::monostate{};
    }
    else {
        return fn();
    }
}

} // namespace task::option

namespace task {

using runtime::CoroHandle;
using runtime::CoroPromise;
using runtime::CoroContext;
using runtime::StopSource;
using option::makeOption;
using option::Option;

// The return value part of the task promise
template <typename T>
class TaskPromiseBase : public CoroPromise {
public:
    auto return_value(T value) noexcept {
        mValue.emplace(std::move(value));
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

    auto get_return_object() noexcept -> Task<T> {
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
        return TaskHandle<T>(handle);
    }
};

// Awaiter
class TaskAwaiterBase {
public:
    TaskAwaiterBase(TaskAwaiterBase &&other) : 
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
    TaskAwaiterBase(TaskHandle<> task) : mTask(task) { }
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
    TaskAwaiter(TaskHandle<T> task) : TaskAwaiterBase(task) { }

    /**
     * @brief Get the result of the task
     * 
     * @return T>
     */
    auto await_resume() const -> T {
        ILIAS_ASSERT_MSG(mTask.done(), "The task is not done, maybe call resume() twice");
        return TaskHandle<T>::cast(mTask).value();
    }
};

// Environment for the blocking wait task
class TaskBlockingContext final : public CoroContext {
public:
    TaskBlockingContext(TaskHandle<> task) : mTask(task) {
        mTask.setCompletionHandler(TaskBlockingContext::onComplete);
        mTask.setContext(*this);
        this->executor = runtime::Executor::currentThread();
        this->stoppedHandler = [](auto) {
            std::terminate(); // The blocking unsupport cancelled
        };
        ILIAS_ASSERT_MSG(this->executor, "The current thread has no executor");
    }

    auto enter() -> void {
        mTask.resume();
        if (!mTask.done()) {
            this->executor->run(mStopExecutor.get_token());            
        }
        ILIAS_ASSERT_MSG(mTask.done(), "??? INTERNAL BUG");
    }
private:
    static auto onComplete(CoroContext &_self) -> void { // Break the event loop
        static_cast<TaskBlockingContext &>(_self).mStopExecutor.request_stop();
    }
    
    TaskHandle<> mTask; // The task we use to wait for (doesn't take ownership)
    StopSource mStopExecutor; // The stop source of the executor
};

// Environment for the spawn task
class TaskSpawnContext final : public CoroContext {
public:
    TaskSpawnContext(TaskHandle<> task) : mTask(task) {
        mTask.setContext(*this);
        mTask.setCompletionHandler(TaskSpawnContext::onComplete);
        this->stoppedHandler = TaskSpawnContext::onComplete;
        this->executor = runtime::Executor::currentThread();
        ILIAS_ASSERT_MSG(this->executor, "The current thread has no executor");
    }
    ~TaskSpawnContext() {
        mTask.destroy();
    }

    // Blocking enter the executor
    auto enter() -> void {
        if (!mCompleted) {
            this->executor->run(mStopExecutor.get_token());
        }
    }

    // Get the value of the task, nullopt if the task is stopped
    template <typename T>
    auto value() -> Option<T> {
        ILIAS_ASSERT_MSG(mCompleted, "??? INTERNAL BUG");
        if (isStopped()) {
            return std::nullopt;
        }
        return makeOption([&]() {
            return TaskHandle<T>::cast(mTask).value();
        });
    }

    static auto make(TaskHandle<> task) -> std::shared_ptr<TaskSpawnContext> {
        auto ptr = std::make_shared<TaskSpawnContext>(task);
        ptr->mSelf = ptr;
        ptr->mTask.schedule(); // Schedule the task
        return ptr;
    }
private:
    static auto onComplete(CoroContext &_self) -> void { 
        auto &self = static_cast<TaskSpawnContext &>(_self);

        // Done..
        self.mCompleted = true;
        self.executor->post(TaskSpawnContext::derefSelf, &self);
    }

    // deref self in event loop
    static auto derefSelf(void *_self) -> void {
        auto ptr = std::move(static_cast<TaskSpawnContext *>(_self)->mSelf);
        ptr->mStopExecutor.request_stop();
        ptr.reset();
    }

    std::shared_ptr<TaskSpawnContext> mSelf; // Used to avoid free self dur execution
    TaskHandle<> mTask; // The task we use to wait for (take ownership)
    StopSource mStopExecutor; // The stop source of the executor
    bool mCompleted = false;
};

} // namespace task

/**
 * @brief The lazy task class, it take the ownership of the coroutine
 * 
 * @tparam T The return type of the task (default: void)
 */
template <typename T>
class Task {
public:
    using promise_type = task::TaskPromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;
    using value_type = T;

    Task() = default;
    Task(std::nullptr_t) { }
    Task(const Task &) = delete;
    Task(Task &&other) noexcept : mHandle(other._leak()) { }
    ~Task() noexcept { 
        if (mHandle) {
            mHandle.destroy();
        }
    }

    /**
     * @brief Leak the std coroutine handle
     * @note It is internal function, you should not use it
     * 
     * @return handle_type 
     */
    auto _leak() -> handle_type {
        return std::exchange(mHandle, nullptr);
    }

    // Set the context of the task, call on await_transform
    auto setContext(runtime::CoroContext &context) -> void {
        auto handle = task::TaskHandle<T>(mHandle);
        handle.setContext(context);
    }

    /**
     * @brief Run the task and block until the task is done, return the value
     * 
     * @return T 
     */
    auto wait() && -> T {
        auto context = task::TaskBlockingContext(mHandle);
        context.enter();
        return mHandle.promise().value();
    }

    auto operator =(Task<T> &&other) -> Task & {
        if (&other == this) {
            return *this;
        }
        if (mHandle) {
            mHandle.destroy();
        }
        mHandle = other._leak();
        return *this;
    }

    explicit operator bool() const noexcept {
        return bool(mHandle);
    }

    auto operator co_await() && -> task::TaskAwaiter<T> {
        return task::TaskAwaiter<T>(_leak());
    }
private:
    Task(handle_type handle) : mHandle(handle) {}

    handle_type mHandle;
friend class task::TaskPromise<T>;
};

// Spawn...
/**
 * @brief The handle of an spawned task
 * 
 */
class StopHandle {
public:
    StopHandle() = default;
    StopHandle(std::nullptr_t) {}

    // Request the stop of the task
    auto stop() const { return mPtr->stop(); }
    auto isStopped() const { return mPtr->isStopped(); }
protected:
    std::shared_ptr<task::TaskSpawnContext> mPtr;
};

template <typename T>
class WaitHandle {
public:
    WaitHandle() = default;
    WaitHandle(std::nullptr_t) {}

    auto stop() const { return mPtr->stop(); }
    auto isStopped() const { return mPtr->isStopped(); }

    // Blocking wait for the task to be done, nullopt on task stopped
    auto wait() && -> task::Option<T> { 
        auto ptr = std::exchange(mPtr, nullptr);
        ptr->enter();
        return ptr->value<T>();
    }
private:
    std::shared_ptr<task::TaskSpawnContext> mPtr;
template <typename U>
friend auto spawn(Task<U> task) -> WaitHandle<U>;
};

// Spawn a task running on the current thread executor
template <typename T>
inline auto spawn(Task<T> task) -> WaitHandle<T> {
    auto handle = WaitHandle<T>();
    handle.mPtr = task::TaskSpawnContext::make(task._leak());
    return handle;
}

// Sleep for a duration
inline auto sleep(std::chrono::milliseconds duration) -> Task<void> {
    return runtime::Executor::currentThread()->sleep(duration.count());
}

ILIAS_NS_END

// Interop with std...
template <typename T>
struct std::hash<ILIAS_NAMESPACE::task::TaskHandle<T> > {
    auto operator()(const ILIAS_NAMESPACE::task::TaskHandle<T> &handle) const -> std::size_t {
        return std::hash<ILIAS_NAMESPACE::runtime::CoroHandle>()(handle);
    }
};
