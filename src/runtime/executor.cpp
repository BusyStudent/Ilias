#include <ilias/runtime/executor.hpp>
#include <ilias/runtime/timer.hpp>
#include <ilias/task/task.hpp>
#include <ilias/log.hpp>
#include <condition_variable> // std::condition_variable
#include <queue> // std::queue
#include <mutex> // std::mutex

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

ILIAS_NS_END