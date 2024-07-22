#pragma once

#include "cancel_handle.hpp"
#include "task.hpp"

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
    return mCoro;
}



ILIAS_NS_END