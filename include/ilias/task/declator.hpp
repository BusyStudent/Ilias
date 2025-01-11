/**
 * @file declator.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The declator
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
    std::chrono::milliseconds mMs; //< The 

    template <typename T>
    auto declare(Task<T> &&task) const {
        return impl(std::move(task), *this);
    }

    template <typename T>
    static auto impl(Task<T> task, TimeoutTags tags) -> Task<AddResultIf<T> > {
        auto [res, timeout] = co_await whenAny(std::move(task), sleep(tags.mMs));
        if (timeout) {
            co_return Unexpected(Error::TimedOut);
        }
        if constexpr (std::is_same_v<T, void>) { //< If is void, just return it
            co_return {};
        }
        else {
            co_return std::move(*res);
        }
    }
};

} // namespace detail

/**
 * @brief The concept of declare any awaitable
 * 
 * @tparam T 
 */
template <typename T>
concept AwaitableDeclator = requires(T t) {
    t.declare(Task<void> { });
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
 * @brief Combine the awaitable with declator
 * 
 * @tparam T The awaitable type
 * @tparam Declator The AwaitableDeclator type
 * @param awaitable 
 * @param declator 
 * @return auto 
 */
template <Awaitable T, AwaitableDeclator Declator>
inline auto operator |(T &&awaitable, Declator &&declator) {
    return std::forward<Declator>(declator).declare(
        Task{std::forward<T>(awaitable)} //< Make the awaitable into task
    );
}

ILIAS_NS_END