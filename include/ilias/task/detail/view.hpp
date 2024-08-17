/**
 * @file view.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Impl the TaskView<T> class, it is a view of the task
 * @version 0.1
 * @date 2024-08-09
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/task/detail/promise.hpp>
#include <ilias/cancellation_token.hpp>

ILIAS_NS_BEGIN

namespace detail {
    struct TaskViewNull { };

    template <typename T>
    concept IsTaskPromise = std::is_base_of_v<TaskPromiseBase, T>;    
}

template <typename T = detail::TaskViewNull>
class TaskView;

/**
 * @brief The type erased TaskView class, it can observe the task, 
 *  it is a superset of std::coroutine_handle<> and has interface to access the task's promise field
 * 
 */
template <>
class TaskView<detail::TaskViewNull> {
public:
    TaskView() = default;
    TaskView(std::nullptr_t) { }

    /**
     * @brief Construct a TaskView from a Task's coroutine handle
     * 
     * @tparam T 
     * @param handle 
     */
    template <detail::IsTaskPromise T>
    TaskView(std::coroutine_handle<T> handle) : mPromise(&handle.promise()), mHandle(handle) { }

    /**
     * @brief Check if the task is done
     * 
     * @return bool 
     */
    auto done() const noexcept { return mHandle.done(); }

    /**
     * @brief Resume the task
     * 
     */
    auto resume() const noexcept { return mHandle.resume(); }

    /**
     * @brief Destroy the task
     * 
     * @return auto 
     */
    auto destroy() const noexcept { ILIAS_ASSERT(isSafeToDestroy()); return mHandle.destroy(); }

    /**
     * @brief Send a cancel request to the task
     * 
     */
    auto cancel() const { return cancellationToken().cancel(); }

    /**
     * @brief Get the cancellation token of the task
     * 
     * @return CancellationToken& 
     */
    auto cancellationToken() const -> CancellationToken & { return mPromise->cancellationToken(); }

    /**
     * @brief Check if the task is cancelled
     * 
     * @return bool
     */
    auto isCancelled() const noexcept -> bool { return cancellationToken().isCancelled(); }

    /**
     * @brief Check if the task is started
     * 
     * @return true 
     * @return false 
     */
    auto isStarted() const noexcept -> bool { return mPromise->isStarted(); }

    /**
     * @brief Check the coroutine is safe to destroy
     * 
     * @return bool 
     */
    auto isSafeToDestroy() const noexcept -> bool { return !isStarted() || done(); }

    /**
     * @brief Get the executor of the task
     * 
     * @return Executor* 
     */
    auto executor() const noexcept -> Executor * { return mPromise->executor(); }

    /**
     * @brief Set the Awaiting Coroutine object, the task will resume it when self is done
     * 
     * @param handle 
     * @return auto 
     */
    auto setAwaitingCoroutine(std::coroutine_handle<> handle) const noexcept -> void { return mPromise->setAwaitingCoroutine(handle); }

    /**
     * @brief Register a callback function to be called when the task is done
     * 
     * @param fn 
     * @param data 
     * @return auto 
     */
    auto registerCallback(void (*fn)(void *), void *data) const noexcept -> void { return mPromise->registerCallback(fn, data); }

    /**
     * @brief Cast to std::coroutine_handle<>
     * 
     * @return std::coroutine_handle<> 
     */
    operator std::coroutine_handle<>() const noexcept {
        return mHandle;
    }
protected:
    detail::TaskPromiseBase *mPromise = nullptr;
    std::coroutine_handle<>  mHandle = nullptr;
};

/**
 * @brief The TaskView<T> class, it can observe the task
 * 
 * @tparam T 
 */
template <typename T>
class TaskView final : public TaskView<> {
public:
    using promise_type = detail::TaskPromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;

    TaskView() = default;
    TaskView(std::nullptr_t) { }

    /**
     * @brief Construct a new Task View object from Task<T>'s handle
     * 
     * @param handle 
     */
    TaskView(handle_type handle) : TaskView<>(handle) { }

    /**
     * @brief Get the handle of the Task
     * 
     * @return handle_type 
     */
    auto handle() const -> handle_type {
        return handle_type::from_address(this->mHandle.address());
    }

    /**
     * @brief Get the return value of the task
     * 
     * @return Result<T> 
     */
    auto value() const -> Result<T> {
        return static_cast<promise_type *>(mPromise)->value();
    }

    /**
     * @brief Cast to handle_type
     * 
     * @return handle_type 
     */
    operator handle_type() const {
        return handle();
    }
};

ILIAS_NS_END