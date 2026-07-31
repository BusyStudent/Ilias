#include <ilias/runtime/tracing.hpp>
#include <ilias/runtime/coro.hpp>
#include <unordered_map>
#include <atomic>

ILIAS_NS_BEGIN

using namespace runtime;

// CoroContext
auto CoroContext::stop() noexcept -> bool {
    return mStopSource.request_stop();
}

auto CoroContext::setStopped() noexcept -> void {
    mStopped = true;
    mStoppedHandler(*this); // Call the stopped handler, we are stopped
    mStoppedHandler = nullptr; // Mark it as called
}

// TRACING
#if defined(ILIAS_CORO_TRACE)
namespace {
    struct ContextsMap {
        std::unordered_map<SpanId, TraceContext *> map;

        ContextsMap() {
            map.reserve(2048);
        }
        ~ContextsMap() {
            if (!map.empty()) {
                ILIAS_WARN("Runtime", "There are still {} coroutines running", map.size());
            }
        }
    };

    // Use negative values for child spans, positive values for root spans
    thread_local constinit std::atomic<intptr_t> gChildSpanId {-1};
    thread_local constinit std::atomic<intptr_t> gTraceId {1};
    thread_local constinit TracingSubscriber *gSubscriber {};
    thread_local ContextsMap gContextsMap {};
}

TracingSubscriber::~TracingSubscriber() {
    if (gSubscriber == this) {
        gSubscriber = nullptr;
    }
}

auto TracingSubscriber::install() noexcept -> bool {
    gSubscriber = this;
    return true;
}

auto TracingSubscriber::currentThread() noexcept -> TracingSubscriber * {
    return gSubscriber;
}

// TRACING in the context
auto TraceContext::span() noexcept -> TraceSpan & {
    if (mSpan.id != SpanId::Invalid) { // Initialized
        return mSpan;
    }

    // Alloc self id
    if (mParent) { // Child span
        mSpan.id = static_cast<SpanId>(gChildSpanId.fetch_sub(1));
    }
    else {
        mSpan.id = static_cast<SpanId>(gTraceId.fetch_add(1));
    }

    // Set parent it
    mSpan.parentId = mParent ? mParent->id() : SpanId::Invalid;
    mSpan.rootId = mParent ? mRoot->id() : SpanId::Invalid;

    // Set name
    mSpan.name = mName;
    return mSpan;
}

auto TraceContext::id() noexcept -> SpanId {
    return span().id;
}

auto TraceContext::setName(std::string_view name) noexcept -> void {
    mName = name;
    span().name = mName;
    if (gSubscriber) {
        gSubscriber->onEvent(TraceEvent {
            .type = TraceEvent::NameChange,
            .span = span()
        });
    }
}

// TODO: Too much code duplication here
auto TraceContext::spawn(CaptureSource source) noexcept -> void {
    if (!gSubscriber) {
        return;
    }
    auto [_, inserted] = gContextsMap.map.emplace(id(), this);
    ILIAS_ASSERT(inserted, "TaskId {} already exists, this should never happen", static_cast<uintptr_t>(id()));

    // Notify the subscriber
    gSubscriber->onEvent(TraceEvent {
        .type = TraceEvent::Spawn,
        .span = span(),
        .location = source
    });
}

auto TraceContext::complete() noexcept -> void {
    if (!gSubscriber) {
        return;
    }
    ILIAS_ASSERT(mSpan.id != SpanId::Invalid, "TaskId is invalid, did you call it before spawning?");

    // Notify the subscriber
    gSubscriber->onEvent(TraceEvent {
        .type = TraceEvent::Complete,
        .span = span()
    });
    gContextsMap.map.erase(id());
}

auto TraceContext::resume() noexcept -> void {
    if (!gSubscriber) {
        return;
    }
    if (!mSuspended) { // Not suspended
        return;
    }
    mSuspended = false;
    
    // Calc the time
    mSpan.lastResumeAt = std::chrono::steady_clock::now();
    // Increase the resumes count to all parent
    for (auto cur = this; cur != nullptr; cur = cur->parent()) {
        cur->mSpan.resumes += 1;
    }

    // Notify the subscriber
    gSubscriber->onEvent(TraceEvent {
        .type = TraceEvent::Resume,
        .span = span()
    });
}

auto TraceContext::suspend() noexcept -> void {
    if (!gSubscriber) {
        return;
    }
    if (mSuspended) { // Already suspended
        return;
    }
    mSuspended = true;

    // Calc the time
    auto now = std::chrono::steady_clock::now();
    auto busyTime = now - mSpan.lastResumeAt;
    // Increase the busy time to all parent
    for (auto cur = this; cur != nullptr; cur = cur->parent()) {
        cur->mSpan.totalBusy += busyTime;
    }

    // Notify the subscriber
    gSubscriber->onEvent(TraceEvent {
        .type = TraceEvent::Suspend,
        .span = span()
    });
}

auto TraceContext::fromId(SpanId id) noexcept -> TraceContext * {
    auto it = gContextsMap.map.find(id);
    if (it == gContextsMap.map.end()) {
        return nullptr;
    }
    return it->second;
}

#endif // ILIAS_CORO_TRACE

// Use system allocator
auto runtime::allocate(size_t size) -> void * { 
    return std::malloc(size); 
}

auto runtime::deallocate(void *ptr, size_t) noexcept -> void { 
    return std::free(ptr);
}

ILIAS_NS_END