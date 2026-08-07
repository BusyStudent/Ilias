// INTERNAL !!!
#pragma once

#include <ilias/runtime/capture.hpp>
#include <ilias/runtime/await.hpp>
#include <ilias/log.hpp>
#include <source_location>
#include <chrono>
#include <string>

#if !defined(ILIAS_CORO_TRACE)
    #define ILIAS_TRACING_API
#else
    #define ILIAS_TRACING_API ILIAS_API
#endif

ILIAS_NS_BEGIN

namespace runtime {

/**
 * @brief The id of tracing unit
 * 
 */
enum class SpanId : intptr_t {
    Invalid = 0,
};

/**
 * @brief The state of the span
 * 
 */
enum class TraceState : uint8_t {
    Created = 0,
    Running,
    Suspended,
    Completed,
};

/**
 * @brief The span of the trace event, an record of the context
 * @note This struct should be keep in standard layout
 * 
 */
class TraceSpan {
public:
    // ABI
    const size_t size = sizeof(TraceSpan);

    // Tree
    SpanId id {};
    SpanId parentId {};
    SpanId rootId {};

    // State
    TraceState state {};

    // Name
    const char *name {};

    // Location when spawned (file:line)
    const char *location {};

    // Children (unordered)
    const SpanId *child {};
    size_t        childCount {};

    // Resume / Suspend
    int64_t lastResumeAt {}; // The last resume time (in nanoseconds)
    int64_t totalBusy {}; // The busy duration (in nanoseconds)
    time_t  createdAt {}; // The create time (in system time)
    size_t  resumes {}; // The number of resumes counted
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
    
    const TraceSpan &span;
    std::source_location location; // The location of the event happened (currently only for spawn)
};

/**
 * @brief The unit for tracing the coroutine execution
 * 
 */
class ILIAS_TRACING_API TraceContext {
public:
    TraceContext() = default;
    TraceContext(TraceContext &&) = default;
    
    // Operator
    auto operator =(TraceContext &&) -> TraceContext & = default;

#if !defined(ILIAS_CORO_TRACE) // Disabled
    auto setParent(TraceContext &) noexcept {}
    auto setName(std::string_view) noexcept {}
    auto pushFrame(auto ...) noexcept { return 0; }
    auto popFrame() noexcept {}
    auto stacktrace() const noexcept { return Stacktrace {}; }
    auto parent() const noexcept { return static_cast<TraceContext *>(nullptr); }
    auto name() const noexcept { return std::string_view {}; }
    auto id() const noexcept { return static_cast<SpanId>(reinterpret_cast<intptr_t>(this)); }
    
    auto spawn(CaptureSource source = {}) noexcept {}
    auto complete(CaptureSource source = {}) noexcept {}
    auto resume(CaptureSource source = {}) noexcept {}
    auto suspend(CaptureSource source = {}) noexcept {}
#else
    // Set the parent of the ctxt
    auto setParent(TraceContext &parent) noexcept -> void {
        if (!parent.mParent) { // The parent is the root
            mRoot = &parent;
        }
        else {
            mRoot = parent.mRoot; // Has parent, used the cache
        }
        mParent = &parent;
    }

    // Set the name of the ctxt
    auto setName(std::string_view name) noexcept -> void;

    // Push the frame to the stack, return the index of the frame
    template <typename ...Args>
    auto pushFrame(Args &&...args) noexcept {
        mFrames.emplace_back(std::forward<Args>(args)...);
        return mFrames.size() - 1;
    }

    // Pop the frame from the stack
    auto popFrame() noexcept -> void {
        ILIAS_ASSERT(!mFrames.empty());
        mFrames.pop_back();
    }

    // Get the top frame of the ctxt (return pointer, nullptr on empty)
    auto topFrame() noexcept {
        return mFrames.empty() ? nullptr : &mFrames.back();
    }

    auto topFrame() const noexcept {
        return mFrames.empty() ? nullptr : &mFrames.back();
    }

    // Get the stacktrace of the ctxt
    auto stacktrace() const noexcept {
        std::vector<StackFrame> vec{};
        for (auto cur = this; cur != nullptr; cur = cur->mParent) {
            vec.insert(vec.end(), cur->mFrames.rbegin(), cur->mFrames.rend());
        }
        return Stacktrace { std::move(vec) };
    }

    // Get the parent of the ctxt
    auto parent() const noexcept {
        return mParent;
    }

    // Get the debug name of the ctxt
    auto name() const noexcept {
        return std::string_view {mName};
    }

    // Get the id of the ctxt
    auto id() noexcept -> SpanId;

    // Notify we are begin to execute the coroutine
    auto spawn(CaptureSource source = {}) noexcept -> void;

    // Notify we are completed the coroutine
    auto complete() noexcept -> void;

    // Notify we are resumed the coroutine (It is safe to call resume() multiple times, we will ignore the duplicate calls)
    auto resume() noexcept -> void;

    // Notify we are suspended the coroutine (It is safe to call suspend() multiple times, we will ignore the duplicate calls)
    auto suspend() noexcept -> void;

