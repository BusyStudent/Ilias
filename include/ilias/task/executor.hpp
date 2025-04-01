/**
 * @file executor.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The abstraction of post and excute functions
 * @version 0.1
 * @date 2024-08-07
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/cancellation_token.hpp>
#include <ilias/ilias.hpp>
#include <coroutine>
#include <memory>

ILIAS_NS_BEGIN

/**
 * @brief Executor, it can post a callable and execute it in the run() method, it is one loop per thread
 * 
 */
class Executor {
public:
    /**
     * @brief Post a callable to the executor (thread safe)
     * 
     * @param fn The function to post (can not be null)
     * @param args The arguments of the function
     */
    virtual auto post(void (*fn)(void *), void *args) -> void = 0;

    /**
     * @brief Enter and run the task in the executor, it will infinitely loop until the token is canceled
     * 
     * @param token The cancellation token to break the loop
     */
    virtual auto run(CancellationToken &token) -> void = 0;

    /**
     * @brief Sleep for a specified amount of time
     * 
     * @param ms 
     * @return IoTask<void> 
     */
    virtual auto sleep(uint64_t ms) -> IoTask<void> = 0;

    /**
     * @brief Schedule a function to be executed in the executor
     * 
     * @param fn The function to schedule (can not be null)
     */
    auto schedule(detail::MoveOnlyFunction<void()> &&fn) -> void {
        ILIAS_ASSERT_MSG(fn, "fn can not be null");
        auto ptr = new detail::MoveOnlyFunction<void()>(std::move(fn));
        post(scheduleImpl, ptr);
    }

    /**
     * @brief Get the current thread's executor
     * 
     * @return Executor* 
     */
    static auto currentThread() -> Executor * {
        return currentThreadImpl();
    }
protected:
    /**
     * @brief Construct a new Executor object, and set the current thread's executor to this
     * 
     */
    Executor() {  
        ILIAS_ASSERT_MSG(currentThreadImpl() == nullptr, "Executor already exists in the current thread");
        currentThreadImpl() = this; 
    }

    /**
     * @brief Destroy the Executor object, and set the current thread's executor to nullptr
     * 
     */
    ~Executor() { 
        ILIAS_ASSERT_MSG(currentThreadImpl() == this, "Executor not match");
        currentThreadImpl() = nullptr; 
    }
private:
    /**
     * @brief Get the thread local ref to the current executor ptr
     * 
     * @return Executor*& 
     */
    static auto currentThreadImpl() -> Executor * & {
        static thread_local Executor *current = nullptr;
        return current;
    }

    /**
     * @brief The callback to invoke the scheduled function
     * 
     * @param ptr 
     */
    static auto scheduleImpl(void *ptr) noexcept -> void {
        std::unique_ptr<detail::MoveOnlyFunction<void()> > fn {
            static_cast<detail::MoveOnlyFunction<void()> *>(ptr)
        };
        (*fn)();
    }
};

ILIAS_NS_END