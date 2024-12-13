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
}

/**
 * @brief The cancellation policy for the task
 * 
 */
enum class CancelPolicy {
    Once,         // Cancels only the current await point; subsequent co_await operations proceed normally (default)
    Persistent,   // Cancellation persists; all subsequent co_await operations will receive cancellation notifications
};

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
    auto resume() const noexcept { 
        ILIAS_ASSERT(executor()); 
        return mHandle.resume();
    }

    /**
     * @brief Schedule the task in the executor (thread safe)
     * @note Do not call this function if the task is already scheduled
     * 
     * @return auto 
     */
    auto schedule() const noexcept { 
        ILIAS_ASSERT(executor()); 
        return executor()->post(scheduleImpl, mHandle.address());
    }

    /**
     * @brief Destroy the task
     * 
     * @return auto 
     */
    auto destroy() const noexcept { 
        ILIAS_ASSERT(isSafeToDestroy()); 
        return mHandle.destroy(); 
    }

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
     */
    auto setAwaitingCoroutine(std::coroutine_handle<> handle) const noexcept -> void { 
        return mPromise->setAwaitingCoroutine(handle); 
    }

    /**
     * @brief Set the Cancel Policy object, the task will follow this policy when self is cancelled
     * 
     * @param policy 
     */
    auto setCancelPolicy(CancelPolicy policy) const noexcept -> void { 
        return cancellationToken().setAutoReset(policy == CancelPolicy::Once); 
    }

    /**
     * @brief Set the Executor object, the task will use this executor to schedule itself
     * 
     * @param executor 
     */
    auto setExecutor(Executor *executor) const noexcept -> void { 
        return mPromise->setExecutor(executor); 
    }

    /**
     * @brief Register a callback function to be called when the task is done
     * 
     * @param fn 
     * @param data 
     * @return auto 
     */
    auto registerCallback(void (*fn)(void *), void *data) const noexcept -> void { 
        return mPromise->registerCallback(fn, data); 
    }

    /**
     * @brief Register a callback function to be called when the task is done
     * 
     * @param fn 
     */
    auto registerCallback(detail::MoveOnlyFunction<void()> fn) const noexcept -> void { 
        return mPromise->registerCallback(std::move(fn)); 
    }

    /**
     * @brief Allow comparison with other TaskView<> objects
     * 
     */
    auto operator <=>(const TaskView<> &other) const noexcept = default;

    /**
     * @brief Cast to std::coroutine_handle<>
     * 
     * @return std::coroutine_handle<> 
     */
    operator std::coroutine_handle<>() const noexcept {
        return mHandle;
    }

    /**
     * @brief Check if the view is valid
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept { return bool(mHandle); }
protected:
    /**
     * @brief The callback function to schedule a coroutine
     * 
     * @param ptr 
     */
    static auto scheduleImpl(void *ptr) -> void {
        std::coroutine_handle<>::from_address(ptr).resume();
    }

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
        return handle_type::from_address(mHandle.address());
    }

    /**
     * @brief Get the return value of the task
     * 
     * @return T
     */
    auto value() const -> T {
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

    /**
     * @brief Cast from TaskView<> (note it is dangerous)
     * 
     * @param view 
     * @return TaskView<T> 
     */
    static auto cast(TaskView<detail::TaskViewNull> view) {
        auto handle = std::coroutine_handle<>(view);
        return TaskView<T>(handle_type::from_address(handle.address()));
    }
};

ILIAS_NS_END

#if !defined(ILIAS_NO_FORMAT)
ILIAS_FORMATTER(TaskView<>) {
    auto format(const auto &view, auto &ctxt) const {
        return format_to(ctxt.out(), "TaskView<{}>", std::coroutine_handle<>(view).address());
    }
};
#endif