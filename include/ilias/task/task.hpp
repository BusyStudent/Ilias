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

inline auto CancelTheTokenHelper(void *token) -> void {
    static_cast<CancellationToken*>(token)->cancel();
}

/**
 * @brief The awaiter of the task
 * 
 * @tparam T 
 */
template <typename T>
class TaskAwaiter {
public:
    TaskAwaiter(TaskView<T> task) : mTask(task) { }

    auto await_ready() const noexcept -> bool {
        mTask.resume();
        return mTask.done();
    }

    auto await_suspend(TaskView<> caller) -> void {
        mTask.setAwaitingCoroutine(caller); //< When the task is done, resume the caller
        mReg = caller.cancellationToken().register_( //< Let the caller's cancel request cancel the current task
            &CancelTheTokenHelper, &mTask.cancellationToken()
        );
    }

    auto await_resume() const -> Result<T> {
        return mTask.value();
    }
private:
    CancellationToken::Registration mReg; //< The reg of we wait for cancel
    TaskView<T> mTask; //< The task we want to await
};

} // namespace detail


/**
 * @brief The lazy task class, it take the ownership of the coroutine
 * 
 * @tparam T 
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
    Task(Task &&other) : mHandle(other.mHandle) {
        other.mHandle = nullptr;
    }

    /**
     * @brief Destroy the Task object, destroy the coroutine
     * 
     */
    ~Task() { 
        if (!mHandle) {
            return;
        }
        ILIAS_ASSERT(_view().isSafeToDestroy());
        mHandle.destroy();
    }

    /**
     * @brief Run the task and block until the task is done, return the value
     * 
     * @return Result<T> 
     */
    auto wait() const -> Result<T> {
        auto &promise = mHandle.promise();
        auto executor = promise.executor();
        ILIAS_ASSERT(!promise.isStarted());
        mHandle.resume(); //< Start it
        if (!mHandle.done()) {
            CancellationToken token;
            promise.registerCallback(detail::CancelTheTokenHelper, &token);
            executor->run(token);
        }
        return promise.value();
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
        auto h = mHandle;
        mHandle = nullptr;
        return h;
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
        mHandle = other.mHandle;
        other.mHandle = nullptr;
        return *this;
    }

    /**
     * @brief Check the task is valid
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept {
        return mHandle;
    }
private:
    Task(handle_type handle) : mHandle(handle) { }

    handle_type mHandle;
friend class detail::TaskPromise<T>;
};

/**
 * @brief Get the awaiter of the task
 * 
 * @tparam T 
 * @param task 
 * @return detail::TaskAwaiter<T> 
 */
template <typename T>
inline auto operator co_await(Task<T> &&task) -> detail::TaskAwaiter<T> {
    return detail::TaskAwaiter(task._view());
}

/**
 * @brief Sleep the current task for a period of time
 * 
 * @param ms 
 * @return Task<void> 
 */
inline auto sleep(std::chrono::milliseconds ms) -> Task<void> {
    return Executor::currentThread()->sleep(ms.count());
}

/**
 * @brief Suspend the current task, and queue self to the executor
 * 
 * @return Awaiter 
 */
inline auto yield() {
    struct Awaiter {
        auto await_ready() { return false; }
        auto await_suspend(TaskView<> task) { task.schedule(); }
        auto await_resume() { }
    };
    return Awaiter {};
}

ILIAS_NS_END