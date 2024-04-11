#pragma once

#include <concepts>
#include <coroutine>
#include <exception>
#include "ilias.hpp"
#include "ilias_source_location.hpp"

#if !defined(__cpp_lib_coroutine)
#error "Compiler does not support coroutines"
#endif

#if  defined(ILIAS_NO_SOURCE_LOCATION) || !defined(__cpp_lib_format)
#undef ILIAS_COROUTINE_TRACE
#endif

#if !defined(ILIAS_COROUTINE_TRACE)
#define ILIAS_CTRACE(fmt, ...)
#define ILIAS_CAPTURE_CALLER(name) std::source_location name = std::source_location()
#elif defined(__cpp_lib_print)
#define ILIAS_CTRACE(fmt, ...) ::std::println(stderr, fmt, __VA_ARGS__);
#define ILIAS_CAPTURE_CALLER(name) std::source_location name = std::source_location::current()
#include <print>
#else
#define ILIAS_CTRACE(fmt, ...) ::fprintf(stderr, "%s\n", std::format(fmt, __VA_ARGS__).c_str());
#define ILIAS_CAPTURE_CALLER(name) std::source_location name = std::source_location::current()
#include <format>
#endif

// Useful macros
#define ilias_go   ILIAS_NAMESPACE::EventLoop::instance() <<
#define ilias_wait ILIAS_NAMESPACE::EventLoop::instance() >>
#define ilias_select co_await ILIAS_NAMESPACE::Select

ILIAS_NS_BEGIN

template <typename T>
class AwaitTransform;
template <typename T>
class TaskPromise;
class PromiseBase;

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
 * @brief Get Awaiter's result
 * 
 * @tparam T 
 */
template <Awaiter T> 
using AwaiterResult = decltype(std::declval<T>().await_resume());

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

    virtual void quit() = 0;
    virtual void run() = 0;
    virtual void post(void (*fn)(void *), void *arg = nullptr) = 0;
    virtual bool delTimer(uintptr_t timer) = 0;
    virtual uintptr_t addTimer(int64_t ms, void (*fn)(void *), void *arg, int flags = 0) = 0;

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
     * @brief Resume a coroutine handle in event loop, it doesnot take the ownship
     * 
     * @param handle 
     */
    void resumeHandle(std::coroutine_handle<> handle) noexcept;
    /**
     * @brief Post a coroutine handle to event loop, it will destory the given handle
     * 
     * @param handle 
     */
    void destroyHandle(std::coroutine_handle<> handle) noexcept;
    /**
     * @brief Resume a task by task promise pointer
     * 
     * @param task 
     */
    void resumeCoroutine(PromiseBase *task) noexcept;

    static EventLoop *instance() noexcept;
    static EventLoop *setInstance(EventLoop *loop) noexcept;
private:
    struct Tls {
        EventLoop *loop = nullptr;
    };
    static Tls &_tls() noexcept;
    static void _resumeHandle(void *handlePtr) noexcept;
    static void _destroyHandle(void *handlePtr) noexcept;
protected:
    EventLoop();
    EventLoop(const EventLoop &) = delete;
    ~EventLoop();
};
/**
 * @brief Helper class for switch to another coroutine handle
 * 
 */
class SwitchCoroutine {
public:
    template <typename T>
    SwitchCoroutine(std::coroutine_handle<T> handle) noexcept : mHandle(handle) { }
    ~SwitchCoroutine() = default;

    auto await_ready() const noexcept -> bool { return false; }
    auto await_suspend(std::coroutine_handle<> handle) noexcept { return mHandle; }
    auto await_resume() const noexcept -> void { }
private:
    std::coroutine_handle<> mHandle;
};

inline EventLoop::EventLoop() {
    ILIAS_ASSERT_MSG(instance() == nullptr, "EventLoop instance already exists");
    setInstance(this);
}
inline EventLoop::~EventLoop() {
    ILIAS_ASSERT_MSG(instance() == this, "EventLoop instance already exists");
    setInstance(nullptr);
}
inline EventLoop::Tls &EventLoop::_tls() noexcept {
    static thread_local Tls tls;
    return tls;
}
inline EventLoop *EventLoop::instance() noexcept {
    return _tls().loop;
}
inline EventLoop *EventLoop::setInstance(EventLoop *loop) noexcept {
    auto &tls = _tls();
    auto prev = tls.loop;
    tls.loop = loop;
    return prev;
}
inline void EventLoop::resumeHandle(std::coroutine_handle<> handle) noexcept {
    post(_resumeHandle, handle.address());
}
inline void EventLoop::destroyHandle(std::coroutine_handle<> handle) noexcept {
    post(_destroyHandle, handle.address());
}
inline void EventLoop::_resumeHandle(void *handle) noexcept {
    auto h = std::coroutine_handle<>::from_address(handle);
    h.resume();
}
inline void EventLoop::_destroyHandle(void *handle) noexcept {
    auto h = std::coroutine_handle<>::from_address(handle);
    h.destroy();
}

ILIAS_NS_END