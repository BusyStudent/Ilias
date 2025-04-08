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

#include <ilias/task/task.hpp>
#include <ilias/cancellation_token.hpp>
#include <ilias/log.hpp>
#include <optional>
#include <variant> //< For std::monostate
#include <vector>
#include <tuple>
#include <span>


ILIAS_NS_BEGIN

namespace detail {

/**
 * @brief The tag for when any on tuple
 * 
 * @tparam Types 
 */
template <typename ...Types>
struct WhenAnyTuple {
    std::tuple<Task<Types>...> mTuple;

    /**
     * @brief Blocking wait until the when any complete
     * 
     * @return std::tuple<std::optional<Types>...>
     */
    auto wait(ILIAS_CAPTURE_CALLER(loc)) const {
        return awaitableWrapperForward(*this).wait(loc);
    }

#if defined(ILIAS_TASK_TRACE)
    /**
     * @brief Trace the await point
     * 
     * @internal It is invoked when co_await is called, see more in Task<T>::_trace
     * @param caller 
     */
    auto _trace(CoroHandle caller) const {
        std::apply([&](auto &...task) {
            (task._trace(caller), ...);
        }, mTuple);
        caller.frame().msg = "whenAny";
    }
#endif // defined(ILIAS_TASK_TRACE)
};

/**
 * @brief The tag for when any on range (vector)
 * 
 * @tparam T 
 */
template <typename T>
struct WhenAnyRange {
    std::vector<Task<T> > mRange;

    /**
     * @brief Blocking wait until the when any complete
     * 
     * @return T
     */
    auto wait(ILIAS_CAPTURE_CALLER(loc)) const -> T {
        return awaitableWrapperForward(*this).wait(loc);
    }

#if defined(ILIAS_TASK_TRACE)
    /**
     * @brief Trace the await point
     * 
     * @internal It is invoked when co_await is called, see more in Task<T>::_trace
     * @param caller 
     */
    auto _trace(CoroHandle caller) const {
        for (auto &task : mRange) {
            task._trace(caller);
        }
        caller.frame().msg = "whenAny";
    }
#endif // defined(ILIAS_TASK_TRACE)
};

/**
 * @brief Awaiter for when any on a tuple
 * 
 * @tparam Types 
 */
template <typename ...Types>
class WhenAnyTupleAwaiter {
public:
    using InTuple = std::tuple<TaskView<Types>...>;
    using OutTuple = std::tuple<std::optional<
        std::conditional_t<!std::is_same_v<Types, void>, Types, std::monostate> //< Replace void to std::monostate
    >...>;

    WhenAnyTupleAwaiter(InTuple tasks) : mTasks(tasks) { }

    auto await_ready() -> bool {
        return false;
    }

    auto await_suspend(CoroHandle caller) -> void {
        ILIAS_TRACE("WhenAny", "[{}] Begin", sizeof ...(Types));
        // Register callback
        mCaller = caller;

        // Start all tasks
        auto start = [&](auto ...tasks) {
            (startTask(tasks), ...);
        };
        std::apply(start, mTasks);

        // Register caller cancel callback
        mRegistration = caller.cancellationToken().register_(
            std::bind(&WhenAnyTupleAwaiter::cancelAll, this)
        );
    }

    auto await_resume() -> OutTuple {
        ILIAS_ASSERT(mTaskLeft == 0);
        ILIAS_ASSERT(mGot);
        ILIAS_TRACE("WhenAny", "[{}] End", sizeof ...(Types));
        return makeResult(std::make_index_sequence<sizeof...(Types)>{});
    }
private:
    /**
     * @brief Get invoked when a task is completed
     * 
     * @param task 
     */
    auto completeCallback(TaskView<> task) -> void {
        ILIAS_TRACE("WhenAny", "[{}] Task {} completed, {} Left", sizeof ...(Types), task, mTaskLeft - 1);
        if (!mGot) {
            ILIAS_TRACE("WhenAny", "[{}] First task got the result is {}", sizeof ...(Types), task);
            mGot = task;
            cancelAll(); //< Cancel all another existing tasks
        }
        mTaskLeft -= 1;
        if (mTaskLeft == 0) {
            task.setAwaitingCoroutine(mCaller); // Let the current task resume the caller
            // mCaller.schedule();
        }
    }

    /**
     * @brief Callback used to cancel all sub tasks
     * 
     */
    auto cancelAll() -> void {
        ILIAS_TRACE("WhenAny", "[{}] Cancel all tasks", sizeof ...(Types));
        auto cancel = [](auto ...tasks) {
            (tasks.cancel(), ...);
        };
        std::apply(cancel, mTasks);
    }

    template <size_t I>
    auto makeResult() -> std::tuple_element_t<I, OutTuple> {
        using RetT = std::tuple_element_t<I, OutTuple>;
        auto task = std::get<I>(mTasks);
        if (task != mGot) {
            return std::nullopt;
        }
        // Check if the task return void, if so, replace it by std::monostate
        if constexpr(std::is_same_v<RetT, std::optional<std::monostate> >) {
            task.value(); //< Make sure the exception throw if the task has it
            return std::monostate {};
        }
        else {
            return task.value();
        }
    }

    template <size_t ...Idx>
    auto makeResult(std::index_sequence<Idx...>) -> OutTuple {
        return {makeResult<Idx>()...};
    }

    auto startTask(TaskView<> task) -> void {
        task.setExecutor(mCaller.executor());
        task.registerCallback(
            std::bind(&WhenAnyTupleAwaiter::completeCallback, this, task)
        );
        task.resume();
        if (mGot && mGot != task) { //< Another task has already got the result, send a cancel
            task.cancel();
        }
    }

