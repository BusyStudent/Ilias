/**
 * @file spawn.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Task spawn, wait, and cancel.
 * @version 0.1
 * @date 2024-09-30
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/detail/refptr.hpp>
#include <ilias/task/detail/view.hpp>
#include <ilias/task/task.hpp>
#include <concepts>

#define ilias_go    ILIAS_NAMESPACE::detail::SpawnTags { *ILIAS_NAMESPACE::Executor::currentThread() } << 
#define ilias_spawn ILIAS_NAMESPACE::detail::SpawnTags { co_await ILIAS_NAMESPACE::currentExecutor() } <<

ILIAS_NS_BEGIN

namespace detail {

struct SpawnData {
    SpawnData() {}
    SpawnData(const SpawnData &) = delete;
    ~SpawnData() {
        ILIAS_ASSERT(mTask.isSafeToDestroy());
        mTask.destroy();
    }
    
    auto ref() noexcept {
        ++mRefcount;
    }
    
    auto deref() noexcept {
        if (--mRefcount != 0) {
            return;
        }
        // Do cleanup
        if (mTask.done()) {
            destroy();
            return;
        }
        // Watch when the task done, then destroy
        mTask.registerCallback(compeleteCallback, this);
    }

    auto destroy() -> void {
        if (mDeleteSelf) {
            mDeleteSelf(this);
        }
        else {
            delete this;
        }
    }

    static auto compeleteCallback(void *self) -> void {
        auto &executor = static_cast<SpawnData *>(self)->mTask.executor();
        executor.post(destroyCallback, self);
    }

    /**
     * @brief Invoked on the event loop to destroy the self.
     * 
     * @param self 
     */
    static auto destroyCallback(void *self) -> void {
        static_cast<SpawnData *>(self)->destroy();
    }

    CancellationToken mToken;
    TaskView<> mTask;
    uint32_t mRefcount = 0;
    void (*mDeleteSelf)(SpawnData *) = nullptr;
};

template <typename T>
struct SpawnDataWithCallable final : SpawnData {
    template <typename ...Args>
    SpawnDataWithCallable(T callable, Args &&...args) : mCallable(std::move(callable)) {
        mDeleteSelf = &deleteSelf;
        mTask = std::invoke(mCallable, std::forward<Args>(args)...)._leak();
    }
    static auto deleteSelf(SpawnData *self) -> void {
        delete static_cast<SpawnDataWithCallable<T> *>(self);
    }
    
    T mCallable;
};

using SpawnDataRef = RefPtr<SpawnData>;

/**
 * @brief Awaiter for Wait Handle
 * 
 * @tparam T 
 */
template <typename T>
class WaitHandleAwaiter {
public:
    WaitHandleAwaiter(TaskView<T> task) : mTask(task) { }

    auto await_ready() const -> bool { return mTask.done(); }

    auto await_suspend(TaskView<> caller) -> void {
        mTask.setCancellationToken(caller.cancellationToken()); //< Let the caller's cancel request cancel the current task
        mTask.setAwaitingCoroutine(caller); //< When the task is done, resume the caller
    }

    auto await_resume() const -> T {
        return mTask.value();
    }
private:
    TaskView<T> mTask;
};

/**
 * @brief The common part of the task awaiter that will schedule the task on another executor
 * 
 */
class ScheduleOnAwaiterBase {
public:
    auto await_ready() const noexcept -> bool { return false; }

    auto await_suspend(TaskView<> caller) noexcept {
        mCaller = caller;
        mTask.registerCallback(onComplete, this);
        mTask.setCancellationToken(mToken);
        mTask.schedule();
        mReg = mCaller.cancellationToken().register_(onCancel, this);
    }
private:
    static auto onComplete(void *_self) -> void { 
        // May in different thread, use schedule to post to the executor
        auto self = static_cast<ScheduleOnAwaiterBase *>(_self);
        self->mCaller.schedule();
    }
    
