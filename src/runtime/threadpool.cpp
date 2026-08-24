#include <ilias/runtime/executor.hpp>
#include <ilias/runtime/token.hpp>
#include <ilias/log.hpp>
#include <condition_variable> // std::condition_variable
#include <thread> // std::thread
#include <queue> // std::queue
#include <mutex> // std::mutex

#if defined(_WIN32)
    #include <ilias/detail/win32defs.hpp>
#endif // _WIN32

ILIAS_NS_BEGIN

using namespace runtime;

auto threadpool::submit(CallableRef &callable) -> void {

#if defined(_WIN32) // Use win32 thread pool
    auto invoke = [](PTP_CALLBACK_INSTANCE instance, void *cb) -> void {
        auto callable = static_cast<CallableRef *>(cb);
        callable->invoke();
    };
    if (!::TrySubmitThreadpoolCallback(invoke, &callable, nullptr)) {
        ILIAS_THROW(std::system_error{std::error_code{static_cast<int>(::GetLastError()), std::system_category()}, "Faliled to submit to thread pool"});
    }
#else // Use our own thread pool
    struct ThreadPool {
        ThreadPool() : idle(1) {
            threads.emplace_back(&ThreadPool::worker, this, stopSource.get_token());
        }

        ~ThreadPool() {
            stopSource.request_stop();
            cond.notify_all();
            for (auto &thread : threads) {
                thread.join();
            }
        }
        // TODO: Maybe we need add worker quit, if it is idle too much
        auto dispatch(StopToken &token) -> void {
            while (true) {
                std::unique_lock locker{mutex};
                cond.wait(locker, [&]() {
                    return !queue.empty() || token.stop_requested();
                });
                if (token.stop_requested()) {
                    return;
                }
                auto *callable = queue.front();
                queue.pop();
                locker.unlock();
                
                // Execute the callable
                idle.fetch_sub(1, std::memory_order_relaxed); // -= 1
                callable->invoke();
                idle.fetch_add(1, std::memory_order_relaxed); // += 1
            }
        };

        auto worker(StopToken token) -> void {
            ILIAS_TRACE("Runtime", "Threadpool worker started");
            ::pthread_setname_np(::pthread_self(), "ilias::worker");
            dispatch(token);
            idle.fetch_sub(1, std::memory_order_relaxed); // The worker thread is exiting, so -= 1
            ILIAS_TRACE("Runtime", "Threadpool worker exiting");
        }

        StopSource stopSource; // for notifying the threads to stop
        std::queue<CallableRef *> queue;
        std::condition_variable cond;
        std::mutex mutex;
        std::vector<std::thread> threads;
        std::atomic<size_t> idle {0}; // number of idle threads
        std::size_t hw = std::thread::hardware_concurrency(); // number of machine's cpu
    };
    static ThreadPool pool;

    std::lock_guard locker{pool.mutex};
    if (pool.idle.load(std::memory_order_relaxed) == 0) {
        if (pool.threads.size() < pool.hw && pool.hw != 0) { // We can create more threads
            pool.threads.emplace_back(&ThreadPool::worker, &pool, pool.stopSource.get_token());
            pool.idle += 1;
        }
    }
    pool.queue.emplace(&callable);
    pool.cond.notify_one();
#endif
}

ILIAS_NS_END