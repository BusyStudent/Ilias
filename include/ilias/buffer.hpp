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
#include <utility> // std::swap
#include <array> // std::array
#include <span> // std::span
#include <bit> // std::bit_cast

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

// Endianess
/**
 * @brief Check if the system is in network byte order
 * 
 * @return true 
 * @return false 
 */
consteval auto isNetworkOrder() noexcept -> bool {
    return std::endian::native == std::endian::big;
}

/**
 * @brief Swap the bytes of the value
 * 
 * @tparam T 
 */
template <std::integral T>
constexpr auto byteswap(const T value) noexcept -> T { // std::byteswap is C++23, so we need to implement it by ourselves
    if constexpr (sizeof(T) == 1) {
        return value;
    }
#if defined(__GNUC__) || defined(__clang__)
    else if constexpr (sizeof(T) == 2) {
        return std::bit_cast<T>(__builtin_bswap16(std::bit_cast<uint16_t>(value)));
    }
    else if constexpr (sizeof(T) == 4) {
        return std::bit_cast<T>(__builtin_bswap32(std::bit_cast<uint32_t>(value)));
    }
    else if constexpr (sizeof(T) == 8) {
        return std::bit_cast<T>(__builtin_bswap64(std::bit_cast<uint64_t>(value)));
    }
#elif defined(_MSC_VER)
    else if constexpr (sizeof(T) == 2) {
        if (!std::is_constant_evaluated()) 
            return std::bit_cast<T>(_byteswap_ushort(std::bit_cast<uint16_t>(value)));
    }
    else if constexpr (sizeof(T) == 4) {
        if (!std::is_constant_evaluated()) 
            return std::bit_cast<T>(_byteswap_ulong(std::bit_cast<uint32_t>(value)));
    }
    else if constexpr (sizeof(T) == 8) {
        if (!std::is_constant_evaluated()) 
            return std::bit_cast<T>(_byteswap_uint64(std::bit_cast<uint64_t>(value)));
    }
#endif // defined(_MSC_VER)

    // Fallback
    auto bytes = std::bit_cast<std::array<std::byte, sizeof(T)> >(value);
    for (size_t i = 0; i < bytes.size() / 2; ++i) {
        std::swap(bytes[i], bytes[bytes.size() - i - 1]);
    }
    return std::bit_cast<T>(bytes);
}


/**
 * @brief Convert the value from host to network byte order
 * 
 * @tparam T 
 * @param value 
 * @return T 
 */
template <std::integral T>
constexpr auto hostToNetwork(const T value) noexcept -> T {
    if constexpr (isNetworkOrder()) { // Network is big endian
        return value;
    }
    else {
        return byteswap(value);
    }
}

/**
 * @brief Convert the value from network to host byte order
 * 
 * @tparam T 
 * @param value 
 * @return T 
 */
template <std::integral T>
constexpr auto networkToHost(const T value) noexcept -> T {
    if constexpr (isNetworkOrder()) { // Network is big endian
        return value;
    }
    else {
        return byteswap(value);
    }
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