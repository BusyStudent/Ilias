// INTERNAL!!!
/**
 * @file narrow.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Helper functions for auto narrowing, used for handling narrow on some system apis
 * @version 0.1
 * @date 2026-10-04
 * 
 * @copyright Copyright (c) 2026
 * 
 */
#pragma once

#include <ilias/defines.hpp>
#include <concepts>
#include <utility>

ILIAS_NS_BEGIN

namespace detail {

template <std::integral T>
struct NarrowInto {
    T value;

    template <std::integral U>
    operator U() const noexcept {
        ILIAS_ASSERT(std::in_range<U>(value), "The value '{}' is out of range for the target type", value);
        return static_cast<U>(value);
    }
};

} // namespace detail

/**
 * @brief Auto narrowing to the target types
 * 
 * @tparam T 
 * @param value 
 */
template <std::integral T>
inline auto narrowInto(T value) noexcept {
    return detail::NarrowInto<T>{value};
}

ILIAS_NS_END