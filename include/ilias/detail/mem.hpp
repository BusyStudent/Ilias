/**
 * @file mem.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Provides memory utilities.
 * @version 0.1
 * @date 2024-08-11
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#pragma once

#include <ilias/ilias.hpp>
#include <algorithm> //< for std::lexicographical_compare_three_way
#include <compare>
#include <cstring>
#include <cstddef>
#include <cctype>
#include <string>

ILIAS_NS_BEGIN

namespace mem {

/**
 * @brief Do the case-ignoring comparison of two strings.
 * 
 * @param lhs 
 * @param rhs 
 * @return auto 
 */
inline auto strcasecmp(std::string_view lhs, std::string_view rhs) {
    return std::lexicographical_compare_three_way(
        lhs.begin(), lhs.end(),
        rhs.begin(), rhs.end(),
        [](char lhs, char rhs) { return std::tolower(lhs) <=> std::tolower(rhs); }
    );
}

/**
 * @brief Make a string lowercase.
 * 
 * @param str 
 * @return auto 
 */
inline auto lowercase(std::string_view str) {
    std::string result(str);
    std::transform(result.begin(), result.end(), result.begin(), [](char c) { return std::tolower(c); });
    return result;
}

inline auto uppercase(std::string_view str) {
    std::string result(str);
    std::transform(result.begin(), result.end(), result.begin(), [](char c) { return std::toupper(c); });
    return result;
}

/**
 * @brief Compare two strings in a case-insensitive manner.
 * 
 */
struct Casecompare {
    using is_transparent = void;

    auto operator()(std::string_view lhs, std::string_view rhs) const {
        return strcasecmp(lhs, rhs) == std::strong_ordering::less;
    }
};


}

ILIAS_NS_END