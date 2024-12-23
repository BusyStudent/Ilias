/**
 * @file scope.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The task scope. It is used to manage the tasks that are running in the scope.
 * @version 0.1
 * @date 2024-10-02
 * 
 * @copyright Copyright (c) 2024
 * 
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

namespace detail {

/**
 * @brief The instance of the task spawned in the scope.
 * 
 */
struct ScopedTask {
    ScopedTask(TaskView<> task, std::list<ScopedTask*> &list) : 
        mTask(task), mList(&list), 
        mIt(list.insert(list.end(), this)) 
    {

    }

    ScopedTask(const ScopedTask &) = delete;

    ~ScopedTask() {
        ILIAS_ASSERT(!mList); //< It should be unlinked before destruction.
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

    TaskView<> mTask; //< The task instance.
    std::list<ScopedTask*> *mList; //< The list that the task is in.
    std::list<ScopedTask*>::iterator mIt; //< The iterator of the task in the manage list.
    int mRefcount = 1; //< The reference count of the task.
};

using ScopedTaskPtr = RefPtr<ScopedTask>;

} // namespace detail

/**
 * @brief Handle for observeing the spawned task in the scope.
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
 * @brief Wait Handle for TaskScope, only movable.
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
     * @brief Blocking Wait for the task to complete. and return the result.
     * 
     * @return T
     */
    auto wait() -> T {
        ILIAS_ASSERT_MSG(mData, "Can not wait for an invalid handle");
        if (!done()) {
            // Wait until done
            CancellationToken token;
            mData->mTask.registerCallback(detail::cancelTheTokenHelper, &token);
            mData->mTask.executor()->run(token);
        }
        auto data = std::move(mData);
        return TaskView<T>::cast(data->mTask).value();
    }

    auto operator =(ScopedWaitHandle &&) -> ScopedWaitHandle & = default;
    auto operator =(const ScopedWaitHandle &) -> ScopedWaitHandle & = delete;
    auto operator <=>(const ScopedWaitHandle &) const = default;

    /**
     * @brief co-await the handle to complete. and return the result.
     * 
     * @return co_await 
     */
    auto operator co_await() && {
        // using the impl in task/spawn.hpp
        ILIAS_ASSERT_MSG(mData, "Can not await an invalid handle");
        return detail::WaitHandleAwaiter<T>{TaskView<T>::cast(mData->mTask)};
    }

    /**
     * @brief Check this handle is valid.
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
 * @brief The task scope. user can spawn tasks in the scope and the scope will wait all the tasks when it is destroyed.
 * 
 */
class TaskScope {
public:
    template <typename T = void>
    using WaitHandle = ScopedWaitHandle<T>;
    using CancelHandle = ScopedCancelHandle;

    TaskScope() = default;
    TaskScope(const TaskScope &) = delete;
    ~TaskScope() {
        if (mAutoCancel) {
            cancel();
        }
        wait();
    }

    /**
     * @brief Blocking current thread until all the tasks in the scope are finished.
     * 
     */
    auto wait() -> void {
        if (mInstances.empty()) {
            return; //< Nothing to wait.
        }
        CancellationToken token;
        mWaitToken = &token;
        mExecutor->run(token);
        ILIAS_ASSERT(mInstances.empty());
    }

    /**
     * @brief Send the cancel request to all the tasks in the scope.
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
     * @brief Spawn a task in the scope. note, The result of the task will be discarded.
     * 
     * @tparam T 
     * @param task The task to spawn. (cannot be null)
     */
    template <typename T>
    auto spawn(Task<T> &&task) -> WaitHandle<T> {
        ILIAS_ASSERT(task);
        auto handle = task._leak();
        auto instance = new detail::ScopedTask(handle, mInstances);
        // Start and add the complete callback.
        instance->mTask.registerCallback(std::bind(&TaskScope::onTaskComplete, this, instance));
        instance->mTask.setExecutor(mExecutor);
        instance->mTask.schedule();
        ILIAS_TRACE("TaskScope", "Spawn a task {} in the scope.", (void*) instance);
        return WaitHandle<T>(instance);
    }

    /**
     * @brief Spawn a task in the scope. the callable version.
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
            // Oh, this callable has member, we should store it.
            using TaskType = std::invoke_result_t<Callable, Args...>;
            return spawn([](Callable callable, Args ...args) -> TaskType {
                co_return co_await std::invoke(std::forward<Callable>(callable), std::forward<Args>(args)...);
            }(std::forward<Callable>(callable), std::forward<Args>(args)...));
        }

    }

    /**
     * @brief Set the scope should auto cancel the tasks when it is destroyed.
     * 
     * @param autoCancel 
     */
    auto setAutoCancel(bool autoCancel) -> void {
        mAutoCancel = autoCancel;
    }

    /**
     * @brief Check whether the scope cancel the tasks when it is destroyed.
     * 
     * @return true 
     * @return false 
     */
    auto autoCancel() const -> bool {
        return mAutoCancel;
    }
private:
    /**
     * @brief The callback for each task, we do management in this callback.
     * 
     * @param instance 
     */
    auto onTaskComplete(detail::ScopedTask *instance) -> void {
        ILIAS_TRACE("TaskScope", "Task {} is finished.", (void*) instance);
        ILIAS_ASSERT(!mInCancel); //< Cannot be in canceling. ill-formed state.
        instance->unlink();
        mExecutor->post([](void *instance) {
            static_cast<detail::ScopedTask*>(instance)->deref();
        }, instance);
        if (mInstances.empty() && mWaitToken) { //< Notify the wait operation.
            mExecutor->post([](void *token) { 
                //< We defer the notify to avoid the memory leak. the delete of the instance does not execute in the same thread.
                ILIAS_TRACE("TaskScope", "All tasks are finished, notify the wait operation.");
                static_cast<CancellationToken*>(token)->cancel();
            }, mWaitToken);
            mWaitToken = nullptr;
        }
    }

    std::list<detail::ScopedTask*> mInstances; //< The list of the tasks that are running in the scope.
    Executor *mExecutor = Executor::currentThread(); //< The executor that the scope use.
    bool mInCancel = false; //< Whether the scope is in canceling.
    bool mAutoCancel = true; //< Whether to cancel the tasks when the scope is destroyed.
    CancellationToken *mWaitToken = nullptr; //< The token for the wait operation.
};

ILIAS_NS_END