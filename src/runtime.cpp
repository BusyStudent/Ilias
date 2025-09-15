#include <ilias/runtime/executor.hpp>
#include <ilias/runtime/timer.hpp>
#include <ilias/runtime/coro.hpp>
#include <ilias/task/task.hpp>
#include <condition_variable> // std::condition_variable
#include <memory_resource> // std::pmr::memory_resource
#include <system_error> // std::system_error
#include <thread> // std::thread
#include <queue> // std::queue
#include <mutex> // std::mutex

#if defined(_WIN32)
    #include <ilias/detail/win32defs.hpp>
#endif // _WIN32


ILIAS_NS_BEGIN

using namespace runtime;

// Executor
static constinit thread_local Executor *currentExecutor = nullptr;

Executor::~Executor() {
    uninstall();
}

auto Executor::currentThread() -> Executor * {
    return currentExecutor;
}

auto Executor::install() -> void {
    if (currentExecutor && currentExecutor != this) {
#if !defined(NDEBUG)
        ::fprintf(stderr, "A different executor already installed\n");
#endif // !NDEBUG
        ILIAS_THROW(std::runtime_error("A different executor already installed"));
    }
    currentExecutor = this;
}

auto Executor::uninstall() -> void {
    if (currentExecutor == this) {
        currentExecutor = nullptr;
    }
}

// EventLoop
struct EventLoop::Impl {
    std::queue<std::pair<void (*)(void *), void *> > queue;
    std::condition_variable cond;
    std::mutex mutex;
    TimerService service;
};

EventLoop::EventLoop() : d(std::make_unique<Impl>()) {}
EventLoop::~EventLoop() = default;

auto EventLoop::post(void (*fn)(void *), void *args) -> void {
    std::lock_guard locker(d->mutex);
    d->queue.emplace(fn, args);
    d->cond.notify_one();
}

auto EventLoop::run(StopToken token) -> void {
    StopCallback callback(token, [&]() {
        d->cond.notify_one();
    });
    while (!token.stop_requested()) {
        std::unique_lock locker(d->mutex);
        auto timepoint = d->service.nextTimepoint();
        if (!timepoint) {
            timepoint = std::chrono::steady_clock::now() + std::chrono::hours(60);
        }
        d->cond.wait_until(locker, *timepoint, [&]() {
            return !d->queue.empty() || token.stop_requested();
        });
        if (token.stop_requested()) {
            return;
        }
        if (!d->queue.empty()) {
            auto fn = d->queue.front();
            d->queue.pop();
            locker.unlock();
            fn.first(fn.second);
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

auto threadpool::submit(Callable *callable) -> void {
    ILIAS_ASSERT_MSG(callable, "Callable must not be null");

#if defined(_WIN32)
    auto invoke = [](void *cb) -> ::DWORD {
        auto callable = static_cast<Callable *>(cb);
        callable->call(*callable);
        return 0;
    };
    if (!::QueueUserWorkItem(invoke, callable, WT_EXECUTEDEFAULT)) {
        ILIAS_THROW(std::system_error(std::error_code(GetLastError(), std::system_category())));
    }
#else // Use our own thread pool
    struct ThreadPool {
        StopSource stopSource; // for notifying the threads to stop
        std::queue<Callable *> queue;
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
            callable->call(*callable);
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
    pool->queue.emplace(callable);
    pool->cond.notify_one();
#endif
}

// Memory Pool
auto runtime::allocate(size_t size) -> void * {
    return ::malloc(size);
}

auto runtime::deallocate(void *ptr, size_t size) noexcept -> void {
    return ::free(ptr);
}

ILIAS_NS_END