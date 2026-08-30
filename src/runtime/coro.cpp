#include <ilias/runtime/tracing.hpp>
#include <ilias/runtime/coro.hpp>
#include <memory_resource>
#include <unordered_map>
#include <algorithm>
#include <ranges>
#include <memory>
#include <vector>

ILIAS_NS_BEGIN

using namespace runtime;

// CoroContext
auto CoroContext::stop() noexcept -> bool {
    return mStopSource.request_stop();
}

auto CoroContext::setStopped() noexcept -> void {
    auto stopped = reinterpret_cast<StoppedHandler>(-1); // NOLINT
    auto handler = std::exchange(mStoppedHandler, stopped); // Mark we are stopped and get the handler
    ILIAS_ASSERT(handler, "Stopped handler not set ?");
    ILIAS_ASSERT(handler != stopped, "We are already stopped");
    handler(*this); // Call the stopped handler, we are stopped
}

// Check Standard layout
static_assert(std::is_standard_layout_v<TraceSpan>);
static_assert(std::is_standard_layout_v<TraceRegistery>);

// TRACING
#if defined(ILIAS_CORO_TRACE)
namespace {
    struct Registery {
        // ABI export to outside
        TraceRegistery registery;

        // Internal
        std::pmr::unsynchronized_pool_resource pool;
        std::pmr::unordered_map<SpanId, TraceContext *> map {&pool};
        std::pmr::unordered_map<SpanId, std::pmr::vector<SpanId> > children {&pool}; // For manage the children set

        std::pmr::unordered_map<SpanId, size_t> spansIdx {&pool}; // For fastly remove item from vector
        std::pmr::vector<const TraceSpan *> spans {&pool}; // Span list, used by registery

        // Use negative values for child spans, positive values for root spans
        intptr_t childSpanId {-1};
        intptr_t traceId {1};

        Registery() {
            map.reserve(2048);
            spansIdx.reserve(2048);
            spans.reserve(2048);
        }

        ~Registery() {
            if (!map.empty()) {
                ILIAS_WARN("Runtime", "There are still {} coroutines running", map.size());
            }
        }

        auto registerChild(TraceContext &ctxt) -> void {
            auto id = ctxt.id();
            auto [_, inserted] = children.emplace(id, std::pmr::vector<SpanId>{&pool});
            if (!ctxt.parent()) {
                return;
            }

            auto &parentSpan = ctxt.parent()->span();
            auto &child = children.at(parentSpan.id);
            child.push_back(id);

            // Update ptr
            parentSpan.child = child.data();
            parentSpan.childCount = child.size();
        }

        auto unregisterChild(TraceContext &ctxt) -> void {
            if (!ctxt.parent()) {
                return;
            }
            auto id = ctxt.id();
            auto &parentSpan = ctxt.parent()->span();
            auto child = children.at(parentSpan.id);
            auto n = std::erase_if(child, [id](auto &v) { return v == id; });
            ILIAS_ASSERT(n == 1, "Id {} not found, this should never happen", id);

            // Update ptr
            parentSpan.child = child.data();
            parentSpan.childCount = child.size();
        }

        auto register_(TraceContext &ctxt, const TraceSpan &span) -> void {
            auto id = span.id;
            auto [_, inserted] = map.emplace(id, &ctxt);
            spans.emplace_back(&span);
            ILIAS_ASSERT(inserted, "Id {} already exists, this should never happen", id);

            // Update ptr
            registery.spans = spans.data();
            registery.spanCount = spans.size();

            // Update idx
            ILIAS_ASSERT(!spans.empty());
            spansIdx[id] = spans.size() - 1; // Last index

            // Prepare children & parent
            registerChild(ctxt);
        }

        auto unregister(TraceContext &ctxt) -> void {
            auto id = ctxt.id();
            auto n = map.erase(id);
            ILIAS_ASSERT(n == 1, "Id {} not found, this should never happen", id);
            unregisterChild(ctxt);

            // Swap the last item with the current one
            auto idx = spansIdx.at(id);
            if (idx != spans.size() - 1) { // Not self
                std::swap(spans[idx], spans.back());
                spansIdx[spans[idx]->id] = idx; // Update index
            }

            spans.pop_back();
            spansIdx.erase(id);
            children.erase(id);

            // Update ptr
            registery.spans = spans.data();
            registery.spanCount = spans.size();
        }
    };

    template <typename T>
    struct Lazy {
        std::unique_ptr<T> val;

        auto operator ->() -> T * {
            if (!val) [[unlikely]] {
                val = std::make_unique<T>();
            }
            return val.get();
        }
    };

