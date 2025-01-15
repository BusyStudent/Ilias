/**
 * @file decorator.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The decorator
 * @version 0.1
 * @date 2025-01-11
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#pragma once

#include <ilias/task/when_any.hpp>
#include <ilias/task/task.hpp>
#include <chrono>

ILIAS_NS_BEGIN

namespace detail {

/**
 * @brief Tags for setTimeout
 * 
 */
struct TimeoutTags {
    std::chrono::milliseconds mMs; //< The Timeout

    template <typename T>
    auto decorate(Task<T> &&task) const {
        return impl(std::move(task), *this);
    }

    template <typename T>
    static auto impl(Task<T> task, TimeoutTags tags) -> Task<AddResultIf<T> > {
        auto [res, timeout] = co_await whenAny(std::move(task), sleep(tags.mMs));
        if (timeout) {
            co_return Unexpected(Error::TimedOut);
        }
        if constexpr (std::is_same_v<T, void>) { // If is void, just return it
            co_return {};
        }
        else {
            co_return std::move(*res);
        }
    }
};

/**
 * @brief Awaiter to start the task, 
 * 
 * @tparam T 
 */
template <typename T>
struct IgnoreCancellationAwaiter {
    auto await_ready() const noexcept { return false; }

    auto await_suspend(CoroHandle caller) const -> bool {
        auto task = mTask._view();
        task.setExecutor(caller.executor());
        task.resume();
        if (task.done()) {
            return false;
        }
        task.setAwaitingCoroutine(caller);
        return true;
    }
    
    auto await_resume() const {
        return mTask._view().value();
    }

    Task<T> mTask;
};

/**
 * @brief Tags for ignore the cancellation
 * 
 */
struct IgnoreCancellationTags {
    template <typename T>
    auto decorate(Task<T> &&task) const {
        return IgnoreCancellationAwaiter<T> { std::move(task) };
    }
};

} // namespace detail

/**
 * @brief The concept of decorate any awaitable
 * 
 * @code 
 *  co_await awaitable | decorator;
 * @endcode 
 * 
 * @tparam T 
 */
template <typename T>
concept AwaitableDecorator = requires(T t) {
    t.decorate(Task<void> { });
};

/**
 * @brief add an timeout limit to an awaitable, use | to combine it
 * 
 * @param ms 
 */
[[nodiscard("Do not forget to use operator |")]]
inline auto setTimeout(std::chrono::milliseconds ms) noexcept {
    return detail::TimeoutTags { ms };
}

/**
 * @brief add an timeout limit to an awaitable, use | to combine it
 * 
 * @param ms
 */
[[nodiscard("Do not forget to use operator |")]]
inline auto setTimeout(uint64_t ms) noexcept {
    return setTimeout(std::chrono::milliseconds(ms));
}

/**
 * @brief Combine the awaitable with decorator
 * 
 * @tparam T The awaitable type
 * @tparam Decorator The AwaitableDecorator type
 * @param awaitable 
 * @param decorator 
 * @return auto 
 */
template <Awaitable T, AwaitableDecorator Decorator>
inline auto operator |(T &&awaitable, Decorator &&decorator) {
    return std::forward<Decorator>(decorator).decorate(
        Task{std::forward<T>(awaitable)} // Make the awaitable into task
    );
}

/**
 * @brief Combine the awaiteble with decorator
 * 
 * @note requires the combine result still is T
 * @tparam T 
 * @tparam Decorator
 * @param awaitable 
 * @param decorator 
 * @return auto 
 */
template <typename T, AwaitableDecorator Decorator> 
inline auto operator |=(Task<T> &awaitable, Decorator &&decorator) -> T & {
    awaitable = std::move(awaitable) | std::forward<Decorator>(decorator);
    return awaitable;
}

/**
 * @brief The tags used to ignore the cancellation, use | to combine it
 * 
 */
inline constexpr auto ignoreCancellation = detail::IgnoreCancellationTags { };

ILIAS_NS_END