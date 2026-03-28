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
    static thread_local constinit Executor *gCurrentExecutor {};
}

Executor::~Executor() {
    uninstall();
}

auto Executor::currentThread() -> Executor * {
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
    std::thread::id id = std::this_thread::get_id(); // The id of the thread running the event loop
    std::mutex mutex;
    TimerService service;
};

EventLoop::EventLoop() : d(std::make_unique<Impl>()) {}
EventLoop::~EventLoop() = default;

auto EventLoop::post(void (*fn)(void *), void *args) -> void {
    if (std::this_thread::get_id() == d->id) {
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

auto EventLoop::sleep(uint64_t ms) -> Task<void> {
    co_return co_await d->service.sleep(ms);
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
            auto callable = pool->queue.front();
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
        pool->threads.push_back(std::thread(worker, pool->stopSource.get_token()));
        ::atexit(cleanup);
    };

    std::call_once(once, init);
    std::lock_guard locker(pool->mutex);
    if (pool->idle == 0) {
        auto hw = std::thread::hardware_concurrency() * 2;
        if (pool->threads.size() < hw && hw != 0) { // We can create more threads
            pool->threads.push_back(std::thread(worker, pool->stopSource.get_token()));
            pool->idle += 1;
        }
    }
    pool->queue.emplace(&callable);
    pool->cond.notify_one();
#endif
}


// TRACING
#if defined(ILIAS_CORO_TRACE)
namespace {
    static thread_local constinit TracingSubscriber *gSubscriber {};
    static thread_local constinit size_t gPrevAlloctionSize {};
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

auto runtime::allocationSize() noexcept -> size_t {
    return gPrevAlloctionSize;
}
#endif // ILIAS_CORO_TRACE

// Thread local memory Pool, default disable
// FIXME: currently scheduleOn cancellation impl conflict with thread local memory pool
//        It may cause task free mismatch when cancelled, so disable it for now
#if 0
namespace {
    static thread_local std::pmr::unsynchronized_pool_resource mempool {
        std::pmr::pool_options {
            .max_blocks_per_chunk = 1024,
            .largest_required_pool_block = 65536,
        }
    };

    // For assertion on debug builds
    struct alignas(__STDCPP_DEFAULT_NEW_ALIGNMENT__) PoolHeader {
        std::thread::id id = std::this_thread::get_id();
    };

    static inline constexpr bool ENABLE_POOL_CHECK =
#if defined(NDEBUG) && !defined(ILIAS_CORO_TRACE)
        false;
#else
        true;
#endif

}

auto runtime::allocate(size_t size) -> void * {
    if constexpr (ENABLE_POOL_CHECK) {
        auto ptr = mempool.allocate(size + sizeof(PoolHeader), alignof(PoolHeader));
        new (ptr) PoolHeader {};
        return static_cast<std::byte *>(ptr) + sizeof(PoolHeader);
    }
    else {
        return mempool.allocate(size, alignof(PoolHeader));
    }
}

auto runtime::deallocate(void *ptr, size_t size) noexcept -> void {
    if constexpr (ENABLE_POOL_CHECK) {
        auto header = reinterpret_cast<PoolHeader *>(static_cast<std::byte *>(ptr) - sizeof(PoolHeader));
        if (header->id != std::this_thread::get_id()) [[unlikely]] {
            ILIAS_ERROR("Runtime", "Cross-thread deallocation detected !!!");
            ILIAS_TRAP();
            std::abort();
        }
        header->~PoolHeader();
        mempool.deallocate(header, size + sizeof(PoolHeader), alignof(PoolHeader));
    }
    else {
        return mempool.deallocate(ptr, size, alignof(PoolHeader));
    }
}
#else // Use system allocator
auto runtime::allocate(size_t size) -> void * { 
    return std::malloc(size); 
}

auto runtime::deallocate(void *ptr, size_t) noexcept -> void { 
    return std::free(ptr);
}
#endif // ILIAS_USE_MEMORY_POOL

ILIAS_NS_END