    static auto onCancel(void *_self) -> void {
        auto  self = static_cast<ScheduleOnAwaiterBase *>(_self);
        auto &executor = self->mTask.executor();
        auto  task = self->mTask;
        executor.post(cancelTheTokenHelper, &task.cancellationToken());
    }
protected:
    ScheduleOnAwaiterBase(Executor &executor, TaskView<> task) : mTask(task) {
        mTask.setExecutor(executor);
    }

    CancellationToken::Registration mReg;
    CancellationToken mToken; //< The token for task
    TaskView<> mTask; //< The target task want to executed on another executor
    TaskView<> mCaller; //< The caller task
};

/**
 * @brief The awaiter for await the task scheduled on a another executor
 * 
 * @tparam T 
 */
template <typename T>
class ScheduleOnAwaiter final : public ScheduleOnAwaiterBase {
public:
    ScheduleOnAwaiter(Executor &executor, TaskView<T> task) : ScheduleOnAwaiterBase(executor, task) { }

    auto await_resume() const -> T {
        return TaskView<T>::cast(mTask).value();
    }
};

/**
 * @brief Helper tags for ilias_go and ilias_spawn macro
 * 
 */
struct SpawnTags {
    Executor &executor;
};

} // namespace detail

/**
 * @brief The handle for a spawned task. used to cancel the task. copyable.
 * 
 */
class CancelHandle {
public:
    explicit CancelHandle(const detail::SpawnDataRef &data) : 
        mData(data) { }

    CancelHandle() = default;
    CancelHandle(std::nullptr_t) { }
    CancelHandle(const CancelHandle &) = default;
    CancelHandle(CancelHandle &&) = default;

    auto done() const -> bool { return mData->mTask.done(); }
    auto cancel() const -> void { return mData->mTask.cancel(); }
    auto operator =(const CancelHandle &) -> CancelHandle & = default;
    auto operator <=>(const CancelHandle &) const noexcept = default;

    /**
     * @brief Check the CancelHandle is valid.
     * 
     * @return true 
     * @return false 
     */
    operator bool() const { return bool(mData); }
private:
    detail::SpawnDataRef mData;
template <typename T>
friend class WaitHandle;
};

/**
 * @brief The handle for a spawned task. used to wait for the task to complete. or cancel, it is unique. and moveon
 * 
 * @tparam T 
 */
template <typename T = void>
class WaitHandle {
public:
    explicit WaitHandle(const detail::SpawnDataRef &data) : 
        mData(data) { }

    WaitHandle() = default;
    WaitHandle(const WaitHandle &) = delete;
    WaitHandle(WaitHandle &&) = default;

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
            mData->mTask.executor().run(token);
        }
        auto data = std::move(mData);
        return TaskView<T>::cast(data->mTask).value();
    }

    auto operator =(const WaitHandle &) = delete;
    auto operator =(WaitHandle &&) -> WaitHandle & = default;
    auto operator <=>(const WaitHandle &) const noexcept = default;

#if defined(ILIAS_TASK_TRACE)
    // As same as Task
    auto _trace(CoroHandle caller) const -> void {
        caller.traceLink(mData->mTask);
    }
#endif // defined(ILIAS_TASK_TRACE)

    /**
     * @brief co-await the handle to complete. and return the result.
     * 
     * @return co_await 
     */
    auto operator co_await() && {
        ILIAS_ASSERT_MSG(mData, "Can not await an invalid handle");
        return detail::WaitHandleAwaiter<T>{TaskView<T>::cast(mData->mTask)};
    }

    /**
     * @brief Check if the handle is valid.
     * 
     * @return true 
     * @return false 
     */
    operator bool() const { return bool(mData); }

    /**
     * @brief implicitly convert to CancelHandle
     * 
     * @return CancelHandle 
     */
    operator CancelHandle() const { return CancelHandle(mData); }
private:
    detail::SpawnDataRef mData;
};

/**
 * @brief Spawn a task and return a handle.
 * 
 * @tparam T 
 * @param executor The executor to schedule the task on
 * @param task The task to spawn
 * @return WaitHandle<T> The handle to wait for the task to complete
 */
