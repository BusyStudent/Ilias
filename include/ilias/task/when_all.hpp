#pragma once

#include <ilias/task/task.hpp>
#include <ilias/cancellation_token.hpp>
#include <ilias/log.hpp>
#include <tuple>

ILIAS_NS_BEGIN

namespace detail {

/**
 * @brief tag for when all
 * 
 * @tparam Types 
 */
template <typename ...Types>
struct WhenAllTuple {
    std::tuple<TaskView<Types> ...> mTuple;

    /**
     * @brief Blocking wait until the when all complete
     * 
     * @return std::tuple<Result<Types> ...> 
     */
    auto wait() const {
        auto helper = [this]() -> Task<std::tuple<Result<Types> ...> > {
            co_return co_await (*this);
        };
        return helper().wait().value();
    }
};

/**
 * @brief Awaiter for when all on a tuple
 * 
 * @tparam Types 
 */
template <typename ...Types>
class WhenAllTupleAwaiter {
public:
    using InTuple = std::tuple<TaskView<Types> ...>;
    using OutTuple = std::tuple<Result<Types> ...>;

    WhenAllTupleAwaiter(InTuple tasks) : mTasks(tasks) { }

    auto await_ready() noexcept -> bool { 
        return false;
    }

    auto await_suspend(TaskView<> caller) noexcept -> bool {
        ILIAS_TRACE("WhenAll", "[{}] Begin", sizeof ...(Types));
        // Start all tasks
        auto executor = caller.executor();
        auto start = [&](auto ...tasks) {
            ((tasks.setExecutor(executor), tasks.resume()), ...);
        };
        std::apply(start, mTasks);

        // Check if all tasks are done
        auto check = [this](auto ...tasks) {
            ((tasks.done() ? mTaskLeft-- : 0), ...);
        };
        std::apply(check, mTasks);
        if (mTaskLeft == 0) {
            return false; //< All tasks are done, resume the caller
        }

        // Save the status for cancel and resume
        mCaller = caller;
        mRegistration = caller.cancellationToken().register_(
            std::bind(&WhenAllTupleAwaiter::cancelAll, this)
        );

        // Register completion callbacks
        auto register_ = [&](auto ...tasks) {
            (tasks.registerCallback(
                std::bind(&WhenAllTupleAwaiter::completeCallback, this, tasks)
            ), ...);
        };
        std::apply(register_, mTasks);
        return true;
    }

    auto await_resume() -> OutTuple {
        ILIAS_TRACE("WhenAll", "[{}] End", sizeof ...(Types));
        ILIAS_ASSERT(mTaskLeft == 0);
        return makeResult(std::make_index_sequence<sizeof...(Types)>{});
    }
private:
    /**
     * @brief Get invoked when a task is completed
     * 
     * @param task 
     */
    auto completeCallback(TaskView<> task) -> void {
        ILIAS_TRACE("WhenAll", "[{}] Task {} completed, {} Left", sizeof ...(Types), task, mTaskLeft - 1);
        mTaskLeft -= 1;
        if (mTaskLeft == 0) {
            mCaller.schedule(); // Resume the caller
        }
    }

    auto cancelAll() {
        auto cancel = [](auto ...tasks) {
            (tasks.cancel(), ...);
        };
        std::apply(cancel, mTasks);
    }

    template <size_t I>
    auto makeResult() -> std::tuple_element_t<I, OutTuple> {
        auto task = std::get<I>(mTasks);
        return task.value();
    }

    template <size_t ...Idx>
    auto makeResult(std::index_sequence<Idx...>) -> OutTuple {
        return {makeResult<Idx>()...};
    }

    InTuple mTasks;
    TaskView<> mCaller;
    size_t mTaskLeft = sizeof...(Types);
    CancellationToken::Registration mRegistration;
};

/**
 * @brief Convert
 * 
 * @tparam Types 
 * @param tuple 
 * @return auto 
 */
template <typename ...Types>
inline auto operator co_await(WhenAllTuple<Types...> tuple) noexcept {
    return WhenAllTupleAwaiter<Types...>(tuple.mTuple);
}

} // namespace detail


/**
 * @brief When All on multiple tasks
 * 
 * @tparam Types 
 * @param args 
 * @return auto 
 */
template <typename ...Types>
inline auto whenAll(Task<Types> && ...args) noexcept {
    return detail::WhenAllTuple<Types...> {
        {args._view()...}
    };
}

template <typename A, typename B>
inline auto operator &&(Task<A> && a, Task<B> && b) noexcept {
    return detail::WhenAllTuple<A, B> {
        {a._view(), b._view()}
    };
}

template <typename ...Types, typename T>
inline auto operator &&(detail::WhenAllTuple<Types...> && tuple, Task<T> && t) noexcept {
    return detail::WhenAllTuple<Types..., T> {
        std::tuple_cat(tuple.mTuple, std::make_tuple(t._view()))
    };
}

ILIAS_NS_END