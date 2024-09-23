/**
 * @file wait.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The header file for the wait function (useful when blocking wait the coroutine)
 * @version 0.1
 * @date 2024-09-22
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/ilias.hpp>
#include <coroutine>
#include <concepts>

/**
 * @brief Blocking wait on the Waitable object
 * 
 */
#define ilias_wait ILIAS_NAMESPACE::detail::WaitTags {} <<


ILIAS_NS_BEGIN

namespace detail {

/**
 * @brief Check is the type is an standard coroutine awaiter
 * 
 * @tparam T 
 */
template <typename T>
concept Awaiter = requires(T t) {
    t.await_ready();
    t.await_resume();
};

/**
 * @brief Check the type can directly wait
 * 
 * @tparam T 
 */
template <typename T>
concept HasWait = requires(T t) {
    t.wait();
};

/**
 * @brief Check the type can be blocking waited
 * 
 * @tparam T 
 */
template <typename T>
concept Waitable = HasWait<T> || Awaiter<T>;

/**
 * @brief Helper tags struct for dispatch the wait function
 * 
 */
struct WaitTags { };

} // namespace detail

/**
 * @brief Blocking the current thread and wait for the task to be done
 * 
 * @tparam T 
 * @param what The waitable object
 * @return auto 
 */
template <detail::Waitable T>
inline auto wait(T &&in) {
    if constexpr (detail::HasWait<T>) {
        return std::forward<T>(in).wait();
    }
    else {
        using returnType = decltype(in.await_resume());
        return [&]() -> Task<returnType> {
            co_return co_await std::forward<T>(in);
        }().wait().value();
    }
}

/**
 * @brief Helper operator for wait
 * 
 */
template <detail::Waitable T>
inline auto operator <<(detail::WaitTags, T &&in) {
    return wait(std::forward<T>(in));
}

ILIAS_NS_END