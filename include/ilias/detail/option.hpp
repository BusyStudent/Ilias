/**
 * @file option.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Provides a void-safe wrapper for optional, FUCK void
 * @version 0.1
 * @date 2025-08-09
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#pragma once
#include <ilias/defines.hpp>
#include <concepts> // std::invocable
#include <optional> // std::optional
#include <variant> // std::monostate

ILIAS_NS_BEGIN

namespace detail {
    template <typename T>
    struct ReplaceVoid {
        using type = T;  
    };

    template <>
    struct ReplaceVoid<void> {
        using type = std::monostate;
    };
} // namespace detail

// For replace the fucking void to std::monostate :(
template <typename T>
using Option = std::optional<typename detail::ReplaceVoid<T>::type>;

// Create an option with 'value'
template <std::invocable Fn>
auto makeOption(Fn &&fn) -> Option<std::invoke_result_t<Fn> > {
    if  constexpr (std::is_void_v<std::invoke_result_t<Fn> >) {
        fn();
        return std::monostate{};
    }
    else {
        return fn();
    }
}

// Unwrap an Option<T> 's value, if void, return the void
template <typename T>
auto unwrapOption(std::optional<T> &&opt) {
    ILIAS_ASSERT(opt.has_value());
    if constexpr (std::is_same_v<T, std::monostate>) {
        return;
    }
    else {
        return std::move(*opt);
    }
}

ILIAS_NS_END