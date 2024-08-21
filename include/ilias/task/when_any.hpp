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
    std::tuple<TaskView<Types>...> mTuple;

    /**
     * @brief Blocking wait until the when any complete
     * 
     * @return std::tuple<std::optional<Result<Types> >...> 
     */
    auto wait() -> std::tuple<std::optional<Result<Types> >...> {
        auto helper = [this]() -> Task<std::tuple<std::optional<Result<Types> >...> > {
            co_return co_await (*this);
        };
        return helper().wait().value();
    }
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
    using OutTuple = std::tuple<std::optional<Result<Types> >...>;

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
            ), ...);
        };
        std::apply(register_, mTasks);

        // Register caller cancel callback
        mRegistration = caller.cancellationToken().register_(
            std::bind(&WhenAnyTupleAwaiter::cancelAll, this)
        );

        // Start all tasks
        auto start = [](auto ...tasks) {
            (tasks.resume(), ...);
        };
        std::apply(start, mTasks);
    }

    auto await_resume() -> OutTuple {
        ILIAS_ASSERT(mTaskLeft == 0);
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
            mCaller.executor()->schedule(mCaller); // Resume the caller
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
        auto task = std::get<I>(mTasks);
        if (task == mGot) {
            return task.value();
        }
        return std::nullopt;
    }

    template <size_t ...Idx>
    auto makeResult(std::index_sequence<Idx...>) -> OutTuple {
        return {makeResult<Idx>()...};
    }

    InTuple mTasks;
    TaskView<> mCaller;
    TaskView<> mGot; // The task that got the result
    size_t mTaskLeft = sizeof...(Types);
    CancellationToken::Registration mRegistration;
};

/**
 * @brief Convert the request to a WhenAnyTuple
 * 
 * @tparam Types 
 * @param tuple 
 * @return auto 
 */
template <typename ...Types>
inline auto operator co_await(WhenAnyTuple<Types...> tuple) {
    return WhenAnyTupleAwaiter<Types...>(tuple.mTuple);
}

} // namespace detail


/**
 * @brief When Any on multiple tasks
 * 
 * @tparam Types 
 * @param args 
 * @return auto 
 */
template <typename ...Types>
inline auto whenAny(Task<Types> && ...args) noexcept {
    return detail::WhenAnyTuple<Types...> {
        {args._view()...}
    };
}

template <typename A, typename B>
inline auto operator ||(Task<A> && a, Task<B> && b) noexcept {
    return detail::WhenAnyTuple<A, B> {
        {a._view(), b._view()}
    };
}

template <typename ...Types, typename T>
inline auto operator ||(detail::WhenAnyTuple<Types...> tuple, Task<T> && t) noexcept {
    return detail::WhenAnyTuple<Types..., T> {
        std::tuple_cat(tuple.mTuple, std::make_tuple(t._view()))
    };
}


ILIAS_NS_END