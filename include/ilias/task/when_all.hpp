#pragma once

#include <ilias/runtime/executor.hpp>
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

class WhenAllAwaiterBase;
class WhenAllTaskContext final : public TaskContext {
public:
    WhenAllTaskContext(TaskHandle<> task) : TaskContext(task) { }

private:
    template <typename T>
    auto value() {
        return TaskHandle<T>::cast(mTask).value();
    }

    WhenAllAwaiterBase *mAwaiter = nullptr;
friend class WhenAllAwaiterBase;
template <typename ...Ts>
friend class WhenAllAwaiter;
};

/**
 * @brief tag for when all on tuple
 * 
 * @tparam Types 
 */
template <typename ...Types>
struct WhenAllTuple final {
    std::array<WhenAllTaskContext, sizeof ...(Types)> mTasks;
    CoroContext *mContext = nullptr; // The context of the caller

    // Set the context of the task, call on await_transform
    auto setContext(CoroContext &context) noexcept {
        mContext = &context;
    }
};

// The common part of when all awaiter
class WhenAllAwaiterBase {
public:
    WhenAllAwaiterBase(std::span<WhenAllTaskContext> tasks, CoroContext &context) : mTasks(tasks), mContext(context) {}

    auto await_ready() -> bool {
        // Start all first
        mLeft = mTasks.size();
        for (auto &ctxt: mTasks) {
            ctxt.mAwaiter = this;
            ctxt.setExecutor(mContext.executor());
            ctxt.setStoppedHandler(&onTaskCompleted);
            ctxt.task().setCompletionHandler(&onTaskCompleted);
            ctxt.task().resume();
        }
        return mLeft == 0;
    }

    auto await_suspend(CoroHandle caller) {
        mCaller = caller;
        mReg.register_<&WhenAllAwaiterBase::onStopRequested>(caller.stopToken(), this); // Forward the stop if needed
    }
protected:
    auto onStopRequested() -> void {
        mStopRequested = true;
        for (auto &ctxt : mTasks) {
            ctxt.stop();
        }
    }

    static auto onTaskCompleted(CoroContext &_ctxt) -> void {
        auto &ctxt = static_cast<WhenAllTaskContext &>(_ctxt);
        auto &self = *ctxt.mAwaiter;
        self.mLeft -= 1;

        if (self.mLeft != 0) {
            return; // Still has some imcomplete tasks
        }
        if (self.mStopRequested) { // The stop was requested, and all tasks are completed, we enter the stopped state
            self.mCaller.setStopped();
            return;
        }
        if (self.mCaller) {
            ctxt.task().setPrevAwaiting(self.mCaller); // Use the current completed task to resume the caller
        }
    }

    std::span<WhenAllTaskContext> mTasks;
    StopRegistration mReg;
    CoroContext &mContext;
    CoroHandle mCaller;
    size_t mLeft = 0;
    bool mStopRequested = false;
};

// The typed part, used to construct the result from the type-erased array
template <typename ...Ts>
class WhenAllAwaiter final : public WhenAllAwaiterBase {
public:
    WhenAllAwaiter(std::span<WhenAllTaskContext> tasks, CoroContext &context) : WhenAllAwaiterBase(tasks, context) {}

    using Tuple = std::tuple<typename Option<Ts>::value_type ...>; // Using Option to replace void to std::monostate
    using RawTuple = std::tuple<Ts...>;

    auto await_resume() -> Tuple {
        return makeResult(std::make_index_sequence<sizeof...(Ts)>{});
    }
private:
    template <size_t I>
    auto makeResult() -> std::tuple_element_t<I, Tuple> {
        using Raw = std::tuple_element_t<I, RawTuple>;
        return *makeOption([&]() {
            // We replace void to std::monostate in Tuple, but in here, we should use TaskHandle<void>, not std::monostate
            return mTasks[I].template value<Raw>();
        });
    }

    template <size_t ...Is>
    auto makeResult(std::index_sequence<Is...>) -> Tuple {
        return {makeResult<Is>()...};
    }
};

template <typename ...Ts>
inline auto operator co_await(WhenAllTuple<Ts...> &&tuple) noexcept {
    return WhenAllAwaiter<Ts...>(tuple.mTasks, *tuple.mContext);
}

} // namespace task

/**
 * @brief When All on multiple awaitable
 * 
 * @tparam Ts 
 * @param args 
 * @return The awaitable for when all the given awaitable
 */
template <Awaitable ...Ts>
[[nodiscard]]
inline auto whenAll(Ts && ...args) noexcept {
    return task::WhenAllTuple<AwaitableResult<Ts>... > { // Construct the task for the given awaitable
        {  task::WhenAllTaskContext(toTask(std::forward<Ts>(args))._leak())... },
        nullptr // The context will be set in await_transform
    };
}

// Logical and operator for awaitable, same as whenAll
template <Awaitable T1, Awaitable T2>
[[nodiscard]]
inline auto operator &&(T1 &&a, T2 &&b) noexcept {
    return whenAll(std::forward<T1>(a), std::forward<T2>(b));
}

ILIAS_NS_END