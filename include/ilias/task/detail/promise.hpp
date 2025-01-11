/**
 * @file promise.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The Task's promise
 * @version 0.1
 * @date 2024-08-09
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/task/executor.hpp>
#include <ilias/detail/expected.hpp>
#include <ilias/detail/functional.hpp>
#include <ilias/cancellation_token.hpp>
#include <ilias/log.hpp>
#include <source_location>
#include <concepts>
#include <optional>
#include <vector>

#if defined(ILIAS_TASK_TRACE)
    #define ILIAS_CAPTURE_CALLER(name) std::source_location name = std::source_location::current()
#else
    #define ILIAS_CAPTURE_CALLER(name) [[maybe_unused]] int name = 0
#endif // defined(ILIAS_TASK_TRACE)


ILIAS_NS_BEGIN

namespace detail {

class SwitchCoroutine {
public:
    SwitchCoroutine(std::coroutine_handle<> handle) : mHandle(handle) { }

    auto await_ready() noexcept { return false; }
    auto await_suspend(std::coroutine_handle<>) noexcept { return mHandle; }
    [[noreturn]]
    auto await_resume() noexcept { unreachable(); } //< We don't need to resume the current coroutine, it is already done
private:
    std::coroutine_handle<> mHandle;
};

class StartCoroutine {
public:
    StartCoroutine(bool &started) : mStarted(started) { }

    auto await_ready() noexcept { return false; }
    auto await_suspend(std::coroutine_handle<>) noexcept { }
    auto await_resume() noexcept { mStarted = true; }
private:
    bool &mStarted;
};

/**
 * @brief The Task's promise common part, hold the exception and the schedule code
 * 
 */
class TaskPromiseBase {
public:
    TaskPromiseBase() = default;
    TaskPromiseBase(const TaskPromiseBase &) = delete;

#if defined(__cpp_exceptions)
    ~TaskPromiseBase() noexcept {
        if (!mException) [[likely]] { //< Exception are throwed or no exception occured
            return;
        }

#if !defined(NDEBUG) //< Try give some debug info about the exception
        ILIAS_ERROR("Task", "Unhandled exception in task");
        try {
            std::rethrow_exception(mException);
        }
        catch (std::exception &e) {
            ILIAS_ERROR("Task", "Exception.what = {}", e.what());
        }
        catch (...) { }
#endif // !defined(NDEBUG)
        std::terminate();
    }
    /**
     * @brief Default exception handler, store the exception
     * 
     * @return auto 
     */
    auto unhandled_exception() noexcept { 
        mException = std::current_exception();
    }

    /**
     * @brief Default rethrow the exception if there is one
     * 
     */
    auto rethrowIfException() -> void {
        if (mException) {
            std::rethrow_exception(std::exchange(mException, nullptr));
        }
    }
#else //< No exceptions
    /**
     * @brief Default exception handler, no exception support, so it is unreachable
     * 
     */
    auto unhandled_exception() const noexcept -> void {
        unreachable();
    }

    /**
     * @brief Default rethrow the exception, no-op
     * 
     */
    auto rethrowIfException() const noexcept -> void {

    }
#endif // defined(__cpp_exceptions)


#if defined(ILIAS_TASK_TRACE)
    /**
     * @brief Forward the awaiter, it used to trace the await point
     * 
     * @tparam T 
     * @param awaiter 
     * @param loc The source location of the await point
     * @return decltype(auto) 
     */
    template <typename T>
    auto await_transform(T &&awaiter, std::source_location loc = std::source_location::current()) -> decltype(auto) {
        mAwaitLocation = loc;
        mChild.clear(); //< Clear previous await info
        return std::forward<T>(awaiter);
    }
#endif // defined(ILIAS_TASK_TRACE)

    /**
     * @brief On the coroutine start, we are lazy, so we suspend it
     * 
     * @return std::suspend_always 
     */
    auto initial_suspend() noexcept -> StartCoroutine {
        return {mStarted};
    }

    /**
     * @brief On the coroutine done, we will resume the coroutine that is waiting for us
     * 
     * @return SwitchCoroutine 
     */
    auto final_suspend() noexcept -> SwitchCoroutine {
        for (auto &callback: mCallbacks) {
            callback();
        }
        return mAwaitingCoroutine;
    }

    /**
     * @brief Get the cancellation token object
     * 
     * @return CancellationToken & 
     */
    auto cancellationToken() -> CancellationToken & {
        return mToken;
    }

    /**
     * @brief Get the executor object
     * 
     * @return Executor* 
     */
    auto executor() -> Executor * {
        return mExecutor;
    }

    /**
     * @brief Check the coroutine is started
     * 
     * @return true 
     * @return false 
     */
    auto isStarted() const -> bool {
        return mStarted;
    }

    /**
     * @brief Set the Executor object
     * 
     * @param executor The executor, executing the coroutine (can't be nullptr)
     */
    auto setExecutor(Executor *executor) -> void {
        mExecutor = executor;
    }

    /**
     * @brief Set the Awaiting Coroutine object
     * 
     * @param handle The coroutine handle that is waiting for us
     */
    auto setAwaitingCoroutine(std::coroutine_handle<> handle) -> void {
        mAwaitingCoroutine = handle;
    }

