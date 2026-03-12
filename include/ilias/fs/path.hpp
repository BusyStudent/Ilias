/**
 * @file path.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The path class & utils (use the std::filesystem::path)
 * @version 0.1
 * @date 2026-03-12
 *  
 * @copyright Copyright (c) 2026
 * 
 */
#pragma once

#include <ilias/defines.hpp>
#include <filesystem>

ILIAS_NS_BEGIN

namespace fs {

/**
 * @brief The path class
 * 
 */
using Path = std::filesystem::path;

/**
 * @brief Concept for std::filesystem::path or another compatible path type 
 * 
 * @tparam T 
 */
template <typename T>
concept IntoPath = std::convertible_to<T, Path>;

// Uitls
/**
 * @brief Convert a PathLike to Path
 * @note The char will be treated as utf-8 encoded
 * @tparam T 
 * @return Path 
 */
template <IntoPath T>
inline auto toPath(T &&path) -> Path {
    if constexpr (std::convertible_to<T, std::string_view>) {
        // std::string_view
        auto sv = std::string_view {path};
        auto u8 = std::u8string_view {
            reinterpret_cast<const char8_t *>(sv.data()),
            sv.size()
        };
        return Path {u8};
    }
    else {
        return Path {path};
    }
}

} // namespace fs

ILIAS_NS_END