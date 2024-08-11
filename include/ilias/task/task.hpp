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
#include <ilias/task/executor.hpp>
#include <ilias/log.hpp>
#include <coroutine>

ILIAS_NS_BEGIN

namespace detail {

inline auto CancelTheToken(void *token) -> void {
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
            &CancelTheToken, &mTask.cancellationToken()
        );
    }

    auto await_resume() const -> Result<T> {
        return mTask.value();
    }
private:
    CancellationToken::Registration mReg; //< The reg of we wait for cancel
    TaskView<T> mTask; //< The task we want to await
};

}


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
        if (mHandle) {
            mHandle.destroy(); 
        }
    }

    /**
     * @brief Check the task is done
     * 
     * @return true 
     * @return false 
     */
    auto done() const noexcept -> bool {
        return mHandle.done();
    }

    /**
     * @brief Get the task's cancellation token
     * 
     * @return CancellationToken& 
     */
    auto cancellationToken() const -> CancellationToken & {
        return mHandle.promise().cancellationToken();
    }

    /**
     * @brief Cancel current task
     * 
     */
    auto cancel() const -> void {
        return cancellationToken().cancel();
    }

    /**
     * @brief Check the task is canceled?
     * 
     * @return true 
     * @return false 
     */
    auto isCanceled() const -> bool {
        return cancellationToken().isCanceled();
    }

    /**
     * @brief Get the coroutine handle
     * 
     * @return handle_type 
     */
    auto handle() const -> handle_type {
        return mHandle;
    }

    /**
     * @brief Run the task and block until the task is done, return the value
     * 
     * @return Result<T> 
     */
    auto wait() const -> Result<T> {
        auto &promise = mHandle.promise();
        auto executor = promise.executor();
        mHandle.resume(); //< Run it
        if (!mHandle.done()) {
            CancellationToken token;
            promise.registerCallback(detail::CancelTheToken, &token);
            executor->run(token);
        }
        return promise.value();
    }

    /**
     * @brief Get the task's view, it provides a super set api of the task
     * 
     * @return TaskView<T> 
     */
    auto view() const -> TaskView<T> {
        return TaskView<T>(mHandle);
    }

    /**
     * @brief Assign the task by move
     * 
     * @param other 
     * @return Task& 
     */
    auto operator = (Task<T> &&other) -> Task & {
        if (&other == this) {
            return *this;
        }
        if (mHandle) {
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

    /**
     * @brief Cast the task to TaskView
     * 
     * @return TaskView<T> 
     */
    operator TaskView<T>() const noexcept {
        return view();
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
auto operator co_await(Task<T> &&task) -> detail::TaskAwaiter<T> {
    return detail::TaskAwaiter(task.view());
}

ILIAS_NS_END