#pragma once

#include <concepts>
#include <coroutine>
#include <exception>
#include <source_location>
#include "ilias.hpp"

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

// For user easy macro
#define ILIAS_CHECK_EXISTS(h) ILIAS_CHECK(ILIAS_CO_EXISTS(h))
#define ILIAS_CHECK_NEXISTS(h) ILIAS_CHECK(!ILIAS_CO_EXISTS(h))
#define ILIAS_CO_RESUME(h) if (h) { ILIAS_CHECK_EXISTS(h); h.resume(); }

// Useful macros
#define ilias_go   ::ILIAS_NAMESPACE::EventLoop::instance() <<
#define ilias_wait ::ILIAS_NAMESPACE::EventLoop::instance() >>
#define ilias_spawn ::ILIAS_NAMESPACE::EventLoop::instance() <<

ILIAS_NS_BEGIN

template <typename T>
class AwaitTransform;
template <typename T>
class AwaitRecorder;
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
/**
 * @brief Helper class for suspend current coroutine and get the coroutine handle
 * 
 * @tparam T 
 */
template <std::invocable T>
class SuspendCoroutine {
public:
    SuspendCoroutine(T &&cb) noexcept : mCallback(cb) { }
    ~SuspendCoroutine() = default;

    auto await_ready() const noexcept -> bool { return false; }
    template <typename U>
    auto await_suspend(std::coroutine_handle<U> handle) noexcept -> void { mCallback(handle); }
    auto await_resume() const noexcept -> void { }
private:
    T mCallback;
};
/**
 * @brief Helper to construct the type by our self
 * 
 * @tparam T 
 */
template <typename T>
class Uninitialized {
public:
    Uninitialized() = default;
    Uninitialized(const Uninitialized &) = default;
    Uninitialized(Uninitialized &&) = default;
    ~Uninitialized() { if (mInited) data()->~T();  }

    template <typename ...Args>
    auto construct(Args &&...args) noexcept(std::is_nothrow_constructible_v<T, Args...>) -> void { 
        new(&mValue) T(std::forward<Args>(args)...);
        mInited = true;
    }
    auto hasValue() const noexcept -> bool { return mInited; }
    auto data() -> T * { return reinterpret_cast<T *>(mValue); }
    auto operator *() -> T & { return *reinterpret_cast<T *>(mValue); }
    auto operator ->() -> T * { return reinterpret_cast<T *>(mValue); }
private:
    uint8_t mValue[sizeof(T)];
    bool    mInited = false;
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