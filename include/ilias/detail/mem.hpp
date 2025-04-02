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
#include <optional>
#include <compare>
#include <cstring>
#include <cstddef>
#include <cctype>
#include <string>
#include <span>

ILIAS_NS_BEGIN

namespace mem {

/**
 * @brief Do the case-ignoring comparison of two strings.
 * 
 * @param lhs 
 * @param rhs 
 * @return auto 
 */
inline auto strcasecmp(std::string_view lhs, std::string_view rhs) noexcept {
    return std::lexicographical_compare_three_way(
        lhs.begin(), lhs.end(),
        rhs.begin(), rhs.end(),
        [](char lhs, char rhs) { return std::tolower(lhs) <=> std::tolower(rhs); }
    );
}

/**
 * @brief Do the memory comparison of two memories.
 * 
 * @param lhs 
 * @param rhs 
 * @param n 
 * @return auto 
 */
inline auto memcmp(const void *lhs, const void *rhs, size_t n) noexcept {
    return ::memcmp(lhs, rhs, n) <=> 0;
}

/**
 * @brief Do the memory comparison of two memories.
 * 
 * @param lhs
 * @param rhs 
 * @return auto 
 */
inline auto memcmp(std::span<const std::byte> lhs, std::span<const std::byte> rhs) noexcept {
    return std::lexicographical_compare_three_way(
        lhs.begin(), lhs.end(),
        rhs.begin(), rhs.end()
    );
}

/**
 * @brief Search for a memory block in another memory block.
 * 
 * @param haystack 
 * @param needle 
 * @return std::optional<size_t> The offset of the needle in the haystack, or std::nullopt if not found.
 */
inline auto memmem(std::span<const std::byte> haystack, std::span<const std::byte> needle) noexcept -> std::optional<size_t> {

#if defined(__linux__)
    auto ptr = ::memmem(haystack.data(), haystack.size(), needle.data(), needle.size());
    if (ptr == nullptr) {
        return std::nullopt;
    }
    return static_cast<const std::byte*>(ptr) - haystack.data();
#else
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end());
    if (it == haystack.end()) {
        return std::nullopt;
    }
    return it - haystack.begin();
#endif // defined(__linux__)

}

/**
 * @brief Compare two strings in a case-insensitive manner.
 * 
 */
struct CaseCompare {
    using is_transparent = void;

    auto operator()(std::string_view lhs, std::string_view rhs) const noexcept {
        return strcasecmp(lhs, rhs) == std::strong_ordering::less;
    }
};

} // namespace mem

ILIAS_NS_END