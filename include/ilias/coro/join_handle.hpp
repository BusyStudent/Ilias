#pragma once

#include "cancel_handle.hpp"
#include "task.hpp"

#define ilias_spawn ::ILIAS_NAMESPACE::EventLoop::instance() <<
#define ilias_go    ::ILIAS_NAMESPACE::EventLoop::instance() <<

ILIAS_NS_BEGIN

/**
 * @brief Handle used to observe the running task but, it can blocking wait for it
 * 
 * @tparam T 
 */
template <typename T>
class JoinHandle final : public CancelHandle {
public:
    using handle_type = typename Task<T>::handle_type;

    explicit JoinHandle(handle_type handle) : CancelHandle(handle) { }
    JoinHandle(std::nullptr_t) : CancelHandle(nullptr) { }
    JoinHandle(const JoinHandle &) = delete;
    JoinHandle(JoinHandle &&) = default;
    JoinHandle() = default;

    /**
     * @brief Blocking wait to get the result
     * 
     * @return Result<T> 
     */
    auto join() -> Result<T>;
    auto joinable() const noexcept -> bool;
    auto operator =(JoinHandle &&other) -> JoinHandle & = default;
};

// JoinHandle
template <typename T>
inline auto JoinHandle<T>::join() -> Result<T> {
    ILIAS_ASSERT(joinable());
    auto h = mCoro.stdHandle<TaskPromise<T> >();
    // If not done, we try to wait it done by enter the event loop
    if (!h.done()) {
        StopToken token;
        h.promise().setStopOnDone(&token);
        h.promise().eventLoop()->run(token);
    }
    // Get the value and drop the task
    auto value = h.promise().value();
    clear();
    return value;
}
template <typename T>
inline auto JoinHandle<T>::joinable() const noexcept -> bool {
    return bool(mCoro);
}

// --- post
template <typename T>
inline auto EventLoop::postTask(Task<T> &&task) {
    auto handle = task.leak();
    handle.promise().setEventLoop(this);
    resumeHandle(handle);
    return JoinHandle<T>(handle);
}
template <typename Callable, typename ...Args>
inline auto EventLoop::spawn(Callable &&callable, Args &&...args) {
    return postTask(Task<>::fromCallable(std::forward<Callable>(callable), std::forward<Args>(args)...));
}

// Helper operators
template <typename T>
inline auto operator <<(EventLoop *eventLoop, Task<T> &&task) {
    return eventLoop->postTask(std::move(task));
}
template <std::invocable Callable>
inline auto operator <<(EventLoop *eventLoop, Callable &&callable) {
    return eventLoop->spawn(std::forward<Callable>(callable));
}
template <typename Callable, typename ...Args>
inline auto co_spawn(Callable &&callable, Args &&...args) {
    return EventLoop::instance()->spawn(std::forward<Callable>(callable), std::forward<Args>(args)...);
}

ILIAS_NS_END