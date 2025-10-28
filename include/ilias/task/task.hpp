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

#include <ilias/runtime/functional.hpp> // SmallFunction
#include <ilias/runtime/executor.hpp> // Executor
#include <ilias/runtime/token.hpp> // StopToken
#include <ilias/runtime/await.hpp> // Awaitable
#include <ilias/runtime/coro.hpp> // CoroPromise
#include <ilias/detail/intrusive.hpp> // List, Rc
#include <ilias/detail/option.hpp> // Option
#include <ilias/log.hpp>
#include <coroutine>
#include <chrono> // std::chrono::duration

ILIAS_NS_BEGIN

namespace task {

// Some containers
using intrusive::RefCounted;
using intrusive::Node;
using intrusive::List;
using intrusive::Rc;

// Runtime
using runtime::StopSource;
using runtime::StopRegistration;
using runtime::CoroHandle;
using runtime::CoroPromise;
using runtime::CoroContext;
using runtime::SmallFunction;
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
class TaskSpawnContext final : public RefCounted<TaskSpawnContext>,
                               public Node<TaskSpawnContext>,
                               private TaskContext
{
public:
    TaskSpawnContext(TaskHandle<> task) : TaskContext(task) {
        auto executor = runtime::Executor::currentThread();
        ILIAS_ASSERT_MSG(executor, "The current thread has no executor");

        mTask.setCompletionHandler(TaskSpawnContext::onComplete);
        this->setStoppedHandler(TaskSpawnContext::onComplete);
        this->setExecutor(*executor);

        this->ref(); // Ref it, we will deref it when it completed
        mTask.schedule(); // Schedule the task in the executor
    }
    TaskSpawnContext(const TaskSpawnContext &) = delete;

    // Send the stop request of the spawn task
    using TaskContext::stop;

    // Get the executor of this ctxt
    using TaskContext::executor;

    // Expose the memory
    using TaskContext::operator new;
    using TaskContext::operator delete;

    // Blocking enter the executor
    auto enter() -> void {
        if (!mCompleted) {
            StopSource source;
            mCompletionHandler = [&](auto &) {
                source.request_stop();
            };
            executor().run(source.get_token());
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

    // Get the id of the spawn task
    auto id() const noexcept -> uintptr_t {
        return reinterpret_cast<uintptr_t>(this);
    }

    // Check the spawn task is completed
    auto isCompleted() const -> bool { return mCompleted; }

    // Set the handle when the task is completed
    auto setCompletionHandler(SmallFunction<void (TaskSpawnContext &)> handler) -> void {
        mCompletionHandler = handler;
    }

    // Set the handler when the task is completed, more convenient
    template <auto Method, typename Object>
    auto setCompletionHandler(Object *obj) -> void {
        mCompletionHandler = [obj](TaskSpawnContext &ctxt) {
            (obj->*Method)(ctxt);
        };
    }
private:
    static auto onComplete(CoroContext &_self) -> void { 
        auto &self = static_cast<TaskSpawnContext &>(_self);

        // Done..
        self.mCompleted = true;
        if (self.mCompletionHandler) { // Notify we are stopped
            self.mCompletionHandler(self);
            self.mCompletionHandler = nullptr;
        }
        if (self.use_count() == 1) { // We are the last one, only can be deref in the event loop
            self.executor().post(TaskSpawnContext::derefSelf, &self);
        }
        else { // For avoid the derefSelf call after quit, we can deref the self
            self.deref();
        }
    }

    // deref self in event loop
    static auto derefSelf(void *_self) -> void {
        auto ptr = static_cast<TaskSpawnContext *>(_self);
        ptr->deref();
    }

    SmallFunction<void (TaskSpawnContext &)> mCompletionHandler; // The completion handler, call when the task is completed or stopped
    std::string mName; // The name of the spawn task
    bool mCompleted = false;
friend class TaskSpawnAwaiterBase;
};

class TaskSpawnAwaiterBase {
public:
    TaskSpawnAwaiterBase(Rc<TaskSpawnContext> ptr) : mCtxt(ptr) {}

    auto await_ready() const noexcept { return mCtxt->isCompleted(); }
    auto await_suspend(CoroHandle caller) {
        mHandle = caller;
        mCtxt->setCompletionHandler<&TaskSpawnAwaiterBase::onCompletion>(this);
        mReg.register_<&TaskSpawnAwaiterBase::onStopRequested>(caller.stopToken(), this); // Forward the stop request
    }
protected:
    auto onStopRequested() -> void {
        mCtxt->stop();
    }

    auto onCompletion(TaskSpawnContext &) -> void {
        if (mCtxt->isStopped() && mHandle.isStopRequested()) { // The target is stopped and the caller was requested to stop
            mHandle.setStopped();
            return; // Forward the stop
        }
        if (mCtxt->isStopped()) {
            mHandle.schedule(); // We should resume the caller by ourself
            return;
        }
        // Let the mTask resume the caller
        mCtxt->mTask.setPrevAwaiting(mHandle);
    }

    Rc<TaskSpawnContext> mCtxt;
    StopRegistration mReg;
    CoroHandle mHandle;
};

// Awaiter for co_await WaitHandle<T>
template <typename T>
class TaskSpawnAwaiter final : public TaskSpawnAwaiterBase {
public:
    using TaskSpawnAwaiterBase::TaskSpawnAwaiterBase;

    auto await_resume() -> Option<T> {
        return mCtxt->value<T>();
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
struct BlockingWaitTags {};

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
    auto wait() -> T {
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
class StopHandle final {
public:
    StopHandle() = default;
    StopHandle(std::nullptr_t) {}
    StopHandle(const StopHandle &) = default;
    StopHandle(StopHandle &&) = default;
    explicit StopHandle(task::Rc<task::TaskSpawnContext> ptr) : mPtr(std::move(ptr)) {}

    // Request the stop of the task
    auto id() const { return mPtr->id(); }
    auto stop() const { return mPtr->stop(); }

    auto operator =(const StopHandle &) -> StopHandle & = delete;
    auto operator =(StopHandle &&other) -> StopHandle & = default;

    explicit operator bool() const noexcept {
        return bool(mPtr);
    }
protected:
    task::Rc<task::TaskSpawnContext> mPtr;
};

template <typename T>
class WaitHandle final {
public:
    WaitHandle() = default;
    WaitHandle(std::nullptr_t) {}
    WaitHandle(const WaitHandle &) = delete;
    WaitHandle(WaitHandle &&) = default;

    auto stop() const { return mPtr->stop(); }

    // Blocking wait for the task to be done, nullopt on task stopped
    auto wait() -> Option<T> { 
        auto ptr = std::exchange(mPtr, nullptr);
        ptr->enter();
        return ptr->value<T>();
    }

    // Get the internal context ptr
    auto _leak() -> task::Rc<task::TaskSpawnContext> {
        return std::exchange(mPtr, nullptr);
    }

    // Await for the task to be done, return the Option<T>, nullopt on task stopped
    auto operator co_await() && -> task::TaskSpawnAwaiter<T> {
        return { std::exchange(mPtr, nullptr) };
    }

    auto operator =(const WaitHandle &) -> WaitHandle & = delete;
    auto operator =(WaitHandle &&other) -> WaitHandle & = default;

    // Convert to stop handle
    operator StopHandle() {
        return StopHandle {mPtr};
    }

    // Check the wait handle is valid
    explicit operator bool() const noexcept {
        return bool(mPtr);
    }
private:
    task::Rc<task::TaskSpawnContext> mPtr;
template <typename U>
friend auto spawn(Task<U> task) -> WaitHandle<U>;
};

// Spawn a task running on the current thread executor
template <typename T>
inline auto spawn(Task<T> task) -> WaitHandle<T> {
    auto handle = WaitHandle<T>();
    handle.mPtr = task::Rc<task::TaskSpawnContext>::make(task._leak());
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
inline auto blockingWait(T awaitable) -> AwaitableResult<T> {
    return toTask(std::move(awaitable)).wait();
}

inline auto blockingWait() -> task::BlockingWaitTags {
    return {};
}

// Concepts
template <typename T>
concept IntoTask = requires (T t) {
    toTask(std::forward<T>(t));
};

// Tags invoke
template <Awaitable T>
inline auto operator |(T awaitable, task::ToTaskTags) {
    return toTask(std::move(awaitable));
}

template <Awaitable T>
inline auto operator |(T awaitable, task::BlockingWaitTags) {
    return blockingWait(std::move(awaitable));
}

ILIAS_NS_END

// Interop with std...
template <typename T>
struct std::hash<ILIAS_NAMESPACE::task::TaskHandle<T> > {
    auto operator()(const ILIAS_NAMESPACE::task::TaskHandle<T> &handle) const -> std::size_t {
        return std::hash<ILIAS_NAMESPACE::runtime::CoroHandle>()(handle);
    }
};
