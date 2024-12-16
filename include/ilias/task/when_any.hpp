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
#include <tuple>


ILIAS_NS_BEGIN

namespace detail {

/**
 * @brief The tag for when any
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
    auto wait() const {
        return awaitableWrapperForward(*this).wait();
    }
};

// TODO: May bug here, need to check
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

    auto await_suspend(TaskView<> caller) -> void {
        ILIAS_TRACE("WhenAny", "[{}] Begin", sizeof ...(Types));
        // Register callback
        mCaller = caller;
        auto register_ = [&](auto ...tasks) {
            (tasks.registerCallback(
                std::bind(&WhenAnyTupleAwaiter::completeCallback, this, tasks)
            )
            , ...);
        };
        std::apply(register_, mTasks);

        // Register caller cancel callback
        mRegistration = caller.cancellationToken().register_(
            std::bind(&WhenAnyTupleAwaiter::cancelAll, this)
        );

        // Start all tasks
        auto start = [&](auto ...tasks) {
            (startTask(tasks), ...);
        };
        std::apply(start, mTasks);
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
            mCaller.schedule(); // Resume the caller
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
        task.resume();
        if (mGot && mGot != task) { //< Another task has already got the result, send a cancel
            task.cancel();
        }
    }

    InTuple mTasks;
    TaskView<> mCaller;
    TaskView<> mGot; // The task that got the result
    size_t mTaskLeft = sizeof...(Types);
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