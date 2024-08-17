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
#include <ilias/cancellation_token.hpp>
#include <optional>
#include <vector>

ILIAS_NS_BEGIN

namespace detail {

class SwitchCoroutine {
public:
    SwitchCoroutine(std::coroutine_handle<> handle) : mHandle(handle) { }

    auto await_ready() noexcept { return false; }
    auto await_suspend(std::coroutine_handle<>) noexcept { return mHandle; }
    auto await_resume() noexcept { ::abort(); }
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

class TaskPromiseBase {
public:
    TaskPromiseBase() = default;
    TaskPromiseBase(const TaskPromiseBase&) = delete;

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
        for (auto &[callback, arg] : mCallbacks) {
            callback(arg);
        }
        if (!mAwaitingCoroutine) {
            mAwaitingCoroutine = std::noop_coroutine();
        }
        return mAwaitingCoroutine;
    }

    /**
     * @brief Default exception handler, terminates the program
     * 
     */
    auto unhandled_exception() const noexcept -> void {
        std::terminate();
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
        mCallbacks.emplace_back(callback, arg);
    }
protected:  
    bool mStarted = false;
    Executor *mExecutor = Executor::currentThread(); //< The executor, doing the 
    CancellationToken mToken; //< The cancellation token
    std::coroutine_handle<> mAwaitingCoroutine; //< The coroutine handle that is waiting for us, we will resume it when done 
    std::vector<std::pair<void (*)(void *), void *> > mCallbacks; //< The callbacks that will be called when the coroutine is done
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
    using value_type = Result<T>;

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

#if defined(__cpp_exceptions)
    /**
     * @brief Exception support, it will translate BadExpectedAccess<Error> to the Unexpected<Error>() and store it to value
     * 
     */
    auto unhandled_exception() noexcept -> void {
        try {
            throw;
        }
        catch (const BadExpectedAccess<Error>& e) {
            mValue.emplace(Unexpected(e.error()));
        }
        catch (...) {
            mException = std::current_exception();
        }
    }
#endif

    /**
     * @brief Get the return value of the coroutine
     * 
     * @return value_type 
     */
    auto value() -> value_type {
#if defined(__cpp_exceptions)
        if (mException) {
            std::rethrow_exception(mException);
        }
#endif
        ILIAS_ASSERT(handle().done()); //< The coroutine should be done
        ILIAS_ASSERT(mValue.has_value()); //< The value should be set
        return std::move(*mValue);
    }
private:
    std::optional<value_type> mValue; //< The value
#if defined(__cpp_exceptions)
    std::exception_ptr mException; //< The exception
#endif
};

}

ILIAS_NS_END