/**
 * @file spawn.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The spawn for the task system.
 * @version 0.1
 * @date 2026-01-23
 * 
 * @copyright Copyright (c) 2026
 * 
 */
#pragma once

#include <ilias/runtime/functional.hpp> // SmallFunction
#include <ilias/runtime/exception.hpp> // ExceptionPtr
#include <ilias/runtime/coro.hpp> // CoroPromise
#include <ilias/detail/intrusive.hpp> // List, Rc
#include <ilias/detail/option.hpp> // Option
#include <ilias/task/task.hpp> // TaskHandle
#include <concepts> // std::invocable
#include <new> // std::destroying_delete_t

ILIAS_NS_BEGIN

namespace task {

// Some containers
using intrusive::RefCounted;
using intrusive::ListNode;
using intrusive::List;
using intrusive::Rc;

// Runtime
using runtime::StopRegistration;
using runtime::SmallFunction;
using runtime::ExceptionPtr;
using runtime::CoroPromise;

// Environment for the spawn task common part
class TaskSpawnContextBase : public RefCounted<TaskSpawnContextBase>,
                             public ListNode<TaskSpawnContextBase>,
                             protected TaskContext
{
public:
    ILIAS_API
    TaskSpawnContextBase(TaskHandle<> task, CaptureSource source);
    TaskSpawnContextBase(const TaskSpawnContextBase &) = delete;

    // Send the stop request of the spawn task
    using TaskContext::stop;
    using TaskContext::isStopped;

    // Get the executor of this ctxt
    using TaskContext::executor;

    // Impl the virtual delete
    auto operator delete(TaskSpawnContextBase *self, std::destroying_delete_t) -> void {
        self->mManager(*self, Ops::Delete);
    }

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

    // Get the id of the spawn task
    auto id() const noexcept -> uintptr_t {
        return reinterpret_cast<uintptr_t>(this);
    }

    // Check the spawn task is completed
    auto isCompleted() const -> bool { return mCompleted; }

    // Set the handle when the task is completed
    auto setCompletionHandler(SmallFunction<void (TaskSpawnContextBase &)> handler) -> void {
        mCompletionHandler = handler;
    }

    // Set the handler when the task is completed, more convenient
    template <auto Method, typename Object>
    auto setCompletionHandler(Object *obj) -> void {
        mCompletionHandler = [obj](TaskSpawnContextBase &ctxt) {
            (obj->*Method)(ctxt);
        };
    }
protected:
    // Call on the task completed or stopped
    auto onComplete() -> void;

    SmallFunction<void (TaskSpawnContextBase &)> mCompletionHandler; // The completion handler, call when the task is completed or stopped
    ExceptionPtr mException; // The exception of the task
    bool mCompleted = false;

    // Virtual method
    enum Ops : uint8_t { Delete, SetValue };
    void (*mManager)(TaskSpawnContextBase &self, Ops) = nullptr;
};

// Environment for the spawn task<T>, store the return value
template <typename T>
class TaskSpawnContext final : public TaskSpawnContextBase {
public:
    TaskSpawnContext(TaskHandle<T> task, CaptureSource source) : TaskSpawnContextBase(task, source) {
        mManager = TaskSpawnContext::manager;
    }

    // Get the value of the task, nullopt if the task is stopped
    auto value() -> Option<T> {
        ILIAS_ASSERT(mCompleted, "??? INTERNAL BUG");
        mException.rethrowIfAny();
        return std::move(mValue);
    }

    // Allocate the spawn task context
    auto operator new(size_t size) -> void * {
        return runtime::allocate(size);
    }
protected:
    static auto manager(TaskSpawnContextBase &_self, Ops op) -> void {
        auto &self = static_cast<TaskSpawnContext &>(_self);
        switch (op) {
            case Ops::Delete: {
                self.~TaskSpawnContext(); 
                runtime::deallocate(&self, sizeof(self));
                break;
            }
            case Ops::SetValue: {
                ILIAS_ASSERT(self.isCompleted());
                auto handle = TaskHandle<T>::cast(self.mTask);
                if (self.isStopped()) {
                    return;
                }
                self.mException = handle.takeException();
                if (self.mException) {
                    return;
                }
                self.mValue = makeOption([&]() { return handle.value(); });
                return;
            }
        }
    }