    // Global subscriber and registery
    thread_local constinit TracingSubscriber *gSubscriber {};
    thread_local constinit Lazy<Registery> gRegistery {};

    // Get the now time in nanoseconds
    auto nsNow() -> int64_t {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    }
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
        mSpan.id = static_cast<SpanId>(gRegistery->childSpanId--);
    }
    else {
        mSpan.id = static_cast<SpanId>(gRegistery->traceId++);
    }

    // Set parent it
    mSpan.parentId = mParent ? mParent->id() : SpanId::Invalid;
    mSpan.rootId = mParent ? mRoot->id() : SpanId::Invalid;

    // Set name
    mSpan.name = mName.c_str();

    // Init time
    mSpan.createdAt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    return mSpan;
}

auto TraceContext::id() noexcept -> SpanId {
    return span().id;
}

auto TraceContext::setName(std::string_view name) noexcept -> void {
    mName = name;
    span().name = mName.c_str();
    if (gSubscriber) {
        gSubscriber->onEvent(TraceEvent {
            .type = TraceEvent::NameChange,
            .span = span()
        });
    }
}

// TODO: Too much code duplication here
auto TraceContext::spawn(CaptureSource source) noexcept -> void {
    auto loc = source.toLocation();

    // Register self to the registery
    gRegistery->register_(*this, span());
    mLocation = std::string{loc.file_name()} + ":" + std::to_string(loc.line());
    std::ranges::replace(mLocation, '\\', '/');
    
    span().state = TraceState::Running;
    span().location = mLocation.c_str();

    // Notify the subscriber
    if (gSubscriber) {
        gSubscriber->onEvent(TraceEvent {
            .type = TraceEvent::Spawn,
            .span = span(),
            .location = source
        });
    }
}

auto TraceContext::complete(CaptureSource) noexcept -> void {
    ILIAS_ASSERT(mSpan.id != SpanId::Invalid, "TaskId is invalid, did you call complete() before spawn()?");
    span().state = TraceState::Completed;

    // Notify the subscriber
    if (gSubscriber) {
        gSubscriber->onEvent(TraceEvent {
            .type = TraceEvent::Complete,
            .span = span()
        });
    }
    gRegistery->unregister(*this);
}

auto TraceContext::resume(CaptureSource) noexcept -> void {
    if (!mSuspended) { // Not suspended
        return;
    }
    if (auto frame = topFrame(); frame) {
        frame->setMessage({}); // Clear the message
    }
    span().state = TraceState::Running;
    mSuspended = false;
    
    // Calc the time
    mSpan.lastResumeAt = nsNow();
    // Increase the resumes count to all parent
    for (auto cur = this; cur != nullptr; cur = cur->parent()) {
        cur->mSpan.resumes += 1;
    }

    // Notify the subscriber
    if (gSubscriber) {
        gSubscriber->onEvent(TraceEvent {
            .type = TraceEvent::Resume,
            .span = span()
        });
    }
}

auto TraceContext::suspend(CaptureSource) noexcept -> void {
    if (mSuspended) { // Already suspended
        return;
    }
    span().state = TraceState::Suspended;
    mSuspended = true;

    // Calc the time
    auto now = nsNow();
    auto busyTime = now - mSpan.lastResumeAt;
    // Increase the busy time to all parent
    for (auto cur = this; cur != nullptr; cur = cur->parent()) {
        cur->mSpan.totalBusy += busyTime;
    }

    // Notify the subscriber
    if (gSubscriber) {
        gSubscriber->onEvent(TraceEvent {
            .type = TraceEvent::Suspend,
            .span = span()
        });
    }
}

auto TraceContext::fromId(SpanId id) noexcept -> TraceContext * {
    auto it = gRegistery->map.find(id);
    if (it == gRegistery->map.end()) {
        return nullptr;
    }
    return it->second;
}

auto TraceRegistery::currentThread() -> const TraceRegistery * {
    return &gRegistery->registery;
}

extern "C" {
    auto runtime::_ilias_trace_registry_v1() -> const TraceRegistery * {
        if (!gRegistery.val) { // Avoid side effect
            return nullptr;
        }
        return &gRegistery->registery;
    }
} // extern "C"

#endif // ILIAS_CORO_TRACE

// Use system allocator
auto runtime::allocate(size_t size) -> void * { 
    return std::malloc(size); 
}

auto runtime::deallocate(void *ptr, size_t) noexcept -> void { 
    return std::free(ptr);
}

ILIAS_NS_END