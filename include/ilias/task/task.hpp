/**
 * @file task.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The task class, provide the coroutine support
 * @version 0.1
 * @date 2024-08-09
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/task/detail/promise.hpp>
#include <ilias/task/detail/view.hpp>
#include <ilias/task/detail/wait.hpp>
#include <ilias/task/executor.hpp>
#include <ilias/log.hpp>
#include <coroutine>
#include <chrono>

ILIAS_NS_BEGIN

namespace detail {

/**
 * @brief The common part of the task awaiter
 * 
 */
class TaskAwaiterBase {
public:
    auto await_ready() const noexcept -> bool { return false; }

    /**
     * @brief Try to start the task and check if it is done
     * 
     * @return true 
     * @return false 
     */
    auto await_suspend(CoroHandle caller) noexcept -> bool {
        mTask.setExecutor(caller.executor());
        mTask.resume();
        if (mTask.done()) { //< Done, resume the caller
            return false;
        }

        mTask.setAwaitingCoroutine(caller); //< When the task is done, resume the caller
        mReg = caller.cancellationToken().register_( //< Forward the caller's cancel request cancel to the current task
            &cancelTheTokenHelper, &mTask.cancellationToken()
        );
        return true;
    }
protected:
    TaskAwaiterBase(TaskView<> task) : mTask(task) { }

    CancellationToken::Registration mReg; //< The reg of we wait for cancel
    TaskView<> mTask; //< The task we wait for
};

/**
 * @brief The awaiter of the task
 * 
 * @tparam T 
 */
template <typename T>
class TaskAwaiter final : public TaskAwaiterBase {
public:
    TaskAwaiter(TaskView<T> task) : TaskAwaiterBase(task) { }

    /**
     * @brief Get the result of the task
     * 
     * @return T>
     */
    auto await_resume() const -> T {
        return TaskView<T>::cast(mTask).value();
    }
};

/**
 * @brief Check if callable and args can be used to create a task.
 * 
 * @tparam Callable 
 * @tparam Args 
 */
template <typename Callable, typename ...Args>
concept TaskGenerator = requires(Callable &&callable, Args &&...args) {
    std::invoke(callable, args...)._view();
    std::invoke(callable, args...)._leak();
};

} // namespace detail


/**
 * @brief The lazy task class, it take the ownership of the coroutine
 * 
 * @tparam T The return type of the task (default: void)
 */
template <typename T>
class Task {
public:
    using promise_type = detail::TaskPromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;
    using value_type = T;

    /**
     * @brief Construct a new empty Task object
     * 
     */
    Task() = default;

    /**
     * @brief Construct a new empty Task object
     * 
     */
    Task(std::nullptr_t) { }

    /**
     * @brief Construct a new Task object, disable copy
     * 
     */
    Task(const Task &) = delete;

    /**
     * @brief Construct a new Task object by move
     * 
     * @param other 
     */
    Task(Task &&other) : mHandle(other._leak()) { }

    /**
     * @brief Construct a new Task object by any awaitable, the result of the awaitable must be convertible to T
     * 
     * @tparam U The 
     * @param awaitable 
     */
    template <typename U> requires(
        Awaitable<U> &&
        std::convertible_to<AwaitableResult<U>, T>
    )
    explicit Task(U &&awaitable) {
        mHandle = detail::awaitableWrapper<U, T>(std::forward<U>(awaitable))._leak();
    }

    /**
     * @brief Destroy the Task object, destroy the coroutine
     * 
     */
    ~Task() noexcept { 
        if (!mHandle) {
            return;
        }
        ILIAS_ASSERT(_view().isSafeToDestroy());
        mHandle.destroy();
    }

    /**
     * @brief Run the task and block until the task is done, return the value
     * 
     * @return T 
     */
    auto wait() const -> T {
        auto executor = Executor::currentThread();
        auto &promise = mHandle.promise();
        ILIAS_ASSERT(!promise.isStarted());
        promise.setExecutor(executor);
        mHandle.resume(); //< Start it
        if (!mHandle.done()) {
            CancellationToken token;
            promise.registerCallback(detail::cancelTheTokenHelper, &token);
            executor->run(token);
        }
        return promise.value();
    }

    /**
     * @brief Set the task's cancel policy
     * 
     * @param policy 
     */
    auto setCancelPolicy(CancelPolicy policy) -> void {
        return _view().setCancelPolicy(policy);
    }

    /**
     * @brief Get the task's internal view, it provides a super set api of the coroutine handle and task
     * 
     * @internal not recommend to use it at the outside
     * @return TaskView<T> 
     */
    auto _view() const -> TaskView<T> {
        return TaskView<T>(mHandle);
    }

    /**
     * @brief Release the ownership of the task, return the coroutine handle
     * 
     * @internal not recommend to use it at the outside
     * @return handle_type 
     */
    auto _leak() -> handle_type {
        return std::exchange(mHandle, nullptr);
    }


    /**
     * @brief Assign the task by move
     * 
     * @param other 
     * @return Task& 
     */
    auto operator =(Task<T> &&other) -> Task & {
        if (&other == this) {
            return *this;
        }
        if (mHandle) {
            ILIAS_ASSERT(_view().isSafeToDestroy());
            mHandle.destroy();
        }
        mHandle = other._leak();
        return *this;
    }

    /**
     * @brief Check the task is valid
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept {
        return bool(mHandle);
    }

    /**
     * @brief Get the awaiter of the task
     * 
     * @tparam T 
     * @param task 
     * @return detail::TaskAwaiter<T> 
     */
    auto operator co_await() && -> detail::TaskAwaiter<T> {
        return detail::TaskAwaiter<T>(mHandle);
    }
private:
    Task(handle_type handle) : mHandle(handle) { }

    handle_type mHandle;
friend class detail::TaskPromise<T>;
};

/**
 * @brief Sleep the current task for a period of time
 * 
 * @param ms 
 * @return IoTask<void> 
 */
inline auto sleep(std::chrono::milliseconds ms) -> IoTask<void> {
    return Executor::currentThread()->sleep(ms.count());
}

/**
 * @brief Suspend the current task, and queue self to the executor
 * 
 * @return Awaiter 
 */
inline auto yield() noexcept {
    struct Awaiter {
        auto await_ready() { return false; }
        auto await_suspend(TaskView<> task) { task.schedule(); }
        auto await_resume() { }
    };
    return Awaiter {};
}

/**
 * @brief Get the current task view
 * 
 * @return TaskView<>
 */
inline auto currentTask() noexcept {
    struct Awaiter {
        auto await_ready() { return false; }
        auto await_suspend(TaskView<> task) -> bool { mTask = task; return false; }
        auto await_resume() -> TaskView<> { return mTask; }
        TaskView<> mTask;
    };
    return Awaiter {};
}

/**
 * @brief Get the current executor in the task
 * 
 * @return Executor &
 */
inline auto currentExecutor() noexcept {
    struct Awaiter {
        auto await_ready() { return false; }
        auto await_suspend(TaskView<> task) -> bool { mExecutor = task.executor(); return false; }
        auto await_resume() -> Executor & { ILIAS_ASSERT(mExecutor); return *mExecutor; }
        Executor *mExecutor;
    };
    return Awaiter {};
}

ILIAS_NS_END