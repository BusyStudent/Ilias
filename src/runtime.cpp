#include <ilias/runtime/executor.hpp>
#include <ilias/runtime/tracing.hpp>
#include <ilias/runtime/timer.hpp>
#include <ilias/runtime/coro.hpp>
#include <ilias/task/task.hpp>
#include <condition_variable> // std::condition_variable
#include <memory_resource> // std::pmr::memory_resource
#include <system_error> // std::system_error
#include <thread> // std::thread
#include <queue> // std::queue
#include <mutex> // std::mutex
#include <new>

#if defined(_WIN32)
    #include <ilias/detail/win32defs.hpp>
#endif // _WIN32


ILIAS_NS_BEGIN

using namespace runtime;

// Executor
namespace {
    thread_local constinit Executor *gCurrentExecutor {};
}

Executor::~Executor() {
    uninstall();
}

auto Executor::currentThread() noexcept -> Executor * {
    return gCurrentExecutor;
}

auto Executor::install() -> void {
    if (gCurrentExecutor && gCurrentExecutor != this) {
        ILIAS_ERROR("Runtime", "A different executor already installed");
        ILIAS_THROW(std::runtime_error("A different executor already installed"));
    }
    gCurrentExecutor = this;
}

auto Executor::uninstall() -> void {
    if (gCurrentExecutor == this) {
        gCurrentExecutor = nullptr;
    }
}

// EventLoop
struct EventLoop::Impl {
    using Callback = std::pair<void (*)(void *), void *>;

    std::queue<Callback> localQueue;
    std::queue<Callback> sharedQueue; // The queue shared between threads, protected by mutex
    std::condition_variable cond;
    std::mutex mutex;
    TimerService service;
};

EventLoop::EventLoop() : d(std::make_unique<Impl>()) {}
EventLoop::~EventLoop() = default;

auto EventLoop::post(void (*fn)(void *), void *args) -> void {
    if (Executor::currentThread() == this) {
        d->localQueue.emplace(fn, args);
        return;
    }
    {
        std::lock_guard locker {d->mutex};
        d->sharedQueue.emplace(fn, args);
    }
    d->cond.notify_one();
}

auto EventLoop::run(StopToken token) -> void {
    auto callback = runtime::StopCallback(token, [&]() {
        d->cond.notify_one();
    });
    auto pred = [&]() {
        return !d->localQueue.empty() || !d->sharedQueue.empty() || token.stop_requested();
    };
    while (true) {
        // First process local queue
        while (!d->localQueue.empty()) {
            auto fn = d->localQueue.front();
            d->localQueue.pop();
            fn.first(fn.second);
        }

        // Begin waiting for callbacks
        std::unique_lock locker {d->mutex};
        if (auto timepoint = d->service.nextTimepoint(); timepoint) {
            d->cond.wait_until(locker, *timepoint, pred);
        }
        else {
            d->cond.wait(locker, pred);
        }

        ILIAS_ASSERT(d->localQueue.empty(), "Local queue should be empty after processing");
        d->localQueue.swap(d->sharedQueue); // Collect all callbacks from shared queue
        if (d->localQueue.empty() && token.stop_requested()) { // Only quit after process all avaliable callbacks
            return;
        }
        if (locker.owns_lock()) {
            locker.unlock();
        }
        d->service.updateTimers();
    }
}

auto EventLoop::sleep(std::chrono::nanoseconds ns) -> Task<void> {
    co_return co_await d->service.sleep(ns);
}

auto threadpool::submit(CallableRef &callable) -> void {

#if defined(_WIN32)
    auto invoke = [](void *cb) -> ::DWORD {
        auto callable = static_cast<CallableRef *>(cb);
        callable->invoke();
        return 0;
    };
    if (!::QueueUserWorkItem(invoke, &callable, WT_EXECUTEDEFAULT)) {
        ILIAS_THROW(std::system_error(std::error_code(GetLastError(), std::system_category()), "Faliled to submit to thread pool"));
    }
#else // Use our own thread pool
    struct ThreadPool {
        StopSource stopSource; // for notifying the threads to stop
        std::queue<CallableRef *> queue;
        std::condition_variable cond;
        std::mutex mutex;
        std::vector<std::thread> threads;
        std::atomic<size_t> idle {0}; // number of idle threads
        std::atomic<std::chrono::steady_clock::time_point> lastPeek; // last time the worker peeked the queue
    };
    static constinit ThreadPool *pool = nullptr;
    static constinit std::once_flag once;

    auto dispatch = [](StopToken &token) {
        while (true) {
            std::unique_lock locker(pool->mutex);
            pool->cond.wait(locker, [&]() {
                return !pool->queue.empty() || token.stop_requested();
            });
            if (token.stop_requested()) {
                return;
            }
            auto *callable = pool->queue.front();
            pool->queue.pop();
            pool->lastPeek = std::chrono::steady_clock::now();
            locker.unlock();
            
            pool->idle -= 1;
            callable->invoke();
            pool->idle += 1;
        }
    };
    auto worker = [&](StopToken token) {
        ::pthread_setname_np(::pthread_self(), "ilias::worker");
        dispatch(token);
        pool->idle -= 1; // The worker thread is exiting, so -= 1
    };

    auto cleanup = []() {
        pool->stopSource.request_stop();
        pool->cond.notify_all();
        for (auto &thread : pool->threads) {
            thread.join();
        }
        delete pool;
    };

    auto init = [&]() {
        pool = new ThreadPool; 
        pool->idle += 1;
        pool->threads.emplace_back(std::thread(worker, pool->stopSource.get_token()));
        ::atexit(cleanup);
    };

    std::call_once(once, init);
    std::lock_guard locker(pool->mutex);
    if (pool->idle == 0) {
        auto hw = std::thread::hardware_concurrency() * 2;
        if (pool->threads.size() < hw && hw != 0) { // We can create more threads
            pool->threads.emplace_back(std::thread(worker, pool->stopSource.get_token()));
            pool->idle += 1;
        }
    }
    pool->queue.emplace(&callable);
    pool->cond.notify_one();
#endif
}

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