    InTuple mTasks;
    CoroHandle mCaller;
    TaskView<> mGot; // The task that got the result
    size_t mTaskLeft = sizeof...(Types);
    CancellationToken::Registration mRegistration;
};

/**
 * @brief WhenAny on an vector of tasks
 * 
 * @tparam T 
 */
template <typename T>
class WhenAnyRangeAwaiter {
public:
    WhenAnyRangeAwaiter(std::span<const Task<T> > tasks) : mTasks(tasks), mTaskLeft(tasks.size()) { }

    auto await_ready() -> bool {
        return false;
    }

    auto await_suspend(CoroHandle caller) -> void {
        ILIAS_TRACE("WhenAny", "Range [{}] Begin", mTasks.size());
        // Register callback
        mCaller = caller;

        // Start all tasks
        for (auto &task : mTasks) {
            auto view = task._view();
            view.setExecutor(caller.executor());
            view.registerCallback(
                std::bind(&WhenAnyRangeAwaiter::completeCallback, this, view)
            ); // Register the completion callback, wait for the task to complete
            view.resume();
            if (mGot && mGot != view) { //< Another task has already got the result, send a cancel
                view.cancel();
            }
        }

        // Register caller cancel callback
        mRegistration = caller.cancellationToken().register_(
            std::bind(&WhenAnyRangeAwaiter::cancelAll, this)
        );
    }

    auto await_resume() -> T {
        ILIAS_TRACE("WhenAny", "Range [{}] End", mTasks.size());
        ILIAS_ASSERT(mGot);
        if constexpr (std::is_same_v<T, void>) {
            mGot.value(); //< Make sure the exception throw if the task has it
        }
        else {
            return mGot.value();
        }
    }
private:
    /**
     * @brief Get invoked when a task is completed
     * 
     * @param task 
     */
    auto completeCallback(TaskView<T> task) -> void {
        ILIAS_TRACE("WhenAny", "[{}] Task {} completed, {} Left", mTasks.size(), task, mTaskLeft - 1);
        if (!mGot) {
            ILIAS_TRACE("WhenAny", "[{}] First task got the result is {}", mTasks.size(), task);
            mGot = task;
            cancelAll(); //< Cancel all another existing tasks
        }
        mTaskLeft -= 1;
        if (mTaskLeft == 0) {
            task.setAwaitingCoroutine(mCaller); // Let the current task resume the caller
            // mCaller.schedule();
        }
    }

    /**
     * @brief Callback used to cancel all sub tasks
     * 
     */
    auto cancelAll() -> void {
        ILIAS_TRACE("WhenAny", "[{}] Cancel all tasks", mTasks.size());
        for (auto &task : mTasks) {
            task._view().cancel();
        }
    }

    std::span<const Task<T> > mTasks;
    size_t mTaskLeft;
    CoroHandle mCaller;
    TaskView<T> mGot;
    CancellationToken::Registration mRegistration;
};

/**
 * @brief Convert the WhenAnyTuple to awaiter
 * 
 * @tparam Types 
 * @param tuple 
 * @return auto 
 */
template <typename ...Types>
inline auto operator co_await(const WhenAnyTuple<Types...> &tuple) noexcept {
    auto views = std::apply([](auto &...tasks) { //< Convert the task to TaskView
        return std::tuple { tasks._view()... };
    }, tuple.mTuple);
    return WhenAnyTupleAwaiter<Types...>(views);
}

/**
 * @brief Convert the WhenAnyRange to awaiter
 * 
 * @tparam T 
 * @param range 
 * @return auto 
 */
template <typename T>
inline auto operator co_await(const WhenAnyRange<T> &range) noexcept {
    return WhenAnyRangeAwaiter<T>(range.mRange);
}

} // namespace detail


/**
 * @brief When Any on multiple awaitable
 * 
 * @tparam Types 
 * @param args 
 * @return The awaitable for waiting any of the given awaitable
 */
template <Awaitable ...Types>
inline auto whenAny(Types && ...args) noexcept {
    return detail::WhenAnyTuple<AwaitableResult<Types>...> { //< Construct the task for the given awaitable
        { Task<AwaitableResult<Types> >(std::forward<Types>(args))... }
    };
}

/**
 * @brief When Any on task vector
 * 
 * @tparam T 
 * @param tasks The tasks' vector (must not be empty)
 * @return auto 
 */
template <typename T>
inline auto whenAny(std::vector<Task<T> > &&tasks) noexcept {
    ILIAS_ASSERT(!tasks.empty());
    return detail::WhenAnyRange<T> { std::move(tasks) };
}

/**
 * @brief When Any on multiple awaitable
 * 
 * @tparam A 
 * @tparam B 
 * @param a The first awaitable
 * @param b The second awaitable
 * @return The awaitable for waiting any of the given awaitable 
 */
template <Awaitable A, Awaitable B>
inline auto operator ||(A && a, B && b) noexcept {
    using ResultA = AwaitableResult<A>;
    using ResultB = AwaitableResult<B>;
    return detail::WhenAnyTuple<ResultA, ResultB> {
        {
            Task<ResultA>(std::forward<A>(a)), 
            Task<ResultB>(std::forward<B>(b)) 
        }
    };
}

/**
 * @brief When Any on multiple awaitable
 * 
 * @tparam Types 
 * @tparam T 
 * @param tuple 
 * @param t 
 * @return The awaitable for waiting any of the given awaitable 
 */
template <typename ...Types, Awaitable T>
inline auto operator ||(detail::WhenAnyTuple<Types...> &&tuple, T && t) noexcept {
    return detail::WhenAnyTuple<Types..., AwaitableResult<T> > {
        std::tuple_cat(
            std::move(tuple.mTuple), 
            std::tuple { Task<AwaitableResult<T> >(std::forward<T>(t)) } //< Convert the awaitable to task
        )
    };
}


ILIAS_NS_END