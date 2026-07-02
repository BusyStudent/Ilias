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

#include <ilias/runtime/await.hpp> // Awaitable
#include <ilias/io/error.hpp> // IoTask
#include <ilias/io/vec.hpp> // IoVec
#include <ilias/buffer.hpp> // Buffer
#include <concepts>
#include <cstddef>
#include <cstdint> // int64_t
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
 * @brief Enum for BufReadable::fill
 * 
 */
enum class FillPolicy : int {
    None, // Only fill the buffer when it's empty
    More, // Always fill the buffer with more data
};

/**
 * @brief Concept for check types that is awaitable && await result is same as IoResult<U>
 * 
 * @tparam T
 * @tparam U
 */
template <typename T, typename U>
concept IsIoAwaitable = requires {
    requires Awaitable<T>; // T is awaitable
    requires std::same_as<AwaitableResult<T>, IoResult<U> >; // T's await result is same as IoResult<U>
};

/**
 * @brief Concept for types that can be read to a a byte span.
 * 
 * @tparam T 
 */
template <typename T>
concept Readable = requires(T &t) {
    { t.read(MutableBuffer {}) } -> IsIoAwaitable<size_t>;
};

/**
 * @brief Concept for types that can be written to a byte span.
 * 
 * @tparam T 
 */
template <typename T>
concept Writable = requires(T &t) {
    { t.write(Buffer {}) } -> IsIoAwaitable<size_t>;
    { t.shutdown() }       -> IsIoAwaitable<void>;
    { t.flush() }          -> IsIoAwaitable<void>;
};

/**
 * @brief Concept for types that can seek to a position.
 * 
 * @tparam T 
 */
template <typename T>
concept Seekable = requires(T &t) {
    { t.seek(int64_t {}, SeekOrigin {}) } -> IsIoAwaitable<uint64_t>;  
};

/**
 * @brief Concept for types that has a buffer to do read operations.
 * 
 * @tparam T 
 */
template <typename T>
concept BufReadable = Readable<T> && requires(T &t) {
    { t.fill(FillPolicy {}) } -> IsIoAwaitable<Buffer>;
    { t.consume(size_t {}) } -> std::same_as<void>;
};

/**
 * @brief Concept for types that can be read to a vector of byte spans.
 * 
 * @tparam T 
 */
template <typename T>
concept ScatterReadable = Readable<T> && requires(T &t) {
    { t.readv(std::span<const MutableIoVec> {}) } -> IsIoAwaitable<size_t>;
};

/**
 * @brief Concept for types that can be written to a vector of byte spans.
 * 
 * @tparam T 
 */
template <typename T>
concept GatherWritable = Writable<T> && requires(T &t) {
    { t.writev(std::span<const IoVec> {}) } -> IsIoAwaitable<size_t>;
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
concept BorrowFileDescriptor = requires(T &t) {
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