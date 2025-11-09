// INTERNAL !!!
#pragma once

#include <ilias/defines.hpp>
#include <coroutine>
#include <concepts>
#include <ranges>

ILIAS_NS_BEGIN

namespace runtime {

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
 * @brief Check the type can be directly co_await
 * 
 * @tparam T 
 */
template <typename T>
concept RawAwaitable = Awaiter<T> || HasCoAwait<T> || HasMemberCoAwait<T>;

/**
 * @brief Types that can be cast into an awaitable using ADL
 * 
 * @tparam T 
 */
template <typename T>
concept IntoRawAwaitableADL = requires(T t) {
    { toAwaitable(std::forward<T>(t)) } -> RawAwaitable;
};

/**
 * @brief Check the type is an raw awaitable or can be casted into an awaitable
 * 
 * @tparam T 
 */
template <typename T>
concept Awaitable = RawAwaitable<T> || IntoRawAwaitableADL<T>;

/**
 * @brief Check the type is a sequence of awaitable like (std::vector<Task<void> >)
 * 
 * @tparam T 
 */
template <typename T>
concept AwaitableSequence = std::ranges::range<T> && Awaitable<std::ranges::range_value_t<T> >;

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
auto toAwaiter(T &&val) noexcept -> decltype(auto) {
    if constexpr (Awaiter<T>) {
        return std::forward<T>(val);
    }
    else if constexpr (HasCoAwait<T>) {
        return operator co_await(std::forward<T>(val));
    }
    else if constexpr (HasMemberCoAwait<T>) {
        return std::forward<T>(val).operator co_await();
    }
    else if constexpr (IntoRawAwaitableADL<T>) {
        return toAwaiter(toAwaitable(std::forward<T>(val)));
    }
    else {
        static_assert(std::is_same_v<T, void>, "Emm?, impossible");
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
 * @brief Get the co_await result type of an awaitable
 * 
 * @tparam T 
 */
template <Awaitable T>
using AwaitableResult = typename AwaitableResultImpl<T>::type;

/**
 * @brief Get the value type of the awaitable sequence 
 * 
 * @tparam T 
 */
template <AwaitableSequence T>
using AwaitableSequenceValue = AwaitableResult<std::ranges::range_value_t<T> >;

} // namespace runtime

using runtime::AwaitableSequenceValue;
using runtime::AwaitableSequence;
using runtime::AwaitableResult;
using runtime::Awaitable;

ILIAS_NS_END