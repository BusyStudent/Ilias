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

#include <ilias/runtime/functional.hpp> // SmallCallable
#include <ilias/runtime/token.hpp> // StopToken
#include <ilias/defines.hpp>
#include <functional>
#include <stop_token>
#include <coroutine>
#include <cstring>
#include <memory>
#include <array>

ILIAS_NS_BEGIN

namespace runtime {

/**
 * @brief Executor, it can post a callable and execute it in the run() method, it is one loop per thread
 * 
 */
class ILIAS_API Executor {
public:
    virtual ~Executor();

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
     * @note If will throw std::runtime_error if the executor is already installed
     * 
     */
    auto install() -> void;

    /**
     * @brief Uninstall the executor from the current thread, if the executor is not installed, no-op
     * 
     */
    auto uninstall() -> void;

    /**
     * @brief Get the current thread's executor
     * 
     * @return Executor * (nullptr on no executor installed)
     */
    static auto currentThread() -> Executor *;

    /**
     * @brief Schedule a coroutine to the executor (thread safe)
     * 
     * @param h The coroutine handle (can not be null)
     */
    inline auto schedule(std::coroutine_handle<> h) -> void {
        post(scheduleImpl, h.address());
    }

    /**
     * @brief Schedule a callable to the executor (thread safe)
     * 
     * @tparam Fn The callable type
     * @param fn 
     */
    template <std::invocable Fn>
        requires (!std::convertible_to<Fn, std::coroutine_handle<> >) // Avoid conflict with coroutine_handle
    inline auto schedule(Fn fn) -> void {
        if constexpr (SmallCallable<Fn>) {
            // We can store the function in the void *;
            // Delegate to it
            auto [proxy, args] = SmallFunction<void()> {fn}.toRaw();
            post(proxy, args);
        }
        else { // Alloc the memory and post it
            post(scheduleAlloc<Fn>, new Fn(std::move(fn)));
        }
    }
private:
    // Corutine proxy
    static auto scheduleImpl(void *h) -> void {
        std::coroutine_handle<>::from_address(h).resume();
    }

    // The allocated memory object proxy
    template <std::invocable Fn>
    static auto scheduleAlloc(void *args) -> void {
        auto ptr = static_cast<Fn*>(args);
        auto guard = std::unique_ptr<Fn>(ptr);
        (*guard)();
    }
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

/**
 * @brief The CallableRef struct. used to impl function reference
 * 
 */
class CallableRef {
public:
    CallableRef() = default;

    // Invoke it
    auto invoke() noexcept -> void { mHandler(*this); }
private:
    using Handler = void (*)(CallableRef &) noexcept;
    Handler mHandler = nullptr;
template <typename T>
friend class CallableImpl;
};

/**
 * @brief The CallableImpl struct, use CRTP to impl the CallableRef
 * 
 * @tparam T 
 */
template <typename T>
class CallableImpl : public CallableRef {
public:
    CallableImpl() noexcept {
        mHandler = invokeImpl;
    }
private:
    static auto invokeImpl(CallableRef &callable) noexcept -> void {
        static_cast<T&>(callable)();
    }
};

} // namespace runtime

namespace runtime::threadpool {
    extern auto ILIAS_API submit(CallableRef &callable) -> void;
} // namespace runtime::threadpool

// Re-export the EventLoop
using runtime::EventLoop;

ILIAS_NS_END