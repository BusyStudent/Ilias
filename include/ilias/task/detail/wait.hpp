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
 * @brief Check is the type can be casted to an awaiter by member co_await()
 * 
 * @tparam T 
 */
template <typename T>
concept HasMemberCoAwait = requires(T &&t) {
    std::forward<T>(t).operator co_await();
};

/**
 * @brief Check is the type can be casted to an awaiter by co_await()
 * 
 * @tparam T 
 */
template <typename T>
concept HasCoAwait = requires(T &&t) {
    operator co_await(std::forward<T>(t));
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
 * @brief Check tge type can be co_await
 * 
 * @tparam T 
 */
template <typename T>
concept Awaitable = Awaiter<T> || HasCoAwait<T> || HasMemberCoAwait<T>;

/**
 * @brief Check the type can be blocking waited
 * 
 * @tparam T 
 */
template <typename T>
concept Waitable = HasWait<T> || Awaitable<T>;

/**
 * @brief The result type of the awaiter
 * 
 * @tparam T 
 */
template <Awaiter T>
using AwaiterResult = decltype(std::declval<T>().await_resume());

/**
 * @brief Convert the type to an awaiter
 * 
 * @tparam T 
 * @return auto 
 */
template <Awaitable T>
auto toAwaiter(T &&val) noexcept {
    if constexpr (Awaiter<T>) {
        return std::forward<T>(val);
    }
    if constexpr (HasCoAwait<T>) {
        return operator co_await(std::forward<T>(val));
    }
    if constexpr (HasMemberCoAwait<T>) {
        return std::forward<T>(val).operator co_await();
    }
}

template <typename T>
struct AwaitableResultImpl {
    using type = AwaiterResult<decltype(toAwaiter(std::declval<T>()))>;
};

template <typename T>
struct AwaitableResultImpl<Task<T> > { //< FAST PATH
    using type = T;
};

/**
 * @brief Get the result type of the awaitable
 * 
 * @tparam T 
 */
template <Awaitable T>
using AwaitableResult = AwaitableResultImpl<T>::type;

/**
 * @brief Helper tags struct for dispatch the wait function
 * 
 */
struct WaitTags { };

/**
 * @brief The helper function for the convert the awaitable to the task, it copy the awaitable
 * 
 * @tparam T 
 * @tparam U 
 * @param awaitable 
 * @return Task<T> 
 */
template <Awaitable T, typename U = AwaitableResult<T> >
inline auto awaitableWrapperCopy(T awaitable) -> Task<U> {
    co_return co_await std::move(awaitable);
}

/**
 * @brief The helper function for the convert the awaitable to the task, it forward the awaitable
 * 
 * @tparam T 
 * @tparam U 
 * @param awaitable 
 * @return Task<T> 
 */
template <Awaitable T, typename U = AwaitableResult<T> >
inline auto awaitableWrapperForward(T &&awaitable) -> Task<U> {
    co_return co_await std::forward<T>(awaitable);
}

/**
 * @brief The helper function for the convert the awaitable to the task
 * 
 * @tparam T 
 * @tparam U 
 * @param awaitable 
 * @return Task<T> 
 */
template <Awaitable T, typename U = AwaitableResult<T> >
inline auto awaitableWrapper(T &&awaitable) -> Task<U> {
    if constexpr (std::is_rvalue_reference_v<decltype(awaitable)>) {
        return awaitableWrapperCopy<T, U>(std::move(awaitable));
    }
    else {
        return awaitableWrapperForward<T, U>(std::forward<T>(awaitable));
    }
}

} // namespace detail


using detail::AwaitableResult;
using detail::Awaitable;

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
        return detail::awaitableWrapperForward(std::forward<T>(in)).wait();
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