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

// --- Concepts

/**
 * @brief Concept for types that can be converted to std::span
 * 
 * @tparam T 
 */
template <typename T>
concept CanToSpan = requires(T &t) {
    std::span(t);
};

/**
 * @brief Concept for types that can be resized
 * 
 * @tparam T 
 */
template <typename T>
concept MemExpendable = requires(T &t) {
    t.resize(size_t {0});  
};

/**
 * @brief Concept for types that can be written to memory
 * 
 * @tparam T 
 */
template <typename T>
concept MemWritable = requires(T &t) {
    { std::span(t).data() } -> std::convertible_to<void *>;
    { std::span(t).size_bytes() } -> std::convertible_to<size_t>;
};

/**
 * @brief Concept for types that can be read from memory
 * 
 * @tparam T 
 */
template <typename T>
concept MemReadable = requires(T &t) {
    { std::span(t).data() } -> std::convertible_to<const void *>;
    { std::span(t).size_bytes() } -> std::convertible_to<size_t>;
};

// --- Utils for std::span<std::byte>
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
 * @brief Convert string literal to std::span<const std::byte> (does not include null terminator)
 * 
 * @tparam N 
 * @return std::span<const std::byte> 
 */
template <size_t N>
inline auto as_buffer(const char (&str)[N]) -> std::span<const std::byte> {
    return std::as_bytes(std::span(str, N - 1));
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