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

#include <ilias/runtime/executor.hpp>
#include <ilias/runtime/token.hpp>
#include <ilias/runtime/await.hpp>
#include <ilias/runtime/coro.hpp>
#include <ilias/detail/option.hpp> // Option
#include <ilias/log.hpp>
#include <coroutine>
#include <chrono> // std::chrono::duration

ILIAS_NS_BEGIN

namespace task {

using runtime::StopSource;
using runtime::StopRegistration;
using runtime::CoroHandle;
using runtime::CoroPromise;
using runtime::CoroContext;

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

// Environment for the task, it bind the task with this
class TaskContext : public CoroContext {
public:
    TaskContext(TaskHandle<> task, std::nostopstate_t) : CoroContext(std::nostopstate), mTask(task) {
        mTask.setContext(*this);
    }
    TaskContext(TaskHandle<> task) : mTask(task) {
        mTask.setContext(*this);
    }
    TaskContext(std::nullptr_t, std::nostopstate_t) : CoroContext(std::nostopstate) {
        
    }
    TaskContext(std::nullptr_t) {

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
protected:
    TaskHandle<> mTask; // The task we use to wait for (take ownership)
};

// Environment for the blocking wait task
class TaskBlockingContext final : public TaskContext {
public:
    TaskBlockingContext(TaskHandle<> task) : TaskContext(task, std::nostopstate) {
        auto executor = runtime::Executor::currentThread();
        ILIAS_ASSERT_MSG(executor, "The current thread has no executor");

        mTask.setCompletionHandler(TaskBlockingContext::onComplete);
        this->setExecutor(*executor);
    }
    TaskBlockingContext(const TaskBlockingContext &) = delete;

