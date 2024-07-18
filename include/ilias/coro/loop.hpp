#pragma once

/**
 * @file loop.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Interface of the EventLoop (callback based)
 * @version 0.1
 * @date 2024-07-18
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#include "../ilias.hpp"
#include <source_location>
#include <coroutine>
#include <concepts>

#if !defined(__cpp_lib_coroutine)
#error "Compiler does not support coroutines"
#endif

// TODO: Cleanup this macro

// For the life-time track
#if !defined(ILIAS_COROUTINE_LIFETIME_CHECK)
#define ILIAS_CO_EXISTS(h) true
#define ILIAS_CO_ADD(h) 
#define ILIAS_CO_REMOVE(h) 
#else
#define ILIAS_CO_EXISTS(h) (_ilias_coset.contains(h))
#define ILIAS_CO_ADD(h) ILIAS_CHECK_NEXISTS(h); _ilias_coset.insert(h)
#define ILIAS_CO_REMOVE(h) ILIAS_CHECK_EXISTS(h); _ilias_coset.erase(h)
#include <set>
inline std::set<std::coroutine_handle<> > _ilias_coset;
#endif

// For track the caller
#if !defined(NDEBUG)
#define ILIAS_CAPTURE_CALLER(name) std::source_location name = std::source_location()
#else
#define ILIAS_CAPTURE_CALLER(name) std::source_location name = std::source_location::current()
#endif

// For user easy macro
#define ILIAS_CHECK_EXISTS(h) ILIAS_CHECK(ILIAS_CO_EXISTS(h))
#define ILIAS_CHECK_NEXISTS(h) ILIAS_CHECK(!ILIAS_CO_EXISTS(h))
#define ILIAS_CO_RESUME(h) if (h) { ILIAS_CHECK_EXISTS(h); h.resume(); }

// Useful macros
#define ilias_go   ::ILIAS_NAMESPACE::EventLoop::instance() <<
#define ilias_wait ::ILIAS_NAMESPACE::EventLoop::instance() >>
#define ilias_spawn ::ILIAS_NAMESPACE::EventLoop::instance() <<

ILIAS_NS_BEGIN

/**
 * @brief A Class for wrapping stop
 * 
 */
class StopToken {
public:
    StopToken(const StopToken &) = delete;
    StopToken() = default;
    ~StopToken() = default;

    auto stop() noexcept -> void;
    auto isStopRequested() const noexcept -> bool;
    auto setCallback(void (*fn)(void *), void *user) noexcept -> void;
private:
    bool mStop = false; //< Is stop requested ?
    void *mUser = nullptr;
    void (*mFunc)(void *user) = nullptr; //< Called on stop was called
};

/**
 * @brief Abstraction of event loop
 * 
 */
class EventLoop {
public:
    enum TimerFlags : int {
        TimerDefault    = 0 << 0,
        TimerSingleShot = 1 << 0, //< This timer is single shot (it will auto remove self)
    };
    
    /**
     * @brief Enter the event loop (blocking), MT unsafe
     * 
     * @param token the stop token, user should use it to stop the current loop, this function will return after this
     * 
     */
    virtual auto run(StopToken &token) -> void = 0;
    /**
     * @brief Post a callback to the event queue, MT safe
     * 
     * @param fn 
     * @param arg 
     */
    virtual auto post(void (*fn)(void *), void *arg = nullptr) -> void = 0;
    /**
     * @brief Del a existing timer, MT unsafe
     * 
     * @param timer 
     * @return true 
     * @return false 
     */
    virtual auto delTimer(uintptr_t timer) -> bool = 0;
    /**
     * @brief Add a new timer, MT unsafe
     * 
     * @param ms 
     * @param fn 
     * @param arg 
     * @param flags 
     * @return uintptr_t 
     */
    virtual auto addTimer(int64_t ms, void (*fn)(void *), void *arg, int flags = 0) -> uintptr_t = 0;

    /**
     * @brief Blocking execute a task
     * 
     * @tparam T 
     * @param task 
     * @return auto 
     */
    template <typename T>
    auto runTask(const Task<T> &task);
    /**
     * @brief let the task run in the event loop, it takes the ownship
     * 
     * @tparam T 
     * @param task 
     * @return auto 
     */
    template <typename T>
    auto postTask(Task<T> &&task);
    /**
     * @brief Create a task by callable and args, post it to the event loop (support captured lambda)
     * 
     * @tparam Callable 
     * @tparam Args 
     * @param callable 
     * @param args 
     * @return auto 
     */
    template <typename Callable, typename ...Args>
    auto spawn(Callable &&callable, Args &&...args);
    /**
     * @brief Resume a coroutine handle in event loop, it doesnot take the ownship
     * 
     * @param handle 
     */
    auto resumeHandle(std::coroutine_handle<> handle) noexcept -> void;
    /**
     * @brief Post a coroutine handle to event loop, it will destory the given handle
     * 
     * @param handle 
     */
    auto destroyHandle(std::coroutine_handle<> handle) noexcept -> void;
    
    static auto instance() noexcept -> EventLoop *;
    static auto setInstance(EventLoop *loop) noexcept -> EventLoop *;
private:
    struct Tls {
        EventLoop *loop = nullptr;
    };
    static auto _tls() noexcept -> Tls &;
    static auto _resumeHandle(void *handlePtr) noexcept -> void;
    static auto _destroyHandle(void *handlePtr) noexcept -> void;
protected:
    EventLoop();
    EventLoop(const EventLoop &) = delete;
    ~EventLoop();
};


inline EventLoop::EventLoop() {
    ILIAS_ASSERT_MSG(instance() == nullptr, "EventLoop instance already exists");
    setInstance(this);
}
inline EventLoop::~EventLoop() {
    ILIAS_ASSERT_MSG(instance() == this, "EventLoop instance already exists");
    setInstance(nullptr);
}
inline auto EventLoop::_tls() noexcept -> Tls & {
    static thread_local Tls tls;
    return tls;
}
inline auto EventLoop::instance() noexcept -> EventLoop * {
    return _tls().loop;
}
inline auto EventLoop::setInstance(EventLoop *loop) noexcept -> EventLoop * {
    auto &tls = _tls();
    auto prev = tls.loop;
    tls.loop = loop;
    return prev;
}
inline auto EventLoop::resumeHandle(std::coroutine_handle<> handle) noexcept -> void {
    ILIAS_CHECK_EXISTS(handle);
    post(_resumeHandle, handle.address());
}
inline auto EventLoop::destroyHandle(std::coroutine_handle<> handle) noexcept -> void {
    ILIAS_CHECK_EXISTS(handle);
    post(_destroyHandle, handle.address());
}
inline auto EventLoop::_resumeHandle(void *handle) noexcept -> void {
    auto h = std::coroutine_handle<>::from_address(handle);
    ILIAS_CHECK_EXISTS(h);
    h.resume();
}
inline auto EventLoop::_destroyHandle(void *handle) noexcept -> void {
    auto h = std::coroutine_handle<>::from_address(handle);
    ILIAS_CHECK_EXISTS(h);
    h.destroy();
}

inline auto StopToken::isStopRequested() const noexcept -> bool {
    return mStop;
}
inline auto StopToken::setCallback(void (*func)(void *), void *user) noexcept -> void {
    mFunc = func;
    mUser = user;
}
inline auto StopToken::stop() noexcept -> void {
    if (mStop) {
        return;
    }
    mStop = true;
    // Invoke func if
    if (mFunc) {
        mFunc(mUser);
    }
}

ILIAS_NS_END