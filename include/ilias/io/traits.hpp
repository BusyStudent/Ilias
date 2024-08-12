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

#include <ilias/ilias.hpp>
#include <cstddef>
#include <span>

ILIAS_NS_BEGIN

// Forward declaration
class IPEndpoint;

/**
 * @brief Concept for types that can be read to a a byte span.
 * 
 * @tparam T 
 */
template <typename T>
concept Readable = requires(T t) {
    t.read(std::span<std::byte> {});
};

/**
 * @brief Concept for types that can be written to a byte span.
 * 
 * @tparam T 
 */
template <typename T>
concept Writable = requires(T t) {
    t.write(std::span<std::byte> {});
};

/**
 * @brief Concept for types that can be shutdown, performing any necessary cleanup.
 * 
 * @tparam T 
 */
template <typename T>
concept Shuttable = requires(T t) {
    t.shutdown();
};

/**
 * @brief Concept for types that can be connected to an endpoint.
 * 
 * @tparam T 
 */
template <typename T>
concept Connectable = requires(T t) {
    t.connect(std::declval<const IPEndpoint &>());
};

/**
 * @brief Concept for types that can be read and written to a byte span.
 * 
 * @tparam T 
 */
template <typename T>
concept Stream = Readable<T> && Writable<T>;

/**
 * @brief Concept for types that can be read, written to a byte span and shutdown.
 * 
 * @tparam T 
 */
template <typename T>
concept StreamClient = Stream<T> && Shuttable<T>;


ILIAS_NS_END