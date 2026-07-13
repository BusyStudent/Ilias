#pragma once

#include <ilias/runtime/executor.hpp>
#include <ilias/runtime/await.hpp>
#include <ilias/runtime/coro.hpp>
#include <ilias/task/task.hpp>
#include <ilias/log.hpp>
#include <variant> // std::monostate
#include <vector> // std::vector
#include <ranges> // std::range
#include <array> // std::array
#include <tuple> // std::tuple
#include <span> // std::span

ILIAS_NS_BEGIN

namespace task {

using runtime::CoroContext;
using runtime::CoroHandle;
using runtime::StopRegistration;
using runtime::CaptureSource;

/**
 * @brief tag for when all on tuple
 * 
 * @tparam Types 
 */
template <typename ...Types>
struct [[ILIAS_CORO_AWAIT_ELIDABLE]] WhenAllTuple final {
    std::array<TaskContext, sizeof ...(Types)> mTasks;
    CoroContext  *mContext = nullptr; // The context of the caller

    [[ILIAS_NO_UNIQUE_ADDRESS]]
    CaptureSource mSource; // The source location of the await point

    // Set the context of the task, call on await_transform
    auto setContext(CoroContext &context, CaptureSource source) noexcept {
        mContext = &context;
        mSource = source;
    }
};

/**
 * @brief tags for when all on sequence
 * 
 * @tparam T 
 */
template <typename T>
struct [[ILIAS_CORO_AWAIT_ELIDABLE]] WhenAllSequence final {
    // As same as tuple version
    std::vector<TaskContext> mTasks;
    CoroContext  *mContext = nullptr; 

    [[ILIAS_NO_UNIQUE_ADDRESS]]
    CaptureSource mSource;

    auto setContext(CoroContext &context, CaptureSource source) noexcept {
        mContext = &context;
        mSource = source;
    }
};

// The common part of when all awaiter
class WhenAllAwaiterBase {
public:
    WhenAllAwaiterBase(std::span<TaskContext> tasks, CoroContext &context, CaptureSource source) : mTasks(tasks), mContext(context), mSource(source) {}

    auto await_ready() -> bool {
        // Start all first
#if defined(ILIAS_CORO_TRACE)
        // TRACING: mark the current await point we are on whenAll
        if (auto frame = mContext.topFrame(); frame) {
            frame->setMessage("whenAll");
        }
#endif // defined(ILIAS_CORO_TRACE)
        mLeft = mTasks.size();
        for (auto &ctxt: mTasks) {
            ctxt.setUserdata(this);
            ctxt.setParent(mContext);
            ctxt.setExecutor(mContext.executor());
            ctxt.setStoppedHandler(&onTaskCompleted);
            ctxt.task().setCompletionHandler(&onTaskCompleted);

            // TRACING: a subtask is started
            ctxt.tracingSpawn(mSource);
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
        auto &ctxt = static_cast<TaskContext &>(_ctxt);
        auto &self = *static_cast<WhenAllAwaiterBase *>(ctxt.userdata());
        self.mLeft -= 1;

        // TRACING: a subtask is completed
        ctxt.tracingComplete();
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

    std::span<TaskContext> mTasks;
    StopRegistration mReg;
    CoroContext &mContext;
    CoroHandle mCaller;
    [[ILIAS_NO_UNIQUE_ADDRESS]]
    CaptureSource mSource; // The source location of the await point
    size_t mLeft = 0;
    bool mStopRequested = false;
};

// The typed part, used to construct the result from the type-erased array
template <typename ...Ts>
class WhenAllAwaiter final : public WhenAllAwaiterBase {
public:
    using WhenAllAwaiterBase::WhenAllAwaiterBase; // Inherit the constructor

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
            auto handle = TaskHandle<Raw>::cast(mTasks[I].task());
            return handle.value();
        });
    }

    template <size_t ...Is>
    auto makeResult(std::index_sequence<Is...>) -> Tuple {
        return {makeResult<Is>()...};
    }
};

// The typed part
template <typename T>
class WhenAllSequenceAwaiter final : public WhenAllAwaiterBase {
public:
    using WhenAllAwaiterBase::WhenAllAwaiterBase; // Inherit the constructor

    using Vector = std::vector<typename Option<T>::value_type>; // Using Option to replace void to std::monostate

    auto await_resume() -> Vector {
        Vector v;
        v.reserve(mTasks.size());
        for (auto &ctxt : mTasks) {
            v.emplace_back(*makeOption([&]() {
                auto handle = TaskHandle<T>::cast(ctxt.task());
                return handle.value();
            }));
        }
        return v;
    }
};

template <typename ...Ts>
inline auto operator co_await(WhenAllTuple<Ts...> &&tuple) noexcept {
    return WhenAllAwaiter<Ts...> {tuple.mTasks, *tuple.mContext, tuple.mSource};
}

template <typename T>
inline auto operator co_await(WhenAllSequence<T> &&seq) noexcept {
    return WhenAllSequenceAwaiter<T> {seq.mTasks, *seq.mContext, seq.mSource};
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
inline auto whenAll([[ILIAS_CORO_ELIDABLE_ARGUMENT]] Ts && ...args) {
    return task::WhenAllTuple<AwaitableResult<Ts>... > { // Construct the task for the given awaitable
        .mTasks = { task::TaskContext {toTask(std::forward<Ts>(args))._leak()}... },
        .mContext = nullptr // The context will be set in await_transform
    };
}

/**
 * @brief When all of the tasks in the sequence is completed, return the result of all tasks.
 * 
 * @tparam T 
 * @param seq The sequence of tasks. (can be empty)
 * @return The vector of the result of all tasks.
 */
template <AwaitableSequence T>
[[nodiscard]]
inline auto whenAll([[ILIAS_CORO_ELIDABLE_ARGUMENT]] T seq) {
    std::vector<task::TaskContext> tasks{};
    if constexpr (std::ranges::sized_range<T>) {
        tasks.reserve(std::ranges::size(seq));
    }
    for (auto &awaitable : seq) { // Construct the task for the given awaitable to an vector
        tasks.emplace_back(toTask(std::move(awaitable))._leak());
    }
    return task::WhenAllSequence<AwaitableSequenceValue<T> > {
        .mTasks = std::move(tasks),
        .mContext = nullptr,
    };
}

// Logical and operator for awaitable, same as whenAll
template <Awaitable T1, Awaitable T2>
[[nodiscard]]
inline auto operator &&(
    [[ILIAS_CORO_ELIDABLE_ARGUMENT]] T1 &&a, 
    [[ILIAS_CORO_ELIDABLE_ARGUMENT]] T2 &&b) 
{
    return whenAll(std::forward<T1>(a), std::forward<T2>(b));
}

ILIAS_NS_END