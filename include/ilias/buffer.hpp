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

#include <ilias/defines.hpp>
#include <cstring> // memcpy
#include <string>
#include <span>

ILIAS_NS_BEGIN

/**
 * @brief The byte const buffer view
 * 
 */
using Buffer        = std::span<const std::byte>;

/**
 * @brief The byte mutable buffer view
 * 
 */
using MutableBuffer = std::span<std::byte>;


/**
 * @brief Concept for types that can be converted to std::span
 * 
 * @tparam T 
 */
template <typename T>
concept IntoSpan = requires(T &t) {
    std::span(t);
};


/**
 * @brief Concept for types that can be converted to Buffer
 * 
 * @tparam T 
 */
template <typename T>
concept IntoBuffer = requires(T &t) {
    { makeBuffer(t) } -> std::convertible_to<Buffer>;
};

/**
 * @brief Concept for types that can be converted to MutableBuffer
 * 
 * @tparam T 
 */
template <typename T>
concept IntoMutableBuffer = requires(T &t) {
    { makeBuffer(t) } -> std::convertible_to<MutableBuffer>;
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

/**
 * @brief Concept for types that can be resized, written to memory and read from memory, like (std::string or std::vector)
 * 
 * @tparam T 
 */
template <typename T>
concept MemContainer = MemExpendable<T> && MemWritable<T> && MemReadable<T>;

/**
 * @brief Concept for types that can be iterated, like (std::array<Buffer> or std::vector<Buffer> or std::span<const Buffer>)
 * 
 * @tparam T 
 */
template <typename T>
concept BufferSequence = requires(T &t) {
    { *std::begin(t) } -> std::convertible_to<Buffer>;
    { *std::end(t)  } -> std::convertible_to<Buffer>;
    {  std::size(t) } -> std::convertible_to<size_t>;
};

template <typename T>
concept MutableBufferSequence = requires(T &t) {
    { *std::begin(t) } -> std::convertible_to<MutableBuffer>;
    { *std::end(t)  } -> std::convertible_to<MutableBuffer>;
    {  std::size(t) } -> std::convertible_to<size_t>;
};

// --- Utils for Buffer(std::span<const std::byte>) & MutableBuffer(std::span<std::byte>)
/**
 * @brief Make a const buffer from void pointer and size
 * 
 * @param buf 
 * @param n 
 * @return Buffer 
 */
inline auto makeBuffer(const void *buf, size_t n) noexcept -> Buffer {
    return std::span(reinterpret_cast<const std::byte *>(buf), n);
}

/**
 * @brief Make a writable buffer from void pointer and size
 * 
 * @param buf 
 * @param n 
 * @return MutableBuffer 
 */
inline auto makeBuffer(void *buf, size_t n) noexcept -> MutableBuffer {
    return std::span(reinterpret_cast<std::byte *>(buf), n);
}

/**
 * @brief Convert object to buffer
 * 
 * @tparam T 
 * @param object 
 * @return auto 
 */
template <IntoSpan T>
inline auto makeBuffer(const T &object) {
    auto span = std::span(object);
    return makeBuffer(span.data(), span.size_bytes());
}

/**
 * @brief Convert object to buffer
 * 
 * @tparam T 
 * @param object 
 * @return auto 
 */
template <IntoSpan T>
inline auto makeBuffer(T &object) {
    auto span = std::span(object);
    return makeBuffer(span.data(), span.size_bytes());
}

namespace literals {
    inline auto operator"" _bin(const char *buf, size_t len) -> Buffer {
        return {reinterpret_cast<const std::byte *>(buf), len};
    }

    inline auto operator"" _bin(unsigned long long val) -> std::byte {
        return static_cast<std::byte>(val);
    }
}

ILIAS_NS_END