    // Lazy init the span
    auto span() noexcept -> TraceSpan &;

    // Get the context pointer from the id, if not found, return nullptr
    static auto fromId(SpanId id) noexcept -> TraceContext *;
private:
    bool          mStarted = false;      // The coroutine is started
    bool          mSuspended = true;     // The coroutine is suspended
    TraceContext *mParent = nullptr;     // Use for stacktrace to dump the whole stack
    TraceContext *mRoot = nullptr;       // The root context of the coroutine (spawn or blocking wait)
    std::string   mName;                 // The name of the coroutine
    std::string   mLocation;             // The location of the spawn (file:line)
    TraceSpan     mSpan;
    std::vector<StackFrame> mFrames;     // The virtual frames of the coroutine
#endif // defined(ILIAS_CORO_TRACE)
};

// TODO: Refactor this?
/**
 * @brief a subscriber for tracing coroutines
 * @note This interface is unstable and may change in the future
 * 
 */
class ILIAS_TRACING_API TracingSubscriber {
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

// MARK: TracingAwaitable
// Add hooks to an awaitable, used for tracing
template <typename T, bool Forward>
class TracingAwaitable {
public:
    template <typename U>
    using DecayIf     = std::conditional_t<Forward, U, std::decay_t<U> >;
    using Awaitable   = DecayIf<T>;
    using Awaiter     = DecayIf<decltype(toAwaiter(std::declval<T>()))>;

    // Move version, store it by value
    TracingAwaitable(T awaitable, TraceContext &ctxt) requires(!Forward) : 
        mAwaitable(std::move(awaitable)), 
        mAwaiter(toAwaiter(std::move(mAwaitable))),
        mCtxt(ctxt) {}

    // Forward version, just store the reference
    TracingAwaitable(T awaitable, TraceContext &ctxt) requires(Forward) :
        mAwaitable(std::forward<T>(awaitable)),
        mAwaiter(toAwaiter(std::forward<T>(mAwaitable))),
        mCtxt(ctxt) {}

    // MUST RVO, Pin the awaitable, avoid the awaiter implementation need an stable awaitable address, it will cause dangling if move
    TracingAwaitable(const TracingAwaitable &) = delete;

    // Hooks
    auto await_ready() noexcept(noexcept(mAwaiter.await_ready())) { 
        return mAwaiter.await_ready(); 
    }

    template <typename U>
    auto await_suspend(std::coroutine_handle<U> handle) noexcept(noexcept(mAwaiter.await_suspend(handle))) {
        using Ret = decltype(mAwaiter.await_suspend(handle));
        if constexpr (std::is_same_v<Ret, void>) {
            mAwaiter.await_suspend(handle);
            mCtxt.suspend();
            return;
        }
        else if constexpr (std::convertible_to<Ret, bool>) { // Return bool
            auto ret = mAwaiter.await_suspend(handle);
            if (ret) { // true on actually suspend
                mCtxt.suspend();
            }
            return ret;
        }
        else { // std::coroutine_handle<>
            auto ret = mAwaiter.await_suspend(handle);
            mCtxt.suspend();
            return ret;
        }
    }

    auto await_resume() noexcept(noexcept(mAwaiter.await_resume())) -> decltype(auto) { 
        mCtxt.resume();
        return mAwaiter.await_resume();
    }
private:
    Awaitable mAwaitable;
    Awaiter   mAwaiter;
    TraceContext &mCtxt;
};

// MARK: Registery
class TraceRegistery {
public:
    // ABI
    const size_t size = sizeof(TraceRegistery);

    // All spans (unordered)
    const TraceSpan *const *spans {};
    size_t                  spanCount {};

    ILIAS_TRACING_API
    static auto currentThread() -> const TraceRegistery *;
};

extern "C" {
    extern ILIAS_TRACING_API auto _ilias_trace_registry_v1() -> const TraceRegistery *;
} // extern "C"

#if !defined(ILIAS_CORO_TRACE)
inline TracingSubscriber::~TracingSubscriber() {}
inline auto TracingSubscriber::install() noexcept -> bool { ILIAS_WARN("Runtime", "Tracing feature is not enabled"); return false; }
inline auto TracingSubscriber::currentThread() noexcept -> TracingSubscriber * { ILIAS_WARN("Runtime", "Tracing feature is not enabled"); return nullptr; }
inline auto TraceRegistery::currentThread() -> const TraceRegistery * { return nullptr; }
#endif // !defined(ILIAS_CORO_TRACE)

// Formatting
inline auto toString(SpanId id) -> std::string {
    if (id == SpanId::Invalid) return "Invalid";
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

inline auto toString(TraceState state) -> std::string_view {
    switch (state) {
        case TraceState::Created: return "Created";
        case TraceState::Running: return "Running";
        case TraceState::Suspended: return "Suspended";
        case TraceState::Completed: return "Completed";
        default: return "Unknown";
    }
}

// Mark it
ILIAS_FORMATTABLE(SpanId);
ILIAS_FORMATTABLE(TraceState);
ILIAS_FORMATTABLE(TraceEvent::Type);

} // namespace runtime

ILIAS_NS_END