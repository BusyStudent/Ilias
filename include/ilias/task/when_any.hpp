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

class WhenAnyAwaiterBase;
class WhenAnyTaskContext final : public TaskContext {
public:
    WhenAnyTaskContext(TaskHandle<> task) : TaskContext(task) { }

private:
    template <typename T>
    auto value() {
        return TaskHandle<T>::cast(mTask).value();
    }

    WhenAnyAwaiterBase *mAwaiter = nullptr;
friend class WhenAnyAwaiterBase;
template <typename ...Ts>
friend class WhenAnyAwaiter;
};

/**
 * @brief tag for when Any on tuple
 * 
 * @tparam Types 
 */
template <typename ...Types>
struct WhenAnyTuple final {
    std::array<WhenAnyTaskContext, sizeof ...(Types)> mTasks;
    CoroContext *mContext = nullptr; // The context of the caller

    // Set the context of the task, call on await_transform
    auto setContext(CoroContext &context) noexcept {
        mContext = &context;
    }
};

// The common part of when Any awaiter
class WhenAnyAwaiterBase {
public:
    WhenAnyAwaiterBase(std::span<WhenAnyTaskContext> tasks, CoroContext &context) : mTasks(tasks), mContext(context) {}

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
        return mGot && mLeft == 0; // All completed and one of them is got
    }

    auto await_suspend(CoroHandle caller) {
        mCaller = caller;
        mReg.register_<&WhenAnyAwaiterBase::onStopRequested>(caller.stopToken(), this); // Forward the stop if needed
    }
protected:
    auto stopAll() -> void {
        for (auto &ctxt : mTasks) {
            ctxt.stop();
        }
    }
    auto onStopRequested() -> void {
        mStopRequested = true;
        stopAll();
    }

    static auto onTaskCompleted(CoroContext &_ctxt) -> void {
        auto &ctxt = static_cast<WhenAnyTaskContext &>(_ctxt);
        auto &self = *ctxt.mAwaiter;

        if (!ctxt.isStopped()) { // Only not stopped task can be got (value produced)
            if (self.mGot == nullptr) {
                self.mGot = &ctxt; // The first completed task
                self.stopAll(); // Stop all other tasks
            }
        }

        self.mLeft -= 1;
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

    std::span<WhenAnyTaskContext> mTasks;
    WhenAnyTaskContext *mGot = nullptr;
    StopRegistration mReg;
    CoroContext &mContext;
    CoroHandle mCaller;
    size_t mLeft = 0;
    bool mStopRequested = false;
};

// The typed part, used to construct the result from the type-erased array
template <typename ...Ts>
class WhenAnyAwaiter final : public WhenAnyAwaiterBase {
public:
    WhenAnyAwaiter(std::span<WhenAnyTaskContext> tasks, CoroContext &context) : WhenAnyAwaiterBase(tasks, context) {}

    using Tuple = std::tuple<Option<Ts> ...>; // Using Option to replace void to std::monostate
    using RawTuple = std::tuple<Ts...>;

    auto await_resume() -> Tuple {
        ILIAS_ASSERT_MSG(mGot, "No value produced, but await_resume called?, ??? INTERNAL BUG");
        return makeResult(std::make_index_sequence<sizeof...(Ts)>{});
    }
private:
    template <size_t I>
    auto makeResult() -> std::tuple_element_t<I, Tuple> {
        using Raw = std::tuple_element_t<I, RawTuple>;
        if (mGot == &mTasks[I]) {
            return makeOption([&]() { // We replace void to std::monostate in Tuple, but in here, we should use TaskHandle<void>, not std::monostate
                return mTasks[I].template value<Raw>();
            });
        }
        return std::nullopt;
    }

    template <size_t ...Is>
    auto makeResult(std::index_sequence<Is...>) -> Tuple {
        return std::tuple{makeResult<Is>()...};
    }
};

template <typename ...Ts>
inline auto operator co_await(WhenAnyTuple<Ts...> &&tuple) noexcept {
    return WhenAnyAwaiter<Ts...>(tuple.mTasks, *tuple.mContext);
}

} // namespace task

/**
 * @brief When Any on multiple awaitable
 * 
 * @tparam Ts 
 * @param args 
 * @return The awaitable for when Any the given awaitable
 */
template <Awaitable ...Ts>
[[nodiscard]]
inline auto whenAny(Ts && ...args) noexcept {
    return task::WhenAnyTuple<AwaitableResult<Ts>... > { // Construct the task for the given awaitable
        {  task::WhenAnyTaskContext(toTask(std::forward<Ts>(args))._leak())... },
        nullptr // The context will be set in await_transform
    };
}

template <Awaitable T1, Awaitable T2>
[[nodiscard]]
inline auto operator ||(T1 &&a, T2 &&b) noexcept {
    return whenAny(std::forward<T1>(a), std::forward<T2>(b));
}

ILIAS_NS_END