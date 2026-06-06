#pragma once

#include <ilias/defines.hpp>
#include <type_traits>
#include <optional>
#include <utility>

#if defined(ILIAS_USE_ZEUS_EXPECTED)
    #include <zeus/expected.hpp>
    #define ILIAS_EXPECTED_NAMESPACE ::zeus
#else
    #include <expected>
    #define ILIAS_EXPECTED_NAMESPACE ::std
#endif // ILIAS_EXPECTED_NAMESPACE

// clang 19 actually supports C++20 CTAD but doesn't define __cpp_deduction_guides correctly
#if (!defined(__clang__) && __cpp_deduction_guides < 201907L) || (defined(__clang__) && __clang_major__ < 19)
#error "This library need C++20 CTAD for aggregates and aliases"
#endif // __cpp_deduction_guides

// Impl TRY...
#if defined(__clang__) // Using clang's statement expression to optoimize away the temporary
    #define ILIAS_BASIC_TRY_IMPL(var, tmp, ret, ...)           \
        var = ({                                               \
            auto &&tmp = (__VA_ARGS__);                        \
            if (!tmp) {                                        \
                ret ::ilias::makeErr(std::move(tmp));          \
            }                                                  \
            std::move(*tmp);                                   \
        })
#else
    #define ILIAS_BASIC_TRY_IMPL(var, tmp, ret, ...)           \
        auto &&tmp = (__VA_ARGS__);                            \
        if (!tmp) {                                            \
            ret ::ilias::makeErr(std::move(tmp));              \
        }                                                      \
        static_cast<void>(tmp);                                \
        var = std::move(*tmp)
#endif // __clang__

// Impl TRYV...
#define ILIAS_BASIC_TRYV_IMPL(ret, ...)                        \
    do {                                                       \
        if (auto &&_res = (__VA_ARGS__); !_res) {              \
            ret ::ilias::makeErr(std::move(_res));             \
        }                                                      \
    } while (false)

/**
 * @brief Unwrap an expected/optional value inside a coroutine and bind it to a local variable.
 *
 * @param var The name of the local variable to declare.
 * @param ... An expression that evaluates to an expected-like type, such as
 *            `Result<T, E>`. The expression may contain `co_await`.
 *
 * @note This macro is only valid inside a coroutine whose return type and promise type.
 *
 * @code
 *   auto example() -> IoTask<int> {
 *       ILIAS_CO_TRY(auto data, co_await fetchData());
 *       ILIAS_CO_TRY(auto value, parse(data));
 *       co_return value + 1;
 *   }
 * @endcode
 */
#define ILIAS_CO_TRY(var, ...) ILIAS_BASIC_TRY_IMPL(var, ILIAS_CONCAT(_tmp_, __LINE__), co_return, __VA_ARGS__)

/**
 * @brief Check an expected/optional result inside a coroutine and discard the success value.
 *
 * It evaluates the given expression and continues execution if the expression succeeds.
 * If the expression contains an error, the enclosing coroutine immediately completes by
 * propagating that error to the caller.
 *
 * Use this macro when the success value is not needed, or when the expression returns
 * a void-like result such as `Result<void, E>`.
 *
 * @param ... An expression that evaluates to an expected-like type, such as
 *            `Result<T, E>`. The expression may contain `co_await`.
 *
 * @note This macro is only valid inside a coroutine whose return type and promise type.
 *
 * @code
 *   auto example() -> IoTask<void> {
 *       ILIAS_CO_TRYV(co_await connect());
 *       ILIAS_CO_TRYV(co_await sendRequest());
 *       co_return {};
 *   }
 * @endcode
 */
#define ILIAS_CO_TRYV(...) ILIAS_BASIC_TRYV_IMPL(co_return, __VA_ARGS__)

// Sync version
/**
 * @brief Unwrap an expected/optional value inside a coroutine and bind it to a local variable.
 *
 * @param var The name of the local variable to declare.
 * @param ... An expression that evaluates to an expected-like type, such as
 *            `Result<T, E>`.
 *
 * @note This macro is only valid inside a normal function.
 *
 * @code
 *   auto example() -> IoResult<int> {
 *       ILIAS_TRY(auto data, fetchData());
 *       ILIAS_TRY(auto value, parse(data));
 *       return value + 1;
 *   }
 * @endcode
 */
#define ILIAS_TRY(var, ...)  ILIAS_BASIC_TRY_IMPL(var, ILIAS_CONCAT(_tmp_, __LINE__), return, __VA_ARGS__)

/**
 * @brief Check an expected/optional result inside a coroutine and discard the success value.
 *
 * It evaluates the given expression and continues execution if the expression succeeds.
 * If the expression contains an error, the enclosing coroutine immediately completes by
 * propagating that error to the caller.
 *
 * Use this macro when the success value is not needed, or when the expression returns
 * a void-like result such as `Result<void, E>`.
 *
 * @param ... An expression that evaluates to an expected-like type, such as
 *            `Result<T, E>`.
 *
 * @note This macro is only valid inside a normal function.
 *
 * @code
 *   auto example() -> IoResult<void> {
 *       ILIAS_TRYV(connect());
 *       ILIAS_TRYV(sendRequest());
 *       return {};
 *   }
 * @endcode
 */
#define ILIAS_TRYV(...) ILIAS_BASIC_TRYV_IMPL(return, __VA_ARGS__)

ILIAS_NS_BEGIN

// Helper for detection
namespace detail {

template <typename T>
struct IsResult : std::false_type {};

template <typename T, typename E>
struct IsResult<ILIAS_EXPECTED_NAMESPACE::expected<T, E> > : std::true_type {};

} // namespace detail

namespace exp = ILIAS_EXPECTED_NAMESPACE;

template <typename T, typename E>
using Result = exp::expected<T, E>;

template <typename E>
using Err = exp::unexpected<E>;

template <typename E>
using BadResultAccess = exp::bad_expected_access<E>;

// For compatibility
template <typename T>
using Unexpected [[deprecated("Use Err instead")]] = exp::unexpected<T>;

template <typename E>
using BadExpectedAccess [[deprecated("Use BadResultAccess instead")]] = exp::bad_expected_access<E>;

// For detection
template <typename T>
concept IsResult = detail::IsResult<std::remove_cv_t<T> >::value;

// Utils function to make error, used in TRY macro
template <typename T, typename E>
inline auto makeErr(Result<T, E> result) -> Err<E> {
    ILIAS_ASSUME(!result, "The result should contains a error");
    return Err(std::move(result.error()));
}

template <typename T>
inline auto makeErr(std::optional<T> option) -> std::nullopt_t {
    ILIAS_ASSUME(!option, "The option should be empty");
    return std::nullopt;
}

// Did you forget to use co_await ?
template <typename T>
inline auto makeErr(Task<T> task) -> T = delete;

ILIAS_NS_END