#pragma once

#include <ilias/task/detail/promise.hpp>
#include <ilias/task/detail/view.hpp>
#include <ilias/task/executor.hpp>
#include <coroutine>
#include <optional>
#include <vector>

#if defined(__cpp_exceptions)
    #include <exception>
#endif

ILIAS_NS_BEGIN

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
    auto handle() const noexcept -> handle_type {
        return mHandle;
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



ILIAS_NS_END