    auto enter() -> void {
        mTask.resume();
        if (!mTask.done()) {
            executor().run(mStopExecutor.get_token());            
        }
        ILIAS_ASSERT_MSG(mTask.done(), "??? INTERNAL BUG");
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

// Environment for the spawn task
class TaskSpawnContext final : public TaskContext {
public:
    TaskSpawnContext(TaskHandle<> task) : TaskContext(task) {
        auto executor = runtime::Executor::currentThread();
        ILIAS_ASSERT_MSG(executor, "The current thread has no executor");

        mTask.setCompletionHandler(TaskSpawnContext::onComplete);
        this->setStoppedHandler(TaskSpawnContext::onComplete);
        this->setExecutor(*executor);
    }
    TaskSpawnContext(const TaskSpawnContext &) = delete;

    // Blocking enter the executor
    auto enter() -> void {
        if (!mCompleted) {
            StopSource source;
            mCompletionHandler = [](auto, void *source) {
                static_cast<StopSource *>(source)->request_stop();
            };
            mUser = &source;
            this->executor().run(source.get_token());
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

    auto id() const noexcept -> uintptr_t {
        return reinterpret_cast<uintptr_t>(this);
    }

    // Check the spawn task is completed
    auto isCompleted() const -> bool { return mCompleted; }

    // Set the handle when the task is completed
    auto setCompletionHandler(void (*handler)(TaskSpawnContext &, void *), void *user) -> void {
        mCompletionHandler = handler;
        mUser = user;
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
        if (self.mCompletionHandler) { // Notify we are stopped
            self.mCompletionHandler(self, self.mUser);
            self.mCompletionHandler = nullptr;
            self.mUser = nullptr;
        }
        if (self.mSelf.use_count() == 1) { // We are the last one, only can be deref in the event loop
            self.executor().post(TaskSpawnContext::derefSelf, &self);
        }
        else { // For avoid the derefSelf call after quit, we can reset the self
            self.mSelf.reset();
        }
    }

    // deref self in event loop
    static auto derefSelf(void *_self) -> void {
        auto ptr = std::move(static_cast<TaskSpawnContext *>(_self)->mSelf);
        ptr.reset();
    }

    std::shared_ptr<TaskSpawnContext> mSelf; // Used to avoid free self dur execution
    std::string mName; // The name of the spawn task
    void (*mCompletionHandler)(TaskSpawnContext &ctxt, void *) = nullptr; // The completion handler, call when the task is completed or stopped
    void  *mUser = nullptr;
    bool   mCompleted = false;
friend class TaskSpawnAwaiterBase;
};

class TaskSpawnAwaiterBase {
public:
    TaskSpawnAwaiterBase(std::shared_ptr<TaskSpawnContext> ptr) : mPtr(ptr) {}

    auto await_ready() const noexcept { return mPtr->mCompleted; }
    auto await_suspend(CoroHandle caller) {
        mPtr->mCompletionHandler = TaskSpawnAwaiterBase::onCompletion;
        mPtr->mUser = this;
        mHandle = caller;
        mReg.register_(caller.stopToken(), onStopRequested, this); // Forward the stop request
    }
protected:
    static auto onCompletion(TaskSpawnContext &, void *_self) -> void {
        auto &self = *static_cast<TaskSpawnAwaiterBase *>(_self);
        if (self.mPtr->isStopped() && self.mHandle.isStopRequested()) { // The target is stopped and the caller was requested to stop
            self.mHandle.setStopped();
            return; // Forward the stop
        }
        if (self.mPtr->isStopped()) {
            self.mHandle.schedule(); // We should resume the caller by ourself
            return;
        }
        // Let the mTask resume the caller
        self.mPtr->mTask.setPrevAwaiting(self.mHandle);
    }
    static auto onStopRequested(void *_self) -> void {
        auto &self = *static_cast<TaskSpawnAwaiterBase *>(_self);
        self.mPtr->stop();
    }

    std::shared_ptr<TaskSpawnContext> mPtr;
    StopRegistration mReg;
    CoroHandle mHandle;
};

// Awaiter for co_await WaitHandle<T>
template <typename T>
class TaskSpawnAwaiter final : public TaskSpawnAwaiterBase {
public:
    using TaskSpawnAwaiterBase::TaskSpawnAwaiterBase;

    auto await_resume() -> Option<T> {
        return mPtr->value<T>();
    }
};

// Awaiter for spawnBlocking && blocking
template <std::invocable Fn>
class TaskSpawnBlockingAwaiter final : public runtime::CallableImpl<TaskSpawnBlockingAwaiter<Fn> > {
public:
    using T = std::invoke_result_t<Fn>;

    TaskSpawnBlockingAwaiter(Fn fn) : mFn(std::move(fn)) {}
    TaskSpawnBlockingAwaiter(TaskSpawnBlockingAwaiter &&) = default;

    auto await_ready() const noexcept { return false; }
    auto await_suspend(CoroHandle caller) {
        mHandle = caller;
        runtime::threadpool::submit(this);
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

} // namespace task

// Re-export this for the user
namespace this_coro = runtime::context;

/**
 * @brief The lazy task class, it take the ownership of the coroutine
 * 
 * @tparam T The return type of the task (default: void)
 */
template <typename T>
class [[nodiscard]] Task {
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
        auto handle = task::TaskHandle<T>(mHandle);
        handle.setContext(context);
    }

    /**
     * @brief Run the task and block until the task is done, return the value
     * 
     * @return T 
     */
    auto wait() && -> T {
        auto context = task::TaskBlockingContext(_leak());
        context.enter();
        return context.value<T>();
    }

    auto operator =(Task<T> &&other) noexcept -> Task & {
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

    auto operator co_await() && noexcept -> task::TaskAwaiter<T> {
        return task::TaskAwaiter<T>(_leak());
    }
private:
    Task(handle_type handle) noexcept : mHandle(handle) {}

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
    StopHandle(const StopHandle &) = default;
    StopHandle(StopHandle &&) = default;
    explicit StopHandle(std::shared_ptr<task::TaskSpawnContext> ptr) : mPtr(std::move(ptr)) {}

    // Request the stop of the task
    auto id() const { return mPtr->id(); }
    auto stop() const { return mPtr->stop(); }

    explicit operator bool() const noexcept {
        return bool(mPtr);
    }
protected:
    std::shared_ptr<task::TaskSpawnContext> mPtr;
};

template <typename T>
class WaitHandle {
public:
    WaitHandle() = default;
    WaitHandle(std::nullptr_t) {}
    WaitHandle(const WaitHandle &) = delete;
    WaitHandle(WaitHandle &&) = default;

    auto stop() const { return mPtr->stop(); }

    // Blocking wait for the task to be done, nullopt on task stopped
    auto wait() && -> Option<T> { 
        auto ptr = std::exchange(mPtr, nullptr);
        ptr->enter();
        return ptr->value<T>();
    }

    // Get the internal context ptr
    auto _leak() && -> std::shared_ptr<task::TaskSpawnContext> {
        return std::exchange(mPtr, nullptr);
    }

    // Await for the task to be done, return the Option<T>, nullopt on task stopped
    auto operator co_await() && -> task::TaskSpawnAwaiter<T> {
        return { std::exchange(mPtr, nullptr) };
    }

    explicit operator bool() const noexcept {
        return bool(mPtr);
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

// Spawn a task by using given callable
template <std::invocable Fn>
inline auto spawn(Fn fn) -> WaitHandle<typename std::invoke_result_t<Fn>::value_type> {
    if constexpr (std::is_function_v<Fn> || std::is_empty_v<Fn>) { // We didn't need to capture the function
        return spawn(fn());
    }
    else {
        auto wrapper = [](auto fn) -> std::invoke_result_t<Fn> {
            co_return co_await fn();
        };
        return spawn(wrapper(fn));
    }
}

// Spawn a blocking task by using given callable, it doesn't support stop
template <std::invocable Fn>
inline auto spawnBlocking(Fn fn) -> WaitHandle<typename std::invoke_result_t<Fn> > {
    return spawn([](auto fn) -> Task<typename std::invoke_result_t<Fn> > {
        co_return co_await task::TaskSpawnBlockingAwaiter<decltype(fn)>(std::move(fn));
    }(std::forward<Fn>(fn)));
}

// Awaiting a blocking task by using given callable
template <std::invocable Fn>
[[nodiscard]]
inline auto blocking(Fn fn) {
    return task::TaskSpawnBlockingAwaiter<decltype(fn)>(std::move(fn));
}

// Sleep for a duration
[[nodiscard]]
inline auto sleep(std::chrono::milliseconds duration) -> Task<void> {
    return runtime::Executor::currentThread()->sleep(duration.count());
}

// Abstraction for awaitable
template <Awaitable T>
inline auto toTask(T awaitable) -> Task<AwaitableResult<T> > {
    co_return co_await awaitable;
}

template <typename T>
inline auto toTask(Task<T> task) -> Task<T> {
    return task;
}

template <typename T>
concept IntoTask = requires (T t) {
    toTask(std::forward<T>(t));
};

ILIAS_NS_END

// Interop with std...
template <typename T>
struct std::hash<ILIAS_NAMESPACE::task::TaskHandle<T> > {
    auto operator()(const ILIAS_NAMESPACE::task::TaskHandle<T> &handle) const -> std::size_t {
        return std::hash<ILIAS_NAMESPACE::runtime::CoroHandle>()(handle);
    }
};
