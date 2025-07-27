#pragma once

#include <ilias/task/task.hpp>
#include <ilias/task/when_any.hpp>
#include <chrono>

ILIAS_NS_BEGIN

namespace task {

template <typename T>
class UnstoppableAwaiter {
public:
    UnstoppableAwaiter(TaskHandle<T> handle) : mHandle(handle), mAwaiter(handle) {}
    UnstoppableAwaiter(UnstoppableAwaiter &&) = default;

    auto await_ready() -> bool {
        mHandle.setContext(mCtxt); // Use the unstoppable context
        return mAwaiter.await_ready();
    }

    auto await_suspend(CoroHandle caller) {
        return mAwaiter.await_suspend(caller);
    }

    auto await_resume() -> T {
        return mAwaiter.await_resume();
    }

    // Set the context of the task, call on await_transform
    auto setContext(runtime::CoroContext &ctxt) {
        // We just need the executor info from the context
        mCtxt.setExecutor(ctxt.executor());
    }
private:
    runtime::CoroContext mCtxt {std::nostopstate};
    TaskHandle<T>  mHandle;
    TaskAwaiter<T> mAwaiter;
};

} // namespace task

// Set an timeout for a task, return nullopt on timeout
template <Awaitable T>
[[nodiscard]]
inline auto setTimeout(T awaitable, std::chrono::milliseconds ms) -> Task<AwaitableResult<T> > {
    auto [res, timeout] = co_await whenAny(std::move(awaitable), sleep(ms));
    if (timeout) {
        co_return std::nullopt;
    }
    co_return std::move(*res);
}

// Make a awaitable execute on an unstoppable context
template <Awaitable T>
[[nodiscard]]
inline auto unstoppable(T awaitable) -> task::UnstoppableAwaiter<AwaitableResult<T> > {
    auto handle = toTask(std::move(awaitable))._leak();
    return {handle};
}

ILIAS_NS_END