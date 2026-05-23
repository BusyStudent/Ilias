#pragma once

#include <ilias/defines.hpp>

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

// Clang supports statement expressions with coroutine keywords;
// GCC ICEs on it (all versions as of trunk). Everyone else uses co_yield fallback.
#if defined(__clang__)
    #define ILIAS_CO_TRYX_IMPL(...) ({                         \
        auto _res = (__VA_ARGS__);                             \
        if (!_res) {                                           \
            co_return ::ilias::Err(std::move(_res).error());   \
        }                                                      \
                                                               \
        std::move(_res).value();                               \
    })
#else
    #define ILIAS_CO_TRYX_IMPL(...) (co_yield(__VA_ARGS__))
#endif

// Impl TRY...
#define ILIAS_CO_TRY_IMPL(var, tmp, ...)                   \
    auto tmp = (__VA_ARGS__);                              \
    if (!tmp) {                                            \
        co_return ::ilias::Err(std::move(tmp).error());    \
    }                                                      \
    var = std::move(*tmp)

// Impl TRYV...
#define ILIAS_CO_TRYV_IMPL(...)                                \
    do {                                                       \
        if (auto _res = (__VA_ARGS__); !_res) {                \
            co_return ::ilias::Err(std::move(_res).error());   \
        }                                                      \
    } while (false)

/**
 * @brief Unwrap an expected/optional value inside a coroutine, short-circuiting on error.
 * 
 * Analogous to Rust's `?` operator. If the expression yields a value, it is unwrapped
 * and returned as the result of the macro invocation. If the expression yields an error,
 * the enclosing coroutine immediately completes with that error propagated to the caller.
 * 
 * @param ... An expression that evaluates to an expected-like type (e.g. `Result<T, E>`).
 *            May include `co_await` subexpressions.
 * 
 * @note This macro expands to a `co_yield` expression or `co_return` and is only valid inside a coroutine
 *       whose promise type provides a compatible `yield_value()` overload.
 * 
 * @code
 *   auto example() -> IoTask<int> {
 *       auto val  = ILIAS_CO_TRYX(co_await fetchData());
 *       auto parsed = ILIAS_CO_TRYX(parse(val));
 *       co_return parsed + 1;
 *   }
 * @endcode
 */
#define ILIAS_CO_TRYX(...) ILIAS_CO_TRYX_IMPL(__VA_ARGS__)

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
#define ILIAS_CO_TRY(var, ...) ILIAS_CO_TRY_IMPL(var, ILIAS_CONCAT(_tmp, __LINE__), __VA_ARGS__)

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
 *       ILIAS_TRYV(co_await connect());
 *       ILIAS_TRYV(co_await sendRequest());
 *       co_return {};
 *   }
 * @endcode
 */
#define ILIAS_CO_TRYV(...) ILIAS_CO_TRYV_IMPL(__VA_ARGS__)

// Short name
#define ILIAS_TRY(...)  ILIAS_CO_TRY(__VA_ARGS__)
#define ILIAS_TRYV(...) ILIAS_CO_TRYV(__VA_ARGS__)
#define ILIAS_TRYX(...) ILIAS_CO_TRYX(__VA_ARGS__)

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
concept IsResult = detail::IsResult<T>::value;

ILIAS_NS_END