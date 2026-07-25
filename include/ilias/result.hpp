#pragma once

#include <ilias/defines.hpp>
#include <ilias/macros.hpp>
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