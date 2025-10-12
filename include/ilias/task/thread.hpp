/**
 * @file thread.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The thread class, used to run a task in a separate thread.
 * @version 0.1
 * @date 2025-10-11
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#pragma once

#include <ilias/runtime/executor.hpp>
#include <ilias/runtime/token.hpp>
#include <ilias/runtime/coro.hpp>
#include <ilias/detail/option.hpp> // Option<T>
#include <ilias/task/task.hpp>
#include <semaphore> // std::binary_semaphore
#include <concepts> // std::invocable
#include <memory> // std::unique_ptr
#include <thread> // std::thread
#include <tuple> // std::tuple

ILIAS_NS_BEGIN

namespace task {

using runtime::StopToken;
using runtime::StopSource;
using runtime::CoroHandle;
using runtime::Executor;

// The common code part of Thread
class ThreadBase {
public:
    struct Deleter {
        auto operator()(ThreadBase *self) -> void {
            self->destroy();
        }
    };

    ThreadBase(const ThreadBase &) = delete;

    // Try blocking join the thread (no-op if already joined)
    auto blockingJoin() -> void {
        if (mThread.joinable()) {
            mThread.join();
        }
    }

    // Send the stop request to the thread
    auto stop() -> void {
        mSource.request_stop();
    }

    // Destroy tne thread, it will try to send stop request to the thread and then wait for the thread to exit.
    auto destroy() -> void { 
        stop();
        blockingJoin();
        return mDestroy(this); 
    }

    // Try suspend the caller, return false if the thread is already completed.
    auto tryAwait(CoroHandle handle) noexcept -> bool {
        bool ret = true;
        mSem.acquire();
        if (mCompleted) { // The mValue is ready
            ret = false;
        }
        else {
            mHandle = handle;
        }
        mSem.release();
        return ret;
    }

    // Set the executor used in the thread
    template <typename T>
    auto setExecutor() noexcept -> void {
        mInit = []() -> Executor * {
            return new T;
        };
    }

    // Start the thread
    ILIAS_API
    auto start() -> void;
protected:
    ThreadBase() = default;
    ~ThreadBase() = default;

    Executor *          (*mInit)() = nullptr; // Create the executor (nullptr on builtin, the PlatformContext)
    Task<void>          (*mInvoke)(ThreadBase &self) = nullptr; // Call the task
    void                (*mDestroy)(ThreadBase *self) = nullptr; // Destroy the thread state
    std::exception_ptr    mException; // The exception that the invoke throwed
private:
    StopSource            mSource;
    CoroHandle            mHandle; // For the coroutine who await this thread
    std::thread           mThread; // the thread that runs the task
    std::binary_semaphore mSem {1}; // Used to locked the mHandle & mCompleted
    bool                  mCompleted = false;
};

// The state object used for Thread<T>. hold the return value of the task.
template <typename T>
class ThreadImpl : public ThreadBase {
public:
    auto value() -> Option<T> {
        if (mException) {
            std::rethrow_exception(std::exchange(mException, {}));
        }
        return std::move(mValue);
    }
protected:
    Option<T>           mValue; // the return value of the task
};

template <typename Fn, typename ...Args> requires (std::invocable<Fn, Args...>)
class ThreadCallable final : public ThreadImpl<typename std::invoke_result_t<Fn, Args...>::value_type> {
public:
    ThreadCallable(Fn fn, Args &&...args) : mFn(std::move(fn)), mArgs(std::forward<Args>(args)...) {
        this->mInvoke = &ThreadCallable::onInvoke;
        this->mDestroy = &ThreadCallable::onDestroy;
    }
private:
    static auto onInvoke(ThreadBase &_self) -> Task<void> {
        auto &self = static_cast<ThreadCallable &>(_self);
        if constexpr (std::is_same_v<decltype(self.mValue), Option<void> >) {
            co_await std::apply(self.mFn, self.mArgs);
            self.mValue.emplace(); // We are done here
        }
        else {
            self.mValue = co_await std::apply(self.mFn, self.mArgs);
        }
    }

    static auto onDestroy(ThreadBase *self) -> void {
        delete static_cast<ThreadCallable *>(self);
    }

    Fn                  mFn;
    std::tuple<Args...> mArgs;
};

// The RAII object that holds the thread handle
template <typename T>
using ThreadHandle = std::unique_ptr<ThreadImpl<T>, ThreadBase::Deleter>;

// For co_await Thread<T>
template <typename T>
class ThreadAwaiter {
public:
    ThreadAwaiter(ThreadHandle<T> handle) : mHandle(std::move(handle)) {}
    ThreadAwaiter(ThreadAwaiter &&) = default;

    auto await_ready() const noexcept { return false; }

    auto await_suspend(CoroHandle caller) noexcept -> bool {
        mCaller = caller;
        if (!mHandle->tryAwait(caller)) { // The thread completed, wakeup
            return false;
        }
        // Forward the stop request to the thread
        mRegistration.register_<&ThreadBase::stop>(caller.stopToken(), mHandle.get());
        return true;
    }

    auto await_resume() -> Option<T> {
        return mHandle->value();
    }
private:
    ThreadHandle<T> mHandle;
    CoroHandle      mCaller;
    StopRegistration mRegistration;
};

} // namespace task


/**
 * @brief The tags used to create a thread (specific executor)
 * 
 * @tparam T 
 */
