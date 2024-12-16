#pragma once

#include <ilias/task/task.hpp>
#include <ilias/cancellation_token.hpp>
#include <ilias/log.hpp>
#include <variant> //< For std::monostate
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
    std::tuple<Task<Types> ...> mTuple;

    /**
     * @brief Blocking wait until the when all complete
     * 
     * @return std::tuple<Types...> 
     */
    auto wait() const {
        return awaitableWrapperForward(*this).wait();
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
    using OutTuple = std::tuple<
        std::conditional_t<!std::is_same_v<Types, void>, Types, std::monostate> //< Replace void to std::monostate
    ...>;

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
        using RetT = std::tuple_element_t<I, OutTuple>;
        auto task = std::get<I>(mTasks);
        // Check if the task return void, if so, replace it by std::monostate
        if constexpr(std::is_same_v<RetT, std::monostate>) {
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

    InTuple mTasks;
    TaskView<> mCaller;
    size_t mTaskLeft = sizeof...(Types);
    CancellationToken::Registration mRegistration;
};

/**
 * @brief Convert WhenAllTuple to awaiter
 * 
 * @tparam Types 
 * @param tuple 
 * @return auto 
 */
template <typename ...Types>
inline auto operator co_await(const WhenAllTuple<Types...> &tuple) noexcept {
    auto views = std::apply([](auto &...tasks) { //< Convert the task to TaskView
        return std::tuple { tasks._view()... };
    }, tuple.mTuple);
    return WhenAllTupleAwaiter<Types...>(views);
}

} // namespace detail


/**
 * @brief When All on multiple awaitable
 * 
 * @tparam Types 
 * @param args 
 * @return The awaitable for when all the given awaitable
 */
template <Awaitable ...Types>
inline auto whenAll(Types && ...args) noexcept {
    return detail::WhenAllTuple<AwaitableResult<Types>... > { //< Construct the task for the given awaitable
        { Task<AwaitableResult<Types> >(std::forward<Types>(args))... }
    };
}

/**
 * @brief When All on multiple awaitable
 * 
 * @tparam A 
 * @tparam B 
 * @param a The first awaitable
 * @param b The second awaitable
 * @return The awaitable for when all the given awaitable 
 */
template <Awaitable A, Awaitable B>
inline auto operator &&(A && a, B && b) noexcept {
    using ResultA = AwaitableResult<A>;
    using ResultB = AwaitableResult<B>;
    return detail::WhenAllTuple<ResultA, ResultB> {
        {
            Task<ResultA>(std::forward<A>(a)), 
            Task<ResultB>(std::forward<B>(b)) 
        }
    };
}

/**
 * @brief When All on multiple awaitable
 * 
 * @tparam Types 
 * @tparam T 
 * @param tuple 
 * @param t 
 * @return The awaitable for when all the given awaitable
 */
template <typename ...Types, Awaitable T>
inline auto operator &&(detail::WhenAllTuple<Types...> &&tuple, T && t) noexcept {
    return detail::WhenAllTuple<Types..., AwaitableResult<T> > {
        std::tuple_cat(
            std::move(tuple.mTuple), 
            std::tuple { Task<AwaitableResult<T> >(std::forward<T>(t)) } //< Convert the awaitable to task
        )
    };
}

ILIAS_NS_END