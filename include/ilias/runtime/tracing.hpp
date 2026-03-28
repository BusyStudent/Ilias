// INTERNAL !!!
#pragma once

#include <ilias/runtime/capture.hpp>
#include <ilias/log.hpp>

ILIAS_NS_BEGIN

namespace runtime {

// Forward declaration
template <typename T>
class TracingAwaiter;
class CoroContext;

/**
 * @brief a subscriber for tracing coroutines
 * @note This interface is unstable and may change in the future
 * 
 */
class ILIAS_API TracingSubscriber {
public:
    virtual ~TracingSubscriber();

    // Task
    /**
     * @brief an new task is spawned
     * @note This is called at ```ilias::spawn``` or ```Task<T>::wait```
     * 
     * @param ctxt 
     */
    virtual auto onTaskSpawn(const CoroContext &ctxt) noexcept -> void {}

    /**
     * @brief an spawned task is completed
     * 
     * @param ctxt 
     */
    virtual auto onTaskComplete(const CoroContext &ctxt) noexcept -> void {}

    // Executor
    /**
     * @brief The task is resumed
     * 
     * @param ctxt 
     */
    virtual auto onResume(const CoroContext &ctxt) noexcept -> void {}

    /**
     * @brief The task is suspended
     * 
     * @param ctxt 
     */
    virtual auto onSuspend(const CoroContext &ctxt) noexcept -> void {}

    // SubTask
    /**
     * @brief The new sub task is spawned
     * @note This is called in declators, like ```whenAny``` or ```whenAll```
     * 
     * @param child 
     */
    virtual auto onChildBegin(const CoroContext &child) noexcept -> void {}

    /**
     * @brief The sub task is completed
     * 
     * @param child 
     */
    virtual auto onChildEnd(const CoroContext &child) noexcept -> void {}

    /**
     * @brief Install the subscriber to current thread
     * 
     * @return true 
     * @return false The trace feature is not enabled
     */
    auto install() noexcept -> bool;

    /**
     * @brief Get the current thread instance of TracingSubscriber
     * 
     * @return TracingSubscriber * (nullptr for none) 
     */
    static auto currentThread() noexcept -> TracingSubscriber *;
};

#if !defined(ILIAS_CORO_TRACE)
inline TracingSubscriber::~TracingSubscriber() {}
inline auto TracingSubscriber::install() noexcept -> bool { ILIAS_WARN("Runtime", "Tracing feature is not enabled"); return false; }
inline auto TracingSubscriber::currentThread() noexcept -> TracingSubscriber * { ILIAS_WARN("Runtime", "Tracing feature is not enabled"); return nullptr; }
#endif // !defined(ILIAS_CORO_TRACE)

} // namespace runtime

// Call the current thread subscriber
namespace runtime::tracing {

/// @copydoc TracingSubscriber::onTaskSpawn
inline auto taskSpawn([[maybe_unused]] const CoroContext &ctxt) noexcept -> void {

#if defined(ILIAS_CORO_TRACE)
    if (auto sub = runtime::TracingSubscriber::currentThread(); sub) [[unlikely]] {
        sub->onTaskSpawn(ctxt);
    }
#endif // defined(ILIAS_CORO_TRACE)

}

/// @copydoc TracingSubscriber::onTaskComplete
inline auto taskComplete([[maybe_unused]] const CoroContext &ctxt) noexcept -> void {

#if defined(ILIAS_CORO_TRACE)
    if (auto sub = runtime::TracingSubscriber::currentThread(); sub) [[unlikely]] {
        sub->onTaskComplete(ctxt);
    }
#endif // defined(ILIAS_CORO_TRACE)

}

/// @copydoc TracingSubscriber::onResume
inline auto resume([[maybe_unused]] const CoroContext &ctxt) noexcept -> void {

#if defined(ILIAS_CORO_TRACE)
    if (auto sub = runtime::TracingSubscriber::currentThread(); sub) [[unlikely]] {
        sub->onResume(ctxt);
    }
#endif // defined(ILIAS_CORO_TRACE)

}

/// @copydoc TracingSubscriber::onSuspend
inline auto suspend([[maybe_unused]] const CoroContext &ctxt) noexcept -> void {
    
#if defined(ILIAS_CORO_TRACE)
    if (auto sub = runtime::TracingSubscriber::currentThread(); sub) [[unlikely]] {
        sub->onSuspend(ctxt);
    }
#endif // defined(ILIAS_CORO_TRACE)

}

/// @copydoc TracingSubscriber::onChildBegin
inline auto childBegin([[maybe_unused]] const CoroContext &child) noexcept -> void {
    
#if defined(ILIAS_CORO_TRACE)
    if (auto sub = runtime::TracingSubscriber::currentThread(); sub) [[unlikely]] {
        sub->onChildBegin(child);
    }
#endif // defined(ILIAS_CORO_TRACE)

}

/// @copydoc TracingSubscriber::onChildEnd
inline auto childEnd([[maybe_unused]] const CoroContext &child) noexcept -> void {

#if defined(ILIAS_CORO_TRACE)
    if (auto sub = runtime::TracingSubscriber::currentThread(); sub) [[unlikely]] {
        sub->onChildEnd(child);
    }
#endif // defined(ILIAS_CORO_TRACE)

}

} // namespace runtime::tracing

ILIAS_NS_END