template <typename T>
inline auto spawn(Executor &executor, Task<T> &&task, ILIAS_CAPTURE_CALLER(loc)) -> WaitHandle<T> {
    auto ref = detail::SpawnDataRef(new detail::SpawnData);
    ref->mTask = task._leak();
    ref->mTask.setCancellationToken(ref->mToken);
    ref->mTask.setExecutor(executor);
    ref->mTask.schedule(); //< Start it on the event loop

#if defined(ILIAS_TASK_TRACE)
    detail::installTraceFrame(ref->mTask, "spawn");
#endif // defined(ILIAS_TASK_TRACE)

    return WaitHandle<T>(ref);
}

/**
 * @brief Spawn a task from a callable and args
 * 
 * @tparam Callable The invokeable type
 * @tparam Args The arguments to pass to the callable
 * 
 * @param executor The executor to schedule the task on
 * @param callable The callable to invoke, invoke_result must be a Task<T>
 * @param args The arguments to pass to the callable
 * @return WaitHandle<T> The handle to wait for the task to complete
 */
template <typename Callable, typename ...Args> 
    requires (detail::TaskGenerator<Callable, Args...>)
inline auto spawn(Executor &executor, Callable callable, Args &&...args) {
    // Normal function or empty class
    if constexpr (std::is_empty_v<Callable> || 
                  std::is_function_v<Callable> || 
                  std::is_member_function_pointer_v<Callable>) 
    {
        return spawn(executor, std::invoke(std::forward<Callable>(callable), std::forward<Args>(args)...));
    }
    else {
        // We need to create a class that hold the callable
        auto ref = detail::SpawnDataRef(
            new detail::SpawnDataWithCallable<Callable>(
                std::forward<Callable>(callable), 
                std::forward<Args>(args)...
            )
        );
        ref->mTask.setCancellationToken(ref->mToken);
        ref->mTask.setExecutor(executor);
        ref->mTask.schedule(); //< Start it on the event loop

#if defined(ILIAS_TASK_TRACE)
    detail::installTraceFrame(ref->mTask, "spawn");
#endif // defined(ILIAS_TASK_TRACE)

        return WaitHandle<typename std::invoke_result_t<Callable, Args...>::value_type>(ref);
    }
}

/**
 * @brief Spawn a task by using current thread executor and return a handle to wait for it to complete.
 * 
 * @tparam T 
 * @param task The task to spawn
 * @return WaitHandle<T> 
 */
template <typename T>
inline auto spawn(Task<T> &&task) -> WaitHandle<T> {
    return spawn(*Executor::currentThread(), std::move(task));
}

/**
 * @brief Spawn a task from a callable and args by using current thread executor and return a handle to wait for it to complete.
 * 
 * @tparam Callable 
 * @tparam Args 
 * 
 * @param callable The callable to invoke, invoke_result must be a Task<T>
 * @param args The arguments to pass to the callable
 * @return WaitHandle<T> The handle to wait for the task to complete
 */
template <typename Callable, typename ...Args>
    requires (detail::TaskGenerator<Callable, Args...>)
inline auto spawn(Callable callable, Args &&...args) {
    return spawn(*Executor::currentThread(), std::forward<Callable>(callable), std::forward<Args>(args)...);
}

/**
 * @brief Let the task schedule on another executor
 * 
 * @tparam T 
 * @param executor 
 * @param task 
 * @return auto 
 */
template <typename T>
inline auto scheduleOn(Executor &executor, Task<T> &&task) noexcept {
    return detail::ScheduleOnAwaiter<T>(executor, task._view());
}

/**
 * @brief Helper for ilias_go macro and ilias_spawn macro
 * 
 * @tparam Args 
 * @param args 
 * @return auto 
 */
template <typename ...Args>
inline auto operator <<(detail::SpawnTags tags, Args &&...args) {
    return spawn(tags.executor, std::forward<Args>(args)...);
}

ILIAS_NS_END