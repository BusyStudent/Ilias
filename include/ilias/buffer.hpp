/**
 * @file buffer.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Provides buffer utils 
 * @version 0.1
 * @date 2024-08-12
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/ilias.hpp>
#include <span>

ILIAS_NS_BEGIN

// --- Utils for std::span<std::byte>
template <typename T>
concept CanToSpan = requires(T &t) {
    std::span(t);
};

/**
 * @brief Convert object to std::span<const std::byte>
 * 
 * @tparam T 
 * @param object 
 * @return std::span<const std::byte> 
 */
template <CanToSpan T>
inline auto as_buffer(const T &object) -> std::span<const std::byte> {
    return std::as_bytes(std::span(object));
}

/**
 * @brief Make a buffer from void pointer and size
 * 
 * @param buf 
 * @param n 
 * @return std::span<const std::byte> 
 */
inline auto as_buffer(const void *buf, size_t n) -> std::span<const std::byte> {
    return std::span(reinterpret_cast<const std::byte *>(buf), n);
}

/**
 * @brief Convert object to writable std::span<std::byte>
 * 
 * @tparam T 
 * @param object 
 * @return std::span<std::byte> 
 */
template <CanToSpan T>
inline auto as_writable_buffer(T &object) -> std::span<std::byte> {
    return std::as_writable_bytes(std::span(object));
}

/**
 * @brief Make a writable buffer from void pointer and size
 * 
 * @param buf 
 * @param n 
 * @return std::span<std::byte> 
 */
inline auto as_writable_buffer(void *buf, size_t n) -> std::span<std::byte> {
    return std::span(reinterpret_cast<std::byte *>(buf), n);
}

/**
 * @brief Cast std::span<In> to std::span<T>
 * 
 * @tparam T The target type 
 * @tparam In 
 * @param in 
 * @return std::span<T> 
 */
template <typename T, typename In>
inline auto span_cast(std::span<In> in) -> std::span<T> {
    return std::span {
        reinterpret_cast<T *>(in.data()),
        in.size_bytes() / sizeof(T)
    };
}

ILIAS_NS_END