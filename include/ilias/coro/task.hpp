#pragma once

#include "../detail/expected.hpp"
#include "promise.hpp"
#include <coroutine>
#include <concepts>
#include <optional>

// Useful macros
#define ilias_go   ::ILIAS_NAMESPACE::EventLoop::instance() <<
#define ilias_wait ::ILIAS_NAMESPACE::EventLoop::instance() >>
#define ilias_spawn ::ILIAS_NAMESPACE::EventLoop::instance() <<

ILIAS_NS_BEGIN

template <typename T>
class TaskPromise;
template <typename T>
class AwaitRecorder;

/**
 * @brief Check a type is Task<T>
 * 
 * @tparam T 
 */
template <typename T>
concept IsTask = requires(T t) {
    t.handle();
    t.promise();
    t.cancel();  
};

/**
 * @brief Check this type can be directly pass to co_await
 * 
 * @tparam T 
 */
template <typename T>
concept Awaiter = requires(T t) {
    t.await_ready();
    t.await_resume();
};

/**
 * @brief Check the type should be passed to await_transform
 * 
 * @tparam T 
 */
template <typename T>
concept NotAwaiter = !Awaiter<T>;

/**
 * @brief A Lazy way to get value
 * 
 * @tparam T 
 */
template <typename T>
class Task {
public:
    using promise_type = TaskPromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;
    using result_type = Result<T>;
    using value_type = T;

    /**
     * @brief Construct a new Task object
     * 
     * @param h 
     */
    explicit Task(handle_type h) : mHandle(h) { }

    /**
     * @brief Construct a new Task object (deleted)
     * 
     * @param t 
     */
    Task(const Task &t) = delete;

    /**
     * @brief Construct a new Task object by move
     * 
     * @param t 
     */
    Task(Task &&t) : mHandle(t.mHandle) { t.mHandle = nullptr; }

    /**
     * @brief Construct a new empty Task object
     * 
     */
    Task() { }

    /**
     * @brief Destroy the Task object
     * 
     */
    ~Task() { clear(); }

    /**
     * @brief Release the ownship of the internal coroutine handle
     * 
     * @return handle_type 
     */
    auto leak() -> handle_type {
        auto h = mHandle;
        mHandle = nullptr;
        return h;
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
     * @brief Get the coroutine promise reference
     * 
     * @return promise_type& 
     */
    auto promise() const -> promise_type & {
        return mHandle.promise();
    }
    
    /**
     * @brief Get the name of the coroutine (for debug)
     * 
     * @return const char* 
     */
    auto name() const -> const char * {
        return mHandle.promise().name();   
    }

    /**
     * @brief Cancel the task
     * 
     * @return CancelStatus
     */
    auto cancel() const -> CancelStatus {
        return promise().cancel();
    }

    /**
     * @brief Get the task's event loop
     * 
     * @return EventLoop* 
     */
    auto eventLoop() const -> EventLoop * {
        return promise().eventLoop();
    }

    /**
     * @brief Get the result value
     * 
     * @return result_type 
     */
    auto value() const -> result_type {
        if (!mHandle.done()) {
            StopToken token;
            promise().setStopOnDone(&token);
            eventLoop()->resumeHandle(mHandle);
            eventLoop()->run(token);
        }
        return promise().value();
    }

    /**
     * @brief Clear the task
     * 
     */
    auto clear() -> void {
        if (!mHandle) {
            return;
        }
        if (!mHandle.done()) {
            // Cancel current
            auto status = cancel();
            ILIAS_ASSERT(status == CancelStatus::Done);
        }
        mHandle.destroy();
        mHandle = nullptr;
    }

    /**
     * @brief Assign a moved task
     * 
     * @param other 
     * @return Task& 
     */
    auto operator =(Task &&other) -> Task & {
        clear();
        mHandle = other.mHandle;
        other.mHandle = nullptr;
        return *this;
    }

    auto operator =(const Task &) -> Task & = delete;

    /**
     * @brief Access the promise directly
     * 
     * @return promise_type* 
     */
    auto operator ->() const -> promise_type * {
        return &(mHandle.promise());
    }

    /**
     * @brief Check the task is empty or not
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept {
        return bool(mHandle);
    }

    /**
     * @brief Create task by the callable and args
     * 
     * @tparam Callable 
     * @tparam Args 
     * @return auto 
     */
    template <typename Callable, typename ...Args>
    static auto fromCallable(Callable &&callable, Args &&...args);
private:
    handle_type mHandle;
};


/**
 * @brief A typed task promise
 * 
 * @tparam T 
 */
template <typename T>
class TaskPromise final : public PromiseBase {
public:
    using handle_type = std::coroutine_handle<TaskPromise<T> >;
    using value_type = Result<T>;
    using return_type = T;

    /**
     * @brief Construct a new Task Promise object
     * 
     */
    TaskPromise(ILIAS_CAPTURE_CALLER(name)) noexcept {
        mName = name.function_name();
    }

    ~TaskPromise() noexcept {
        ILIAS_CO_REMOVE(mHandle); //< Self is destroy, remove this
        ILIAS_ASSERT(!mException); //< If exception was no still in there, abort!
    }

    /**
     * @brief Transform user defined type
     * 
     * @tparam T must be NotAwaiter
     * @param t 
     * @return auto 
     */
    template <NotAwaiter U>
    auto await_transform(U &&t) noexcept {
        return AwaitRecorder {this, AwaitTransform<U>().transform(this, std::forward<U>(t))};
    }

