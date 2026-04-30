/**
 * @file when_any.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Impl the WhenAny
 * @version 0.1
 * @date 2024-08-18
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/runtime/executor.hpp>
#include <ilias/runtime/tracing.hpp>
#include <ilias/runtime/await.hpp>
#include <ilias/runtime/coro.hpp>
#include <ilias/task/task.hpp>
#include <ilias/log.hpp>
#include <variant> // std::monostate
#include <array> // std::array
#include <tuple> // std::tuple
#include <span> // std::span


ILIAS_NS_BEGIN

namespace task {

using runtime::CoroContext;
using runtime::CoroHandle;
using runtime::StopRegistration;

/**
 * @brief tag for when Any on tuple
 * 
 * @tparam Types 
 */
template <typename ...Types>
struct WhenAnyTuple final {
    std::array<TaskContext, sizeof ...(Types)> mTasks;
    CoroContext *mContext = nullptr; // The context of the caller

    // Set the context of the task, call on await_transform
    auto setContext(CoroContext &context) noexcept {
        mContext = &context;
    }
};

// The common part of when Any awaiter
class WhenAnyAwaiterBase {
public:
    WhenAnyAwaiterBase(std::span<TaskContext> tasks, CoroContext &context) : mTasks(tasks), mContext(context) {}

    auto await_ready() -> bool {
        // Start all first
#if defined(ILIAS_CORO_TRACE)
        // TRACING: mark the current await point we are on whenAny
        if (auto frame = mContext.topFrame(); frame) {
            frame->setMessage("whenAny");
        }
#endif // defined(ILIAS_CORO_TRACE)
        mLeft = 0;
        for (auto &ctxt: mTasks) {
            ctxt.setUserdata(this);
            ctxt.setParent(mContext);
            ctxt.setExecutor(mContext.executor());
            ctxt.setStoppedHandler(&onTaskCompleted);
            ctxt.task().setCompletionHandler(&onTaskCompleted);

            // TRACING: a subtask is started
            runtime::tracing::childBegin(ctxt);
            mLeft += 1;
            mStarted += 1;
            ctxt.task().resume();

            if (mGot) {
                break;
            }
        }
        return mGot && mLeft == 0; // All completed and one of them is got
    }

    auto await_suspend(CoroHandle caller) {
        mSuspended = true;
        mCaller = caller;
        mReg.register_<&WhenAnyAwaiterBase::onStopRequested>(caller.stopToken(), this); // Forward the stop if needed
    }
protected:
    auto stopAll() -> void {
        for (size_t idx = 0; idx < mStarted; ++idx) { // Stop the started tasks
            mTasks[idx].stop();
        }
    }
    auto onStopRequested() -> void {
        mStopRequested = true;
        stopAll();
    }

    static auto onTaskCompleted(CoroContext &_ctxt) -> void {
        auto &ctxt = static_cast<TaskContext &>(_ctxt);
        auto &self = *static_cast<WhenAnyAwaiterBase *>(ctxt.userdata());

        // TRACING: a subtask is completed
        runtime::tracing::childEnd(ctxt);
        if (!ctxt.isStopped()) { // Only not stopped task can be got (value produced)
            if (self.mGot == nullptr) {
                self.mGot = &ctxt; // The first completed task
                self.stopAll(); // Stop all other tasks
            }
        }

        self.mLeft -= 1;
        if (!self.mSuspended) { // Still in await_ready
            return;
        }
        if (self.mLeft != 0) {
            return; // Still has some imcomplete tasks
        }
        if (self.mStopRequested && !self.mGot) { // The stop was requested, and all tasks are completed, no value produced, we enter the stopped state
            self.mCaller.setStopped();
            return;
        }
        if (self.mCaller) {
            self.mCaller.schedule();
            self.mCaller = nullptr;
        }
    }

    std::span<TaskContext> mTasks;
    TaskContext *mGot = nullptr;
    StopRegistration mReg;
    CoroContext &mContext;
    CoroHandle mCaller;
    size_t mLeft = 0;    // The number of the task still not completed
    size_t mStarted = 0; // The bound of the started tasks
    bool mStopRequested = false;
    bool mSuspended = false;
};

// The typed part, used to construct the result from the type-erased array
template <typename ...Ts>
class WhenAnyAwaiter final : public WhenAnyAwaiterBase {
public:
    WhenAnyAwaiter(std::span<TaskContext> tasks, CoroContext &context) : WhenAnyAwaiterBase(tasks, context) {}

    using Tuple = std::tuple<Option<Ts> ...>; // Using Option to replace void to std::monostate
    using RawTuple = std::tuple<Ts...>;

    auto await_resume() -> Tuple {
        ILIAS_ASSERT(mGot, "No value produced, but await_resume called?, ??? INTERNAL BUG");
        return makeResult(std::make_index_sequence<sizeof...(Ts)>{});
    }
private:
    template <size_t I>
    auto makeResult() -> std::tuple_element_t<I, Tuple> {
        using Raw = std::tuple_element_t<I, RawTuple>;
        if (mGot == &mTasks[I]) {
            return makeOption([&]() { // We replace void to std::monostate in Tuple, but in here, we should use TaskHandle<void>, not std::monostate
                auto handle = TaskHandle<Raw>::cast(mTasks[I].task());
                return handle.value();
            });
        }
        return std::nullopt;
    }

    template <size_t ...Is>
    auto makeResult(std::index_sequence<Is...>) -> Tuple {
        return std::tuple {makeResult<Is>()...};
    }
};

template <typename ...Ts>
inline auto operator co_await(WhenAnyTuple<Ts...> &&tuple) noexcept {
    return WhenAnyAwaiter<Ts...> {tuple.mTasks, *tuple.mContext};
}

} // namespace task

/**
 * @brief When Any on multiple awaitable
 * @note whenAny starts children from left to right. 
 *       If one child completes synchronously during startup, later children may not be started. 
 *       Already-started children are requested to stop and are awaited before whenAny completes.
 * 
 * @tparam Ts 
 * @param args 
 * @return The awaitable for when Any the given awaitable
 */
template <Awaitable ...Ts> requires(sizeof...(Ts) > 0)
[[nodiscard]]
inline auto whenAny(Ts && ...args) noexcept {
    return task::WhenAnyTuple<AwaitableResult<Ts>... > { // Construct the task for the given awaitable
        {  task::TaskContext {toTask(std::forward<Ts>(args))._leak()}... },
        nullptr // The context will be set in await_transform
    };
}

template <Awaitable T1, Awaitable T2>
[[nodiscard]]
inline auto operator ||(T1 &&a, T2 &&b) noexcept {
    return whenAny(std::forward<T1>(a), std::forward<T2>(b));
}

ILIAS_NS_END