// INTERNAL !!!
#pragma once

#include <ilias/runtime/capture.hpp>
#include <ilias/log.hpp>
#include <source_location>
#include <chrono>
#include <string>

ILIAS_NS_BEGIN

namespace runtime {

/**
 * @brief The id of a task 
 * 
 */
enum class TaskId : intptr_t {
    Invalid = 0,
};

/**
 * @brief The metadata of the trace event
 * 
 */
class TraceMeta {
public:
    // Tree
    TaskId id {};
    TaskId parentId {};
    TaskId rootId {};

    // Name
    std::string_view name;

    // Resume / Suspend
    std::chrono::steady_clock::time_point lastResumeAt {};
    std::chrono::steady_clock::duration   totalBusy {};
    size_t                                resumes {}; // The number of resumes counted
};

/**
 * @brief The event used for tracing the coroutine execution
 * 
 */
class TraceEvent {
public:
    enum Type {
        Spawn,      // An new task is spawn to execute
        Complete,   // A task is completed
        Resume,     // A task is resumed
        Suspend,    // A task is suspended
        NameChange, // The name of a task is changed
    } type {};
    
    const TraceMeta &meta;
    std::source_location location; // The location of the event happened (currently only for spawn)
};

// TODO: Refactor this?
/**
 * @brief a subscriber for tracing coroutines
 * @note This interface is unstable and may change in the future
 * 
 */
class ILIAS_API TracingSubscriber {
public:
    virtual ~TracingSubscriber();

    /**
     * @brief Notify the subscriber that a new event occured
     * 
     * @param event 
     */
    virtual auto onEvent(const TraceEvent &event) noexcept -> void = 0;

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

// Formatting
inline auto toString(TaskId id) -> std::string {
    if (id == TaskId::Invalid) return "Invalid";
    return std::to_string(static_cast<intptr_t>(id));
}

inline auto toString(TraceEvent::Type type) -> std::string_view {
    switch (type) {
        case TraceEvent::Spawn: return "Spawn";
        case TraceEvent::Complete: return "Complete";
        case TraceEvent::Resume: return "Resume";
        case TraceEvent::Suspend: return "Suspend";
        case TraceEvent::NameChange: return "NameChange";
        default: return "Unknown";
    }
}

// Mark it
ILIAS_FORMATTABLE(TaskId);
ILIAS_FORMATTABLE(TraceEvent::Type);

} // namespace runtime

ILIAS_NS_END