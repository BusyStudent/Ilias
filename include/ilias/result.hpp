/**
  ___ _ _             ____              _   _                
 |_ _| (_) __ _ ___  |  _ \ _   _ _ __ | |_(_)_ __ ___   ___ 
  | || | |/ _` / __| | |_) | | | | '_ \| __| | '_ ` _ \ / _ \
  | || | | (_| \__ \ |  _ <| |_| | | | | |_| | | | | | |  __/
 |___|_|_|\__,_|___/ |_| \_\\__,_|_| |_|\__|_|_| |_| |_|\___|

 * @file result.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The alias of the std::expected<T, E>, for error handling
 * @version 0.1
 * @date 2026-11-06
 * 
 * @copyright Copyright (c) 2026
 * 
 */
#pragma once

#include <ilias/defines.hpp>
#include <ilias/macros.hpp> // ILIAS_TRY*
#include <type_traits>
#include <optional>
#include <utility>

#if defined(ILIAS_USE_ZEUS_EXPECTED)
    #include <ilias/detail/bundled/expected.hpp>
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

namespace exp = ILIAS_EXPECTED_NAMESPACE;

ILIAS_EXPORT_BEGIN

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

// Utils function to make error, used in TRY macro, so we need to export them in module
namespace detail {

template <typename T, typename E>
constexpr auto makeErr(Result<T, E> result) -> Err<E> {
    if (!std::is_constant_evaluated()) {
        ILIAS_ASSUME(!result, "The result should contains a error");
    }
    return Err(std::move(result.error()));
}

template <typename T>
constexpr auto makeErr(std::optional<T> option) -> std::nullopt_t {
    if (!std::is_constant_evaluated()) {
        ILIAS_ASSUME(!option, "The option should be empty");        
    }
    return std::nullopt;
}

// Did you forget to use co_await ?
template <typename T>
constexpr auto makeErr(Task<T> task) -> T = delete;

} // namespace detail

ILIAS_EXPORT_END

ILIAS_NS_END