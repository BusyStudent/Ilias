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
#include <concepts>
#include <optional>
#include <vector>

ILIAS_NS_BEGIN

namespace detail {

class SwitchCoroutine {
public:
    SwitchCoroutine(std::coroutine_handle<> handle) : mHandle(handle) { }

    auto await_ready() noexcept { return false; }
    auto await_suspend(std::coroutine_handle<>) noexcept { return mHandle; }
    auto await_resume() noexcept { unreachable(); } //< We don't need to resume the coroutine, it is already done
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
 * @brief The Task's promise common part
 * 
 */
class TaskPromiseBase {
public:
    TaskPromiseBase() { mToken.setAutoReset(true); } //< The default cancel policy is CancelPolicy::Once
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
        if (!mAwaitingCoroutine) {
            mAwaitingCoroutine = std::noop_coroutine();
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
    CancellationToken mToken; //< The cancellation token
    std::coroutine_handle<> mAwaitingCoroutine; //< The coroutine handle that is waiting for us, we will resume it when done 
    std::vector<MoveOnlyFunction<void()> > mCallbacks; //< The callbacks that will be called when the coroutine is done
#if defined(__cpp_exceptions)
    std::exception_ptr mException; //< The stored exception
#endif // defined(__cpp_exceptions)
};

/**
 * @brief The promise of the Task<T>, hold the return value and exception
 * 
 * @tparam T 
 */
template <typename T>
class TaskPromise final : public TaskPromiseBase {
public:
    using handle_type = std::coroutine_handle<TaskPromise>;
    using value_type = T;

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
     * 
     * @return Task<T> 
     */
    auto get_return_object() -> Task<T> {
        return {handle()};
    }

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

#if defined(__cpp_exceptions)
    /**
     * @brief Exception support, it will translate BadExpectedAccess<Error> to the Unexpected<Error>() and store it to value
     * 
     */
    auto unhandled_exception() noexcept -> void {
        if constexpr (IsResult<T>) {
            try {
                throw;
            }
            catch (const BadExpectedAccess<Error> &e) {
                mValue.emplace(Unexpected(e.error()));
            }
            catch (...) {
                mException = std::current_exception();
            }
        }
        else {
            TaskPromiseBase::unhandled_exception(); //< forward to the base class
        }
    }
#endif

    /**
     * @brief Get the return value of the coroutine
     * 
     * @return value_type 
     */
    auto value() -> value_type {
        rethrowIfException();
        ILIAS_ASSERT(handle().done()); //< The coroutine should be done
        ILIAS_ASSERT(mValue.has_value()); //< The value should be set
        return std::move(*mValue);
    }
private:
    std::optional<value_type> mValue; //< The value
};

/**
 * @brief The promise of the Task<void>, hold the return value and exception
 * 
 * @tparam  
 */
template <>
class TaskPromise<void> final : public TaskPromiseBase {
public:
    using handle_type = std::coroutine_handle<TaskPromise>;
    using value_type = void;

    /**
     * @brief Get the coroutine handle of the promise
     * 
     * @return handle_type 
     */
    auto handle() -> handle_type {
        return handle_type::from_promise(*this);
    }

    /**
     * @brief Get the return object object, wrap it to the Task<void>
     * 
     * @return Task<void> 
     */
    template <typename T = void>
    auto get_return_object() -> Task<T> {
        return {handle()};
    }

    /**
     * @brief Return the value of the coroutine
     * 
     */
    auto return_void() -> void {
        // nothing to do
    }

    /**
     * @brief Get the return value of the coroutine
     * 
     */
    auto value() -> void {
        rethrowIfException();
        ILIAS_ASSERT(handle().done()); //< The coroutine should be done
    }
};


template <typename T>
concept IsTaskPromise = std::is_base_of_v<TaskPromiseBase, T>;

} // namespace detail

ILIAS_NS_END