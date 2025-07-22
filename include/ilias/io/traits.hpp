/**
 * @file traits.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Concept for IO operations.
 * @version 0.1
 * @date 2024-08-09
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/io/error.hpp>
#include <ilias/defines.hpp>
#include <ilias/buffer.hpp>
#include <concepts>
#include <cstddef>
#include <span>

ILIAS_NS_BEGIN

/**
 * @brief Concept for types that can be read to a a byte span.
 * 
 * @tparam T 
 */
template <typename T>
concept Readable = requires(T &t) {
    { t.read(MutableBuffer {}) } -> std::same_as<IoTask<size_t> >;
};

/**
 * @brief Concept for types that can be written to a byte span.
 * 
 * @tparam T 
 */
template <typename T>
concept Writable = requires(T &t) {
    { t.write(Buffer {}) } -> std::same_as<IoTask<size_t> >;
    { t.shutdown() }       -> std::same_as<IoTask<void> >;
    { t.flush() }          -> std::same_as<IoTask<void> >;
};

/**
 * @brief Concept for types that cast into the file descriptor.
 * 
 * @tparam T 
 */
template <typename T>
concept IntoFileDescriptor = requires(T &t) {
    static_cast<fd_t>(t);
};

/**
 * @brief Concept for types that can be read and written to a byte span.
 * 
 * @tparam T 
 */
template <typename T>
concept Stream = Readable<T> && Writable<T>;

// For compatibility with old code
template <typename T>
concept StreamClient = Stream<T>;

ILIAS_NS_END