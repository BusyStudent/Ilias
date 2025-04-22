/**
 * @file scope.hpp
 * @author BusyStudent
 * @brief Manages tasks within a scope.
 * @version 0.1
 * @date 2024-10-02
 * 
 * @copyright Copyright (c) 2024
 */

#pragma once

#include <ilias/detail/functional.hpp>
#include <ilias/detail/refptr.hpp>
#include <ilias/task/executor.hpp>
#include <ilias/task/spawn.hpp>
#include <ilias/task/task.hpp>
#include <ilias/log.hpp>
#include <list>

ILIAS_NS_BEGIN

class TaskScope;

namespace detail {

/**
 * @brief Represents a task instance within a scope.
 * 
 */
struct ScopedTask {
    ScopedTask(TaskView<> task, std::list<ScopedTask*> &list) : 
        mTask(task), mList(&list), 
        mIt(list.insert(list.end(), this)) 
    {
        mTask.setCancellationToken(mToken);
    }

    ScopedTask(const ScopedTask &) = delete;

    ~ScopedTask() {
        ILIAS_ASSERT(!mList); //< Must be unlinked before destruction.
        mTask.destroy();
    }

    auto unlink() -> void {
        ILIAS_ASSERT(mList);
        mList->erase(mIt);
        mIt = mList->end();
        mList = nullptr;
    }

    auto ref() -> void {
        ++mRefcount;
    }

    auto deref() -> void {
        --mRefcount;
        if (mRefcount == 0) {
            delete this;
        }
    }

    TaskView<> mTask; //< Task instance.
    CancellationToken mToken;
    std::list<ScopedTask*> *mList; //< List containing the task.
    std::list<ScopedTask*>::iterator mIt; //< Iterator for the task in the list.
    int mRefcount = 1; //< Reference count.
};

/**
 * @brief Awaiter for the task scope to complete.
 * 
 */
class TaskScopeAwaiter {
public:
    TaskScopeAwaiter(TaskScope &scope) : mScope(scope) { }

    auto await_ready() const -> bool;
    auto await_suspend(CoroHandle caller) -> void;
    auto await_resume() const noexcept { }
private:
    static auto onNotify(void *self) -> void;

    TaskScope &mScope;
    CoroHandle mCaller;
    CancellationToken::Registration mReg;
};

using ScopedTaskPtr = RefPtr<ScopedTask>;

} // namespace detail

/**
 * @brief Handle for observing and canceling tasks within a scope.
 * 
 */
class ScopedCancelHandle {
public:
    explicit ScopedCancelHandle(detail::ScopedTask *ptr) : mData(ptr) {
        
    }

    ScopedCancelHandle() = default;
    ScopedCancelHandle(const ScopedCancelHandle &) = default;
    ScopedCancelHandle(ScopedCancelHandle &&) = default;

    auto done() const -> bool { return mData->mTask.done(); }
    auto cancel() const -> void { return mData->mTask.cancel(); }
    auto operator =(const ScopedCancelHandle &) -> ScopedCancelHandle & = default;
    auto operator <=>(const ScopedCancelHandle &) const = default;

    explicit operator bool() const { return bool(mData); }
private:
    detail::ScopedTaskPtr mData;
};

/**
 * @brief Wait handle for TaskScope, only movable.
 * 
 * @tparam T 
 */
template <typename T = void>
class ScopedWaitHandle {
public:
    explicit ScopedWaitHandle(detail::ScopedTask *ptr) : mData(ptr) {
        
    }

    ScopedWaitHandle() = default;
    ScopedWaitHandle(const ScopedWaitHandle &) = delete;
    ScopedWaitHandle(ScopedWaitHandle &&) = default;

    auto done() const -> bool { return mData->mTask.done(); }
    auto cancel() const -> void { return mData->mTask.cancel(); }

    /**
     * @brief Blocking wait for the task to complete and return the result.
     * 
     * @return T
     */
    auto wait() -> T {
        ILIAS_ASSERT_MSG(mData, "Cannot wait for an invalid handle");
        if (!done()) {
            // Wait until done
            CancellationToken token;
            mData->mTask.registerCallback(detail::cancelTheTokenHelper, &token);
            mData->mTask.executor().run(token);
        }
        auto data = std::move(mData);
        return TaskView<T>::cast(data->mTask).value();
    }

