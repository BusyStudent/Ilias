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
using intrusive::Node;
using intrusive::List;
using intrusive::Rc;

// Runtime
using runtime::StopRegistration;
using runtime::SmallFunction;
using runtime::CoroPromise;

// Environment for the spawn task common part
class TaskSpawnContextBase : public RefCounted<TaskSpawnContextBase>,
                             public Node<TaskSpawnContextBase>,
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

    // Expose the memory
    using TaskContext::operator new;
    using TaskContext::operator delete;
    
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
    std::exception_ptr mException; // The exception of the task
    std::string mName; // The name of the spawn task
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
        if (mException) {
            std::rethrow_exception(std::exchange(mException, nullptr));
        }
        return std::move(mValue);
    }
protected:
    static auto manager(TaskSpawnContextBase &_self, Ops op) -> void {
        auto &self = static_cast<TaskSpawnContext &>(_self);
        switch (op) {
            case Ops::Delete: {
                self.~TaskSpawnContext(); 
                TaskContext::operator delete(&self, sizeof(self));
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
    StopHandle(const StopHandle &) = default;
    StopHandle(StopHandle &&) = default;
    explicit StopHandle(task::Rc<task::TaskSpawnContextBase> ptr) : mPtr(std::move(ptr)) {}

    // Request the stop of the task
    auto id() const { return mPtr->id(); }
    auto stop() const { return mPtr->stop(); }

    // Swap with other handle
    auto swap(StopHandle &other) -> void {
        return mPtr.swap(other.mPtr);
    }

    auto operator =(const StopHandle &) -> StopHandle & = delete;
    auto operator =(StopHandle &&other) -> StopHandle & = default;

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

    auto stop() const { return mPtr->stop(); }

    // Blocking wait for the task to be done, nullopt on task stopped
    auto wait() -> Option<T> { 
        ILIAS_ASSERT(mPtr, "WaitHandle is not valid");
        auto ptr = std::exchange(mPtr, nullptr);
        ptr->enter();
        return static_cast<task::TaskSpawnContext<T> &>(*ptr).value();
    }

    // Get the internal context ptr
    auto _leak() -> task::Rc<task::TaskSpawnContextBase> {
        return std::exchange(mPtr, nullptr);
    }

    // Swap with other handle
    auto swap(WaitHandle &other) -> void {
        return mPtr.swap(other.mPtr);
    }

    // Await for the task to be done, return the Option<T>, nullopt on task stopped
    auto operator co_await() && -> task::TaskSpawnAwaiter<T> {
        ILIAS_ASSERT(mPtr, "WaitHandle is not valid");
        return { std::exchange(mPtr, nullptr) };
    }

    auto operator =(const WaitHandle &) -> WaitHandle & = delete;
    auto operator =(WaitHandle &&other) -> WaitHandle & = default;

    // Convert to stop handle
    operator StopHandle() const noexcept {
        return StopHandle {mPtr};
    }

    // Check the wait handle is valid
    explicit operator bool() const noexcept {
        return bool(mPtr);
    }
private:
    task::Rc<task::TaskSpawnContextBase> mPtr;
template <typename U>
friend auto spawn(Task<U> task, runtime::CaptureSource source) -> WaitHandle<U>;
};

// Spawn a task running on the current thread executor
template <typename T>
inline auto spawn(Task<T> task, runtime::CaptureSource source = {}) -> WaitHandle<T> {
    ILIAS_ASSERT(task, "Task is null");
    auto handle = WaitHandle<T> {};
    auto ptr = new task::TaskSpawnContext<T>(task._leak(), source);
    handle.mPtr.reset(ptr);
    return handle;
}

// Spawn a task by using given callable
template <std::invocable Fn>
inline auto spawn(Fn fn, runtime::CaptureSource source = {}) -> WaitHandle<typename std::invoke_result_t<Fn>::value_type> {
    if constexpr (std::is_function_v<Fn> || std::is_empty_v<Fn>) { // We didn't need to capture the function
        return spawn(fn(), source);
    }
    else {
        auto wrapper = [](auto fn) -> std::invoke_result_t<Fn> {
            co_return co_await fn();
        };
        return spawn(wrapper(fn), source);
    }
}

// Spawn a blocking task by using given callable, it doesn't support stop
template <std::invocable Fn>
inline auto spawnBlocking(Fn fn, runtime::CaptureSource source = {}) -> WaitHandle<typename std::invoke_result_t<Fn> > {
    return spawn([](auto fn) -> Task<typename std::invoke_result_t<Fn> > {
        co_return co_await task::TaskBlockingAwaiter<decltype(fn)>(std::move(fn));
    }(std::forward<Fn>(fn)), source);
}

// Special types for just spawn a task and forget about it, useful in callback or Qt slots
class FireAndForget final {
public:
    using promise_type = Task<void>::promise_type;

    FireAndForget(Task<void> task) { spawn(std::move(task)); }
};

ILIAS_NS_END