    /**
     * @brief Register a callback that will be called when the coroutine is done
     * 
     * @param callback 
     * @param arg 
     */
    auto registerCallback(void (*callback)(void *), void *arg) -> void {
        mCallbacks.emplace_back([=]() {
            callback(arg);
        });
    }

    /**
     * @brief Register a callback that will be called when the coroutine is done
     * 
     * @param callback 
     */
    auto registerCallback(MoveOnlyFunction<void()> &&callback) -> void {
        mCallbacks.emplace_back(std::move(callback));
    }

#if defined(ILIAS_TASK_MALLOC)
    /**
     * @brief Allocate memory for the promise
     * 
     * @param size 
     * @return void* 
     */
    auto operator new(size_t size) -> void * {
        return ILIAS_TASK_MALLOC(size);
    }

    /**
     * @brief Free the memory of the promise
     * 
     * @param ptr 
     */
    auto operator delete(void *ptr) -> void {
        ILIAS_TASK_FREE(ptr);
    }
#endif // defined(ILIAS_TASK_MALLOC)

protected:  
    bool mStarted = false;
    Executor *mExecutor = nullptr; //< The executor, doing the 
    CancellationToken mToken { CancellationToken::AutoReset }; //< The cancellation token
    std::coroutine_handle<> mAwaitingCoroutine { std::noop_coroutine() }; //< The coroutine handle that is waiting for us, we will resume it when done 
    std::vector<MoveOnlyFunction<void()> > mCallbacks; //< The callbacks that will be called when the coroutine is done
#if defined(__cpp_exceptions)
    std::exception_ptr mException; //< The stored exception
#endif // defined(__cpp_exceptions)

#if defined(ILIAS_TASK_TRACE) //< Used for debug and trace
    std::source_location mCreateLocation;  //< The location of the creation
    std::source_location mAwaitLocation;   //< The location of the await point
    std::vector<TaskPromiseBase *> mChild; //< The Task we are await for
    TaskPromiseBase *mParent = nullptr;    //< The Task who await us
#endif // defined(ILIAS_TASK_TRACE)
};

/**
 * @brief The promise impl for process the return value T, hold the return value
 * 
 * @tparam T 
 */
template <typename T>
class TaskPromiseImpl : public TaskPromiseBase {
public:
    using value_type = T;

    /**
     * @brief Return the value of the coroutine
     * 
     * @param value 
     */
    auto return_value(value_type value) -> void {
        mValue.emplace(std::move(value));
    }

    template <typename U>
    auto return_value(U &&value) -> void {
        mValue.emplace(std::forward<U>(value));
    }

    /**
     * @brief Get the return value of the coroutine
     * 
     * @return value_type 
     */
    auto value() -> value_type {
        rethrowIfException();
        ILIAS_ASSERT(mValue.has_value()); //< The return value should be set
        return std::move(*mValue);
    }
private:
    std::optional<value_type> mValue; //< The value
};

#if defined(__cpp_exceptions) //< Handle the exception specially
/**
 * @brief The specialized version for handle the exception specially
 * 
 * @tparam T 
 */
template <typename T>
class TaskPromiseImpl<Result<T> > : public TaskPromiseBase {
public:
    using value_type = Result<T>;

    auto return_value(value_type value) -> void {
        mValue.emplace(std::move(value));
    }

    template <typename U>
    auto return_value(U &&value) -> void {
        mValue.emplace(std::forward<U>(value));
    }

    auto unhandled_exception() noexcept -> void {
        try {
            throw;
        }
        catch (BadExpectedAccess<Error> &e) { //< Translate the BadExpectedAccess into return value's errc
            mValue.emplace(Unexpected(e.error()));
        }
        catch (...) {
            mException = std::current_exception();
        }
    }

    auto value() -> value_type {
        rethrowIfException();
        ILIAS_ASSERT(mValue.has_value());
        return std::move(*mValue);
    }
private:
    std::optional<value_type> mValue; //< The value
};
#endif // defined(__cpp_exceptions)

/**
 * @brief The promise impl for void return value
 * 
 * @tparam  
 */
template <>
class TaskPromiseImpl<void> : public TaskPromiseBase {
public:
    auto return_void() const noexcept { }
    auto value() const noexcept { }
};

/**
 * @brief The promise of the Task<T>, interop with the Task<T> class
 * 
 * @tparam T 
 */
template <typename T>
class TaskPromise final : public TaskPromiseImpl<T> {
public:
    using handle_type = std::coroutine_handle<TaskPromise>;

    /**
     * @brief Get the coroutine handle of the promise
     * 
     * @return handle_type 
     */
    auto handle() -> handle_type {
        return handle_type::from_promise(*this);
    }

    /**
     * @brief Get the return object object of the coroutine, wrap it to the Task<T>
     * @param loc The source location of the task
     * @return Task<T> 
     */
    auto get_return_object(ILIAS_CAPTURE_CALLER(loc)) -> Task<T> {
#if defined(ILIAS_TASK_TRACE)
        this->mCreateLocation = loc; //< Store the location whe trace enable
#endif // defined(ILIAS_TASK_TRACE)
        return {handle()};
    }
};

template <typename T>
concept IsTaskPromise = std::is_base_of_v<TaskPromiseBase, T>;

} // namespace detail

ILIAS_NS_END