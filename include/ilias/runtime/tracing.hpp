// INTERNAL !!!
#pragma once

#include <ilias/runtime/capture.hpp>
#include <ilias/log.hpp>

ILIAS_NS_BEGIN

namespace runtime {

// Forward declaration
class CoroContext;

/**
 * @brief a subscriber for tracing coroutines
 * @note This interface is unstable and may change in the future
 * 
 */
class ILIAS_API TracingSubscriber {
public:
    virtual ~TracingSubscriber();

    /**
     * @brief an new coroutine is spawned
     * @note This is called at ```ilias::spawn``` or ```Task<T>::wait```
     * 
     * @param ctxt 
     */
    virtual auto onSpawn(const CoroContext &ctxt) -> void = 0;

    /**
     * @brief an spawned coroutine is completed
     * 
     * @param ctxt 
     */
    virtual auto onComplete(const CoroContext &ctxt) -> void = 0;

    /**
     * @brief Install the subscriber to current thread
     * 
     * @return true 
     * @return false The trace feature is not enabled
     */
    auto install() -> bool;

    /**
     * @brief Get the current thread instance of TracingSubscriber
     * 
     * @return TracingSubscriber * (nullptr for none) 
     */
    static auto currentThread() -> TracingSubscriber *;
};

#if !defined(ILIAS_CORO_TRACE)
inline TracingSubscriber::~TracingSubscriber() {}
inline auto TracingSubscriber::install() -> bool { ILIAS_WARN("Runtime", "Tracing feature is not enabled"); return false; }
inline auto TracingSubscriber::currentThread() -> TracingSubscriber * { ILIAS_WARN("Runtime", "Tracing feature is not enabled"); return nullptr; }
#endif // !defined(ILIAS_CORO_TRACE)

} // namespace runtime

// Call the current thread subscriber
namespace runtime::tracing {

inline auto spawn([[maybe_unused]] const CoroContext &ctxt) -> void {

#if defined(ILIAS_CORO_TRACE)
    if (auto sub = runtime::TracingSubscriber::currentThread(); sub) [[unlikely]] {
        sub->onSpawn(ctxt);
    }
#endif // defined(ILIAS_CORO_TRACE)

}

inline auto complete([[maybe_unused]] const CoroContext &ctxt) -> void {

#if defined(ILIAS_CORO_TRACE)
    if (auto sub = runtime::TracingSubscriber::currentThread(); sub) [[unlikely]] {
        sub->onComplete(ctxt);
    }
#endif // defined(ILIAS_CORO_TRACE)

}

} // namespace runtime::tracing

ILIAS_NS_END