    /**
     * @brief Passthrough Awaiter
     * 
     * @tparam T must be Awaiter
     * @param t 
     * @return T 
     */
    template <Awaiter U>
    auto await_transform(U &&t) noexcept {
        return AwaitRecorder<U&&> {this, std::forward<U>(t)};
    }

    /**
     * @brief Get the return object object
     * 
     * @return Task<T> 
     */
    auto get_return_object() -> Task<T> {
        auto handle = handle_type::from_promise(*this);
        mHandle = handle;
        ILIAS_CO_ADD(mHandle); //< Add to the alive coroutine set
        return Task<T>(handle);
    }

#if defined(__cpp_exceptions)
    /**
     * @brief For task it will catch BadExpectedAccess<Error> and put it to the value
     * 
     */
    auto unhandled_exception() noexcept -> void {
        try {
            throw;
        }
        catch (BadExpectedAccess<Error> &err) {
            mValue.emplace(Unexpected(err.error()));
        }
        catch (...) {
            mException = std::current_exception();
        }
    }
#endif

    /**
     * @brief Get the stored return value, do not double call it or call it when the coroutine is still not done
     * 
     * @return Result<T> 
     */
    auto value() -> Result<T> {
#if defined(__cpp_exceptions)
        if (mException) {
            auto exception = mException;
            mException = nullptr;
            std::rethrow_exception(exception);
        }
#endif
        ILIAS_ASSERT_MSG(mHandle.done(), "Task<T> is not done yet!");
        ILIAS_ASSERT_MSG(mValue.has_value(), "Task<T> doesn't has value, do you forget co_return?");
        return std::move(*mValue);
    }

    template <typename U>
    auto return_value(U &&value) noexcept(std::is_nothrow_move_constructible_v<Result<T> >) -> void {
        mValue.emplace(std::forward<U>(value));
    }

    /**
     * @brief Return the value, the moveable result<T> version
     * 
     * @tparam har 
     * @param value 
     */
    template <char = 0>
    auto return_value(Result<T> &&value) noexcept(std::is_nothrow_move_constructible_v<Result<T> >) -> void {
        mValue.emplace(std::move(value));
    }

    /**
     * @brief Get the promise handle type
     * 
     * @return handle_type 
     */
    auto handle() const -> handle_type {
        return handle_type::from_address(mHandle.address());
    }
private:
#if defined(__cpp_exceptions)
    std::exception_ptr mException;
#endif
    std::optional<Result<T> > mValue;
};

/**
 * @brief Helper class for record the suspend flags
 * 
 * @tparam T 
 */
template <typename T>
class AwaitRecorder {
public:
    auto await_ready() -> bool { 
        return mAwaiter.await_ready(); 
    }
    template <typename U>
    auto await_suspend(std::coroutine_handle<TaskPromise<U> > handle) {
        ILIAS_ASSERT(&handle.promise() == mPromise); //< Is same promise? 
        ILIAS_ASSERT(!mPromise->isSuspended());

        mPromise->setSuspended(true);
        return mAwaiter.await_suspend(handle);
    }
    auto await_resume() {
        mPromise->setSuspended(false);
        return mAwaiter.await_resume();
    }

    PromiseBase *mPromise;
    T mAwaiter;
};


// Task
template <typename T>
template <typename Callable, typename ...Args>
inline auto Task<T>::fromCallable(Callable &&callable, Args &&...args) {
    using TaskType = std::invoke_result_t<Callable, Args...>;
    static_assert(IsTask<TaskType>, "Invoke result must be a task");
    if constexpr(!std::is_class_v<Callable> || std::is_empty_v<Callable>) {
        return std::invoke(std::forward<Callable>(callable), std::forward<Args>(args)...);
    }
    else { //< Make callable alive, for lambda with capturing values
        return [](auto callable, auto ...args) -> Task<typename TaskType::value_type> {
            co_return co_await std::invoke(callable, args...);
        }(std::forward<Callable>(callable), std::forward<Args>(args)...);
    }
}

// Some EventLoop 
template <typename T>
inline auto EventLoop::runTask(const Task<T> &task) {
    task.promise().setEventLoop(this);
    return task.value();
}
template <typename T>
inline auto EventLoop::postTask(Task<T> &&task) {
    auto handle = task.leak();
    handle.promise().setEventLoop(this);
    resumeHandle(handle);
    return JoinHandle<T>(handle);
}
template <typename Callable, typename ...Args>
inline auto EventLoop::spawn(Callable &&callable, Args &&...args) {
    return postTask(Task<>::fromCallable(std::forward<Callable>(callable), std::forward<Args>(args)...));
}

// Helper operators
template <typename T>
inline auto operator <<(EventLoop *eventLoop, Task<T> &&task) {
    return eventLoop->postTask(std::move(task));
}
template <std::invocable Callable>
inline auto operator <<(EventLoop *eventLoop, Callable &&callable) {
    return eventLoop->spawn(std::forward<Callable>(callable));
}
template <typename T>
inline auto operator >>(EventLoop *eventLoop, const Task<T> &task) {
    return eventLoop->runTask(task);
}
template <typename Callable, typename ...Args>
inline auto co_spawn(Callable &&callable, Args &&...args) {
    return EventLoop::instance()->spawn(std::forward<Callable>(callable), std::forward<Args>(args)...);
}

ILIAS_NS_END