    Option<T> mValue;
};

// The bridge for spawn task
class TaskSpawnAwaiterBase {
public:
    TaskSpawnAwaiterBase(Rc<TaskSpawnContextBase> ptr) : mCtxt(std::move(ptr)) {}

    auto await_ready() const noexcept -> bool { 
        return mCtxt->isCompleted();
    }

    auto await_suspend(CoroHandle caller) -> void {
        mHandle = caller;
        mCtxt->setCompletionHandler<&TaskSpawnAwaiterBase::onCompletion>(this);
        mReg.register_<&TaskSpawnAwaiterBase::onStopRequested>(caller.stopToken(), this); // Forward the stop request
    }
protected:
    auto onStopRequested() -> void {
        mCtxt->stop();
    }

    auto onCompletion(TaskSpawnContextBase &) -> void {
        if (mCtxt->isStopped() && mHandle.isStopRequested()) { // The target is stopped and the caller was requested to stop
            mHandle.setStopped();
            return; // Forward the stop
        }
        mHandle.schedule(); // We should resume the caller by ourself
    }

    Rc<TaskSpawnContextBase> mCtxt;
    StopRegistration mReg;
    CoroHandle mHandle;
};

// Awaiter for co_await WaitHandle<T>
template <typename T>
class TaskSpawnAwaiter final : public TaskSpawnAwaiterBase {
public:
    using TaskSpawnAwaiterBase::TaskSpawnAwaiterBase;

    auto await_resume() -> Option<T> {
        return static_cast<TaskSpawnContext<T> &>(*mCtxt).value();
    }
};

} // namespace task

/**
 * @brief The handle of an spawned task
 * 
 */
class StopHandle final {
public:
    StopHandle() = default;
    StopHandle(std::nullptr_t) {}
    StopHandle(StopHandle &&) = default;
    explicit StopHandle(task::Rc<task::TaskSpawnContextBase> ptr) : mPtr(std::move(ptr)) {}

    // Get the id of the task
    auto id() const noexcept {
        ILIAS_ASSERT(mPtr, "StopHandle is not valid");
        return mPtr->id(); 
    }

    // Request the stop of the task
    auto stop() const noexcept -> bool { 
        ILIAS_ASSERT(mPtr, "StopHandle is not valid");
        return mPtr->stop();
    }

    // Swap with other handle
    auto swap(StopHandle &other) noexcept -> void {
        return mPtr.swap(other.mPtr);
    }

    // Operator
    auto operator =(const StopHandle &) -> StopHandle & = delete;
    auto operator =(StopHandle &&other) -> StopHandle & = default;
    auto operator ==(const StopHandle &other) const noexcept -> bool = default;

    explicit operator bool() const noexcept {
        return bool(mPtr);
    }
protected:
    task::Rc<task::TaskSpawnContextBase> mPtr;
};

/**
 * @brief The wait handle of an spawned task (used for co_await or blcokingWait)
 * 
 * @tparam T 
 */
template <typename T>
class WaitHandle final {
public:
    WaitHandle() = default;
    WaitHandle(std::nullptr_t) {}
    WaitHandle(const WaitHandle &) = delete;
    WaitHandle(WaitHandle &&) = default;

    // Get the id of the task
    auto id() const noexcept {
        ILIAS_ASSERT(mPtr, "WaitHandle is not valid");
        return mPtr->id(); 
    }

    // Request the stop of the task
    auto stop() const noexcept -> bool { 
        ILIAS_ASSERT(mPtr, "WaitHandle is not valid");
        return mPtr->stop();
    }

    // Get the StopHandle from it
    auto stopHandle() const noexcept -> StopHandle {
        return StopHandle {mPtr};
    }

