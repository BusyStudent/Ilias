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

#include <ilias/io/vec.hpp>
#include <ilias/ilias.hpp>
#include <cstddef>
#include <span>

ILIAS_NS_BEGIN

/**
 * @brief A type that can be converted to any type.
 * 
 */
struct AnyType {
    template <typename T>
    constexpr operator T() const noexcept; // No implementation
};

/**
 * @brief Concept for types that can be read to a a byte span.
 * 
 * @tparam T 
 */
template <typename T>
concept Readable = requires(T &t) {
    t.read(MutableBuffer {});
};

/**
 * @brief Concept for types that can be written to a byte span.
 * 
 * @tparam T 
 */
template <typename T>
concept Writable = requires(T &t) {
    t.write(Buffer {});
};

/**
 * @brief Concept for types that support vectorized/gathered read operations
 * 
 * @tparam T 
 */
template <typename T>
concept ScatterReadable = requires(T &t) {
    t.read(std::span<const IoVec> {});  
};

/**
 * @brief Concept for types that support vectorized/gathered write operations
 * 
 * @tparam T 
 */
template <typename T>
concept GatherWritable = requires(T &t) {
    t.write(std::span<const IoVec> {});  
};

/**
 * @brief Concept for types that can be shutdown, performing any necessary cleanup.
 * 
 * @tparam T 
 */
template <typename T>
concept Shuttable = requires(T &t) {
    t.shutdown();
};

/**
 * @brief Concept for types that can be connected to an endpoint.
 * 
 * @tparam T 
 */
template <typename T>
concept Connectable = requires(T &t) {
    t.connect(AnyType {});
};

/**
 * @brief Concept for types that can be accept and create a new connection.
 * 
 * @tparam T 
 */
template <typename T>
concept Listener = requires(T &t) {
    t.accept();
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

/**
 * @brief Concept for types that can be read, written in vectorized/gathered operations.
 * 
 * @tparam T 
 */
template <typename T>
concept StreamExt = GatherWritable<T> && ScatterReadable<T>;

/**
 * @brief Concept for types that can be read, written to a byte span and shutdown.
 * 
 * @tparam T 
 */
template <typename T>
concept StreamClient = Stream<T> && Shuttable<T>;


ILIAS_NS_END