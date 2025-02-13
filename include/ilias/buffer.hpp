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
#include <cstring> //< for memcpy
#include <cstdarg>
#include <cstdio>  //< for sprintf
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
 * @brief Make a const buffer from void pointer and size
 * 
 * @param buf 
 * @param n 
 * @return std::span<const std::byte> 
 */
inline auto makeBuffer(const void *buf, size_t n) -> std::span<const std::byte> {
    return std::span(reinterpret_cast<const std::byte *>(buf), n);
}

/**
 * @brief Make a writable buffer from void pointer and size
 * 
 * @param buf 
 * @param n 
 * @return std::span<std::byte> 
 */
inline auto makeBuffer(void *buf, size_t n) -> std::span<std::byte> {
    return std::span(reinterpret_cast<std::byte *>(buf), n);
}

/**
 * @brief Convert object to buffer
 * 
 * @tparam T 
 * @param object 
 */
template <CanToSpan T>
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
template <CanToSpan T>
inline auto makeBuffer(T &object) {
    auto span = std::span(object);
    return makeBuffer(span.data(), span.size_bytes());
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
inline auto spanCast(std::span<In> in) -> std::span<T> {
    if constexpr(sizeof(T) != sizeof(In)) {
        ILIAS_ASSERT(in.size_bytes() % sizeof(T) == 0); // The size must be aligned
    }
    return std::span {
        reinterpret_cast<T *>(in.data()),
        in.size_bytes() / sizeof(T)
    };
}

// --- Utils for sprintf

/**
 * @brief Get the sprintf output size
 * 
 * @param fmt 
 * @param args The va_list, the function will use a copy of it
 * @return size_t 
 */
inline auto vsprintfSize(const char *fmt, va_list args) -> size_t {
    va_list copy;
    va_copy(copy, args);
    auto n = ::vsnprintf(nullptr, 0, fmt, args);
    va_end(copy);
    ILIAS_ASSERT(n >= 0);
    return n;
}

/**
 * @brief Get the sprintf output size
 * 
 * @param fmt 
 * @param ... 
 * @return size_t 
 */
inline auto sprintfSize(const char *fmt, ...) -> size_t {
    va_list args;
    va_start(args, fmt);
    auto n = ::vsnprintf(nullptr, 0, fmt, args);
    va_end(args);
    ILIAS_ASSERT(n >= 0);
    return n;
}

/**
 * @brief Print to buffer's back using va_list
 * 
 * @tparam T must be MemExpendable and MemWritable, and the value type must be char or byte
 * @param buf 
 * @param fmt 
 * @param args 
 * @return size_t 
 */
template <typename T> requires (MemExpendable<T> && MemWritable<T>)
inline auto vsprintfTo(T &buf, const char *fmt, va_list args) -> size_t {
    static_assert(sizeof(buf[0]) == sizeof(char), "buf must be char buffer");
    auto size = vsprintfSize(fmt, args);

    auto curSize = buf.size();
    buf.resize(curSize + size + 1);
    auto n = ::vsnprintf(reinterpret_cast<char *>(buf.data()) + curSize, size + 1, fmt, args);
    buf.resize(curSize + n); // trim the '\0'
    ILIAS_ASSERT(n >= 0);
    return n;
}

/**
 * @brief Print to buffer's back
 * 
 * @tparam T must be MemExpendable and MemWritable, and the value type must be char or byte
 * @param buf 
 * @param fmt 
 * @param ... 
 * @return size_t 
 */
template <typename T> requires (MemExpendable<T> && MemWritable<T>)
inline auto sprintfTo(T &buf, const char *fmt, ...) -> size_t {
    va_list args;
    va_start(args, fmt);
    auto n = vsprintfTo(buf, fmt, args);
    va_end(args);
    return n;
}

/**
 * @brief Print to the string's back using va_list
 * 
 * @tparam T The char type of the string (must size as sizeof(char))
 * @param buf
 * @param fmt
 * @param args
 * @return size_t
 */
template <typename T, typename Traits, typename Alloc> requires (sizeof(T) == sizeof(char))
inline auto vsprintfTo(std::basic_string<T, Traits, Alloc> &buf, const char *fmt, va_list args) -> size_t {
    auto size = vsprintfSize(fmt, args);

    auto len = buf.length();
    buf.resize(len + size); // The string's will auto add the place of the '\0'
    auto n = ::vsprintf(reinterpret_cast<char*>(buf.data() + len), fmt, args);
    ILIAS_ASSERT(n >= 0);
    return n;
}

/**
 * @brief Print to the string's back
 * 
 * @tparam T The char type of the string (must size as sizeof(char))
 * @param buf
 * @param fmt
 * @param ...
 * @return size_t
 */
template <typename T, typename Traits, typename Alloc> requires (sizeof(T) == sizeof(char))
inline auto sprintfTo(std::basic_string<T, Traits, Alloc> &buf, const char *fmt, ...) -> size_t {
    va_list args;
    va_start(args, fmt);
    auto n = vsprintfTo(buf, fmt, args);
    va_end(args);
    return n;
}

ILIAS_NS_END