    auto operator =(ScopedWaitHandle &&) -> ScopedWaitHandle & = default;
    auto operator =(const ScopedWaitHandle &) -> ScopedWaitHandle & = delete;
    auto operator <=>(const ScopedWaitHandle &) const = default;

    /**
     * @brief co-await the handle to complete and return the result.
     * 
     * @return co_await 
     */
    auto operator co_await() && {
        // Using the implementation in task/spawn.hpp
        ILIAS_ASSERT_MSG(mData, "Cannot await an invalid handle");
        return detail::WaitHandleAwaiter<T>{TaskView<T>::cast(mData->mTask)};
    }

    /**
     * @brief Checks if the handle is valid.
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const { return bool(mData); }

    /**
     * @brief Auto convert to ScopedCancelHandle.
     * 
     * @return ScopedCancelHandle 
     */
    operator ScopedCancelHandle() const { return ScopedCancelHandle(mData); }
private:
    detail::ScopedTaskPtr mData;
};

/**
 * @brief Manages tasks within a scope. Waits for all tasks to complete upon destruction.
 * 
 */
class TaskScope {
public:
    template <typename T = void>
    using WaitHandle = ScopedWaitHandle<T>;
    using CancelHandle = ScopedCancelHandle;

    TaskScope() : mExecutor(*Executor::currentThread()) { }
    TaskScope(Executor &exec) : mExecutor(exec) { }
    TaskScope(const TaskScope &) = delete;
    ~TaskScope() {
        if (mAutoCancel) {
            cancel();
        }
        wait();
    }

#if !defined(NDEBUG)
    template <typename T = int> // No implmentation, make the language server happy. :(
    TaskScope(TaskScope &&, T = {}) {
        static_assert(!std::is_same_v<T, int>, "TaskScope cannot be moved");
    }
#endif // !defined(NDEBUG)

    /**
     * @brief Blocks the current thread until all tasks in the scope are finished.
     * 
     */
    auto wait() -> void {
        if (mInstances.empty()) {
            return; //< Nothing to wait for.
        }
        ILIAS_ASSERT(!isWaiting()); // Ensure it's not called twice. or blocking wait and async wait at the same time.
        CancellationToken token;
        mWaitCallback = &detail::cancelTheTokenHelper;
        mWaitData = &token;
        mExecutor.run(token);
        ILIAS_ASSERT(mInstances.empty());
    }

    /**
     * @brief Sends a cancel request to all tasks in the scope.
     * 
     */
    auto cancel() -> void {
        mInCancel = true;
        for (auto &instance : mInstances) {
            instance->mTask.cancel();
        }
        mInCancel = false;
    }

    /**
     * @brief Gets the number of tasks running in the scope.
     * 
     * @return size_t 
     */
    auto runningTasks() const -> size_t {
        return mInstances.size();
    }

    /**
     * @brief Checks if there is someone waiting for the scope to complete.
     * 
     * @return true 
     * @return false 
     */
    auto isWaiting() const -> bool {
        return mWaitCallback || mWaitData;
    }

    /**
     * @brief Spawns a task in the scope. The result of the task will be discarded.
     * 
     * @tparam T 
     * @param task The task to spawn (cannot be null).
     */
    template <typename T>
    auto spawn(Task<T> &&task, ILIAS_CAPTURE_CALLER(loc)) -> WaitHandle<T> {
        ILIAS_ASSERT(task);
        auto handle = task._leak();
        auto instance = new detail::ScopedTask(handle, mInstances);
        // Start and add the complete callback.
        instance->mTask.registerCallback(std::bind(&TaskScope::onTaskComplete, this, instance));
        instance->mTask.setExecutor(mExecutor);
        instance->mTask.schedule();
        ILIAS_TRACE("TaskScope", "Spawned a task {} in the scope.", (void*) instance);

#if defined(ILIAS_TASK_TRACE)
        detail::installTraceFrame(instance->mTask, "Scope::spawn", loc);
#endif // defined(ILIAS_TASK_TRACE)

        return WaitHandle<T>(instance);
    }

