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

#include <ilias/io/error.hpp> // IoTask
#include <ilias/io/vec.hpp> // IoVec
#include <ilias/buffer.hpp> // Buffer
#include <concepts>
#include <cstddef>
#include <cstdio> // SEEK_CUR
#include <span>

ILIAS_NS_BEGIN

/**
 * @brief Enum for seek origin.
 * 
 */
enum class SeekOrigin : int {
    Begin   = SEEK_SET,
    Current = SEEK_CUR,
    End     = SEEK_END,
};

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
 * @brief Concept for types that can seek to a position.
 * 
 * @tparam T 
 */
template <typename T>
concept Seekable = requires(T &t) {
    { t.seek(int64_t {}, SeekOrigin {}) } -> std::same_as<IoTask<uint64_t> >;  
};

/**
 * @brief Concept for types that can be read to a vector of byte spans.
 * 
 * @tparam T 
 */
template <typename T>
concept ScatterReadable = Readable<T> && requires(T &t) {
    { t.readv(std::span<const MutableIoVec> {}) } -> std::same_as<IoTask<size_t> >;
};

/**
 * @brief Concept for types that can be written to a vector of byte spans.
 * 
 * @tparam T 
 */
template <typename T>
concept GatherWritable = Writable<T> && requires(T &t) {
    { t.writev(std::span<const IoVec> {}) } -> std::same_as<IoTask<size_t> >;
};

/**
 * @brief Concept for types that is a layer of another type. (such TlsStream<T> or BufReader)
 * 
 * @tparam T 
 */
template <typename T>
concept Layer = requires (T &t) {
    { t.nextLayer() } -> Readable;  
} || requires (T &t) {
    { t.nextLayer() } -> Writable;   
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
 * @brief Concept for types that can be cast into a Generator.
 * 
 * @tparam T 
 */
template <typename T>
concept IntoGenerator = requires(T &t) {
    toGenerator(std::forward<T>(t));
};

/**
 * @brief Concept for types that can be read and written to a byte span.
 * 
 * @tparam T 
 */
template <typename T>
concept Stream = Readable<T> && Writable<T>;

/**
 * @brief Concept for types that can be read and written to a byte span and seek to a position.
 * 
 * @tparam T 
 */
template <typename T>
concept SeekableStream = Stream<T> && Seekable<T>;

// For compatibility with old code
template <typename T>
concept StreamClient = Stream<T>;

ILIAS_NS_END