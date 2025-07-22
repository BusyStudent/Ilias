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

#include <ilias/runtime/token.hpp>
#include <ilias/defines.hpp>
#include <functional>
#include <stop_token>
#include <coroutine>
#include <memory>

ILIAS_NS_BEGIN

namespace runtime {

/**
 * @brief Executor, it can post a callable and execute it in the run() method, it is one loop per thread
 * 
 */
class ILIAS_API Executor {
public:
    ~Executor();

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
     * @param token The stop token to break the loop
     */
    virtual auto run(StopToken token) -> void = 0;

    /**
     * @brief Sleep for a specified amount of time
     * 
     * @param ms 
     * @return Task<void> 
     */
    virtual auto sleep(uint64_t ms) -> Task<void> = 0;

    /**
     * @brief Install the executor to the current thread
     * @note If will panic if the executor is already installed
     * 
     */
    virtual auto install() -> void;

    /**
     * @brief Get the current thread's executor
     * 
     * @return Executor* 
     */
    static auto currentThread() -> Executor *;
};

/**
 * @brief The Mini Executor
 * 
 */
class ILIAS_API EventLoop : public Executor {
public:
    EventLoop();
    EventLoop(EventLoop &&) = delete;
    ~EventLoop();

    auto post(void (*fn)(void *), void *args) -> void override;
    auto run(StopToken token) -> void override;
    auto sleep(uint64_t ms) -> Task<void> override;
private:
    struct Impl;

    std::unique_ptr<Impl> d;
};

} // namespace runtime

namespace runtime::threadpool {
    extern auto ILIAS_API submit(std::move_only_function<void(StopToken)> fn) -> void;
} // namespace runtime::threadpool

namespace runtime::utils {
    extern auto ILIAS_API setThreadName(std::string_view name) -> void;
}

ILIAS_NS_END