    /**
     * @brief Spawns a task in the scope using a callable.
     * 
     * @tparam Callable 
     * @tparam Args 
     * @param callable 
     * @param args 
     */
    template <typename Callable, typename ...Args> requires detail::TaskGenerator<Callable, Args...>
    auto spawn(Callable callable, Args &&...args) {
        // Normal function or empty class
        if constexpr (std::is_empty_v<Callable> || 
                    std::is_function_v<Callable> || 
                    std::is_member_function_pointer_v<Callable>) 
        {
            return spawn(std::invoke(std::forward<Callable>(callable), std::forward<Args>(args)...));
        }
        else {
            // Callable has members, store it.
            using TaskType = std::invoke_result_t<Callable, Args...>;
            return spawn([](Callable callable, Args ...args) -> TaskType {
                co_return co_await std::invoke(std::forward<Callable>(callable), std::forward<Args>(args)...);
            }(std::forward<Callable>(callable), std::forward<Args>(args)...));
        }

    }

    /**
     * @brief Sets whether the scope should auto-cancel tasks upon destruction.
     * 
     * @param autoCancel 
     */
    auto setAutoCancel(bool autoCancel) -> void {
        mAutoCancel = autoCancel;
    }

    /**
     * @brief Checks whether the scope auto-cancels tasks upon destruction.
     * 
     * @return true 
     * @return false 
     */
    auto autoCancel() const -> bool {
        return mAutoCancel;
    }

    /**
     * @brief co-await the scope to complete. if cancellation is requested, all the tasks in the scope will be canceled.
     * 
     * @return co_await 
     */
    auto operator co_await() noexcept {
        return detail::TaskScopeAwaiter(*this);
    }

    /**
     * @brief Create a task scope with current coroutine's executor.
     * 
     * @tparam T 
     * @return auto 
     */
    template <typename T = TaskScope>
    static auto make() {
        struct Awaiter : detail::GetHandleAwaiter {
            auto await_resume() -> T {
                return T(handle().executor()); // MUST NRVO
            }
        };
        return Awaiter{};
    }
private:
    /**
     * @brief Callback for task completion, used for management.
     * 
     * @param instance 
     */
    auto onTaskComplete(detail::ScopedTask *instance) -> void {
        ILIAS_TRACE("TaskScope", "Task {} finished.", (void*) instance);
        ILIAS_ASSERT(!mInCancel); // Cannot be in canceling state.
        instance->unlink();
        mExecutor.post([](void *instance) {
            static_cast<detail::ScopedTask*>(instance)->deref();
        }, instance); // Delete the instance in the executor.
        if (mInstances.empty() && mWaitCallback) { // Notify the wait operation.
            // Defer the notify to avoid memory leak and ensure the user doesn't quit the event loop before the task instance is deleted.
            mExecutor.post([](void *self) { 
                static_cast<TaskScope*>(self)->notifyWait();
            }, this);
        }
    }

    auto notifyWait() -> void {
        ILIAS_ASSERT(mWaitCallback); // Ensure it's not called twice or in the wrong state.
        ILIAS_TRACE("TaskScope", "All tasks finished, notifying wait operation.");
        auto callback = std::exchange(mWaitCallback, nullptr);
        auto data = std::exchange(mWaitData, nullptr);
        callback(data);
    }

    std::list<detail::ScopedTask*> mInstances; //< List of tasks running in the scope.
    Executor &mExecutor; //< Executor used by the scope.
    bool mInCancel = false; //< Indicates if the scope is canceling tasks.
    bool mAutoCancel = true; //< Indicates if tasks should be auto-canceled upon destruction.
    void (*mWaitCallback)(void *) = nullptr; //< Callback for wait operation.
    void  *mWaitData = nullptr; //< Data for the wait callback.
friend class detail::TaskScopeAwaiter;
};

inline auto detail::TaskScopeAwaiter::await_ready() const -> bool {
    return mScope.mInstances.empty();
}

inline auto detail::TaskScopeAwaiter::await_suspend(CoroHandle caller) -> void {
    ILIAS_ASSERT(!mScope.mWaitCallback && !mScope.mWaitData); // Ensure it's not called twice. or blocking wait and async wait at the same time.
    mCaller = caller;
    mScope.mWaitCallback = &detail::TaskScopeAwaiter::onNotify;
    mScope.mWaitData = this;
    mReg = caller.cancellationToken().register_(std::bind(&TaskScope::cancel, &mScope)); // Forwards the cancellation request to the scope.
}

inline auto detail::TaskScopeAwaiter::onNotify(void *self) -> void {
    auto &awaiter = *static_cast<TaskScopeAwaiter*>(self);
    awaiter.mCaller.resume();
}

ILIAS_NS_END