    // Blocking wait for the task to be done, nullopt on task stopped
    auto wait() -> Option<T> { 
        ILIAS_ASSERT(mPtr, "WaitHandle is not valid");
        auto ptr = std::exchange(mPtr, nullptr);
        auto ctxt = static_cast<task::TaskSpawnContext<T> *>(ptr.get());
        ctxt->enter();
        return ctxt->value();
    }

    // Get the internal context ptr
    auto _leak() -> task::Rc<task::TaskSpawnContextBase> {
        return std::exchange(mPtr, nullptr);
    }

    // Swap with other handle
    auto swap(WaitHandle &other) noexcept -> void {
        return mPtr.swap(other.mPtr);
    }

    // Await for the task to be done, return the Option<T>, nullopt on task stopped
    auto operator co_await() && -> task::TaskSpawnAwaiter<T> {
        ILIAS_ASSERT(mPtr, "WaitHandle is not valid");
        return { std::exchange(mPtr, nullptr) };
    }

    // Operator
    auto operator =(const WaitHandle &) -> WaitHandle & = delete;
    auto operator =(WaitHandle &&other) -> WaitHandle & = default;
    auto operator ==(const WaitHandle &other) const noexcept -> bool = default;

    // Convert to stop handle
    operator StopHandle() const noexcept {
        return stopHandle();
    }

    // Check the wait handle is valid
    explicit operator bool() const noexcept {
        return bool(mPtr);
    }
private:
    WaitHandle(task::TaskSpawnContextBase *ptr) : mPtr(ptr) {}

    task::Rc<task::TaskSpawnContextBase> mPtr;
template <Awaitable U>
friend auto spawn(U awaitale, runtime::CaptureSource source) -> WaitHandle<AwaitableResult<U> >;
};

/**
 * @brief Spawn a task by using given awaitable, running on the current thread executor
 * 
 * @tparam T 
 * @param awaitable The awaitable, (any awaitable object such as Task<T>, Fiber<T>)
 * @return WaitHandle<AwaitableResult<T> > 
 */
template <Awaitable T>
inline auto spawn(T awaitable, runtime::CaptureSource source = {}) -> WaitHandle<AwaitableResult<T> > {
    using U = AwaitableResult<T>;
    return {
        new task::TaskSpawnContext<U> { // Create the context
            toTask(std::move(awaitable))._leak(), // Convert the awaitable to task and get the handle
            source
        }
    };
}

/**
 * @brief Spawn a task by using given callable, running on the current thread executor
 * 
 * @param fn The callable (it should return a awaitable)
 * @return WaitHandle<AwaitableResult<std::invoke_result_t<Fn> > > 
 */
template <std::invocable Fn>
inline auto spawn(Fn fn, runtime::CaptureSource source = {}) -> WaitHandle<AwaitableResult<std::invoke_result_t<Fn> > > {
    if constexpr (std::is_function_v<Fn> || std::is_empty_v<Fn>) { // We didn't need to capture the function
        return spawn(fn(), source);
    }
    else {
        auto wrapper = [](auto fn) -> std::invoke_result_t<Fn> {
            co_return co_await fn();
        };
        return spawn(wrapper(std::move(fn)), source);
    }
}

/**
 * @brief Spawn a blocking task by using given callable, running on the threadpool
 * @note It doesn't support stop
 * 
 * @tparam Fn 
 * @param fn 
 * @return WaitHandle<typename std::invoke_result_t<Fn> > 
 */
template <std::invocable Fn>
inline auto spawnBlocking(Fn fn, runtime::CaptureSource source = {}) -> WaitHandle<typename std::invoke_result_t<Fn> > {
    return spawn(blocking(std::move(fn)), source);
}

// Special types for just spawn a task and forget about it, useful in callback or Qt slots
class FireAndForget final {
public:
    using promise_type = Task<void>::promise_type;

    FireAndForget(Task<void> task) { spawn(std::move(task)); }
};

ILIAS_NS_END