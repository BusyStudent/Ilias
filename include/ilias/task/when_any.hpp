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
 * @brief tag for when Any on tuple
 * 
 * @tparam Types 
 */
template <typename ...Types>
struct [[ILIAS_CORO_AWAIT_ELIDABLE]] WhenAnyTuple final {
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
struct [[ILIAS_CORO_AWAIT_ELIDABLE]] WhenAnySequence final {
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

// The common part of when Any awaiter
class WhenAnyAwaiterBase {
public:
    WhenAnyAwaiterBase(std::span<TaskContext> tasks, CoroContext &context, CaptureSource source) : mTasks(tasks), mContext(context), mSource(source) {}

    ILIAS_API
    auto await_ready() -> bool;

    ILIAS_API
    auto await_suspend(CoroHandle caller) -> void;
protected:
    auto stopAll() -> void; // Stop all the tasks
    static auto onTaskCompleted(CoroContext &_ctxt) -> void;

    std::span<TaskContext> mTasks;
    TaskContext *mGot = nullptr;
    StopRegistration mReg;
    CoroContext &mContext;
    CoroHandle mCaller;
    [[ILIAS_NO_UNIQUE_ADDRESS]]
    CaptureSource mSource; // The source location of the await point
    size_t mLeft = 0;    // The number of the task still not completed
    size_t mStarted = 0; // The bound of the started tasks
    bool mStopRequested = false;
    bool mSuspended = false;
};

// The typed part, used to construct the result from the type-erased array
template <typename ...Ts>
class WhenAnyAwaiter final : public WhenAnyAwaiterBase {
public:
    using WhenAnyAwaiterBase::WhenAnyAwaiterBase; // Inherit the constructor

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

// The typed part
template <typename T>
class WhenAnySequenceAwaiter final : public WhenAnyAwaiterBase {
public:
    using WhenAnyAwaiterBase::WhenAnyAwaiterBase; // Inherit the constructor

    auto await_resume() {
        ILIAS_ASSERT(mGot, "No value produced, but await_resume called?, ??? INTERNAL BUG");
        return TaskHandle<T>::cast(mGot->task()).value();
    }
};

template <typename ...Ts>
inline auto operator co_await(WhenAnyTuple<Ts...> &&tuple) noexcept {
    return WhenAnyAwaiter<Ts...> {tuple.mTasks, *tuple.mContext, tuple.mSource};
}

template <typename T>
inline auto operator co_await(WhenAnySequence<T> &&seq) noexcept {
    return WhenAnySequenceAwaiter<T> {seq.mTasks, *seq.mContext, seq.mSource};
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
inline auto whenAny([[ILIAS_CORO_ELIDABLE_ARGUMENT]] Ts && ...args) {
    return task::WhenAnyTuple<AwaitableResult<Ts>... > { // Construct the task for the given awaitable
        .mTasks = { task::TaskContext {toTask(std::forward<Ts>(args))._leak()}... },
        .mContext = nullptr // The context will be set in await_transform
    };
}

/**
 * @brief When any of the tasks in the sequence is completed, return the result of the task.
 * 
 * @tparam T 
 * @param seq The sequence of tasks. (can't be empty)
 * @return The value of the task that has been completed first.
 */
template <AwaitableSequence T>
[[nodiscard]]
inline auto whenAny([[ILIAS_CORO_ELIDABLE_ARGUMENT]] T seq) {
    std::vector<task::TaskContext> tasks{};
    if constexpr (std::ranges::sized_range<T>) {
        tasks.reserve(std::ranges::size(seq));
    }
    for (auto &awaitable : seq) { // Construct the task for the given awaitable to an vector
        tasks.emplace_back(toTask(std::move(awaitable))._leak());
    }
    if (tasks.empty()) { // Contract violation
        ILIAS_THROW(std::invalid_argument{"Can't whenAny on empty sequence"});
    }
    return task::WhenAnySequence<AwaitableSequenceValue<T> > {
        .mTasks = std::move(tasks),
        .mContext = nullptr,
    };
}

template <Awaitable T1, Awaitable T2>
[[nodiscard]]
inline auto operator ||(
    [[ILIAS_CORO_ELIDABLE_ARGUMENT]] T1 &&a,
    [[ILIAS_CORO_ELIDABLE_ARGUMENT]] T2 &&b) 
{
    return whenAny(std::forward<T1>(a), std::forward<T2>(b));
}

ILIAS_NS_END