template <typename T> requires (std::is_base_of_v<runtime::Executor, T>)
class UseExecutor {};

/**
 * @brief The thread class, used to run a task in a separate thread. 
 * @note It will stop & `BLOCKING!!!` join the thread when destroyed, use `join()` to await it before destroyed
 * 
 * @tparam T 
 */
template <typename T>
class Thread {
public:
    Thread() = default;
    Thread(Thread &&) = default;
    ~Thread() = default;

    /**
     * @brief Start an new thread by given callable and args
     * 
     * @param fn 
     * @param args 
     */
    template <typename Fn, typename ...Args> requires (std::invocable<Fn, Args...>)
    explicit Thread(Fn fn, Args &&...args) : mHandle(new task::ThreadCallable<Fn, Args...>(std::move(fn), std::forward<Args>(args)...)) {
        mHandle->start();
    }

    /**
     * @brief Start an new thread by given callable and args, with sepecific executor
     * 
     * @tparam E 
     * @tparam Fn 
     * @tparam Args 
     */
    template <typename E, typename Fn, typename ...Args> requires (std::invocable<Fn, Args...>)
    explicit Thread(UseExecutor<E>, Fn fn, Args &&...args) : mHandle(new task::ThreadCallable<Fn, Args...>(std::move(fn), std::forward<Args>(args)...)) {
        mHandle->template setExecutor<E>();
        mHandle->start();
    }

    /**
     * @brief Check the thread is joinable
     * 
     * @return bool
     */
    auto joinable() const noexcept { return bool(mHandle); }

    /**
     * @brief Send an stop request to the thread
     * 
     */
    auto stop() noexcept { return mHandle->stop(); }

    /**
     * @brief Blocking current thread until the thread is done, return the result of the task
     * @warning Use with caution in async environment, it will `BLOCK!!!` the current executor, use co_await join() instead
     * 
     * @return Option<T>, nullopt on stopped 
     */
    auto blockingJoin() -> Option<T> { 
        ILIAS_ASSERT(joinable());
        auto ptr = std::exchange(mHandle, nullptr);
        ptr->blockingJoin();
        return ptr->value();
    }

    /**
     * @brief Join the thread, if the caller receives a stop request, it will forward the stop request to the thread
     * 
     * @return task::ThreadAwaiter<T> 
     */
    auto join() -> task::ThreadAwaiter<T> {
        ILIAS_ASSERT(joinable());
        return {std::exchange(mHandle, nullptr)};
    }

    auto operator =(const Thread &) -> Thread & = delete;
    auto operator =(Thread &&) -> Thread & = default;

    /**
     * @brief Check the thread is joinable
     * 
     */
    explicit operator bool() const noexcept { return joinable(); }

    /**
     * @brief Join the thread, as same as co_await thread.join()
     * 
     * @return co_await 
     */
    auto operator co_await() && -> task::ThreadAwaiter<T> { 
        return join();
    }
private:
    task::ThreadHandle<T> mHandle;
};

template <typename Fn, typename ...Args>
Thread(Fn fn, Args &&...args) -> Thread<typename std::invoke_result_t<Fn, Args...>::value_type>;

template <typename E, typename Fn, typename ...Args>
Thread(UseExecutor<E>, Fn fn, Args &&...args) -> Thread<typename std::invoke_result_t<Fn, Args...>::value_type>;

ILIAS_NS_END