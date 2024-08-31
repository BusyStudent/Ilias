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
#include <cstdarg>
#include <cstdio>
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

// --- Utils for writable types

/**
 * @brief Helper class for writing to a buffer
 * 
 * @tparam T 
 */
template <MemWritable T>
class MemWriter {
public:
    /**
     * @brief Construct a new Mem Writer object
     * 
     * @param t 
     * @param offset The offset of the buffer, in bytes
     */
    MemWriter(T &t, size_t offset = 0) : mBuf(t) {
        ILIAS_ASSERT(offset <= std::span(t).size_bytes());
    }

    /**
     * @brief Write data into the buffer
     * 
     * @param data 
     * @return true Success
     * @return false no bytes are written, buffer is no space left (for MemExpendable, the return value is always true)
     */
    auto write(std::span<const std::byte> data) -> bool {
        if (bytesLeft() < data.size()) {
            if (!expandBuffer(data.size())) {
                return false;
            }
        }
        auto buf = left();
        ::memcpy(buf.data(), data.data(), data.size());
        mWritten += data.size();
        return true;
    }

    /**
     * @brief Write data into the buffer, C style version
     * 
     * @param data 
     * @param n 
     * @return true Success
     * @return false no bytes are written, buffer is no space left (for MemExpendable, the return value is always true)
     */
    auto write(const void *data, size_t n) -> bool {
        return write(std::span(static_cast<const std::byte *>(data), n));
    }

    /**
     * @brief Put the string into the buffer
     * 
     * @param str 
     * @return true 
     * @return false 
     */
    auto puts(std::string_view str) -> bool {
        return write(str.data(), str.size());
    }

    /**
     * @brief Put the character into the buffer
     * 
     * @param c 
     * @return true 
     * @return false 
     */
    auto putc(char c) -> bool {
        return write(&c, 1);
    }

    /**
     * @brief Print formatted data into the buffer
     * 
     * @param fmt The C style format string
     * @param args The va_list of arguments
     * @return true as same as write
     * @return false as same as write
     */
    auto vprintf(const char *fmt, va_list args) -> bool {
        va_list varg;
        va_copy(varg, args);

        int n = 0; // Number of bytes should be written
#ifdef _WIN32
        n = ::_vscprintf(fmt, varg);
#else
        n = ::vsnprintf(nullptr, 0, fmt, varg);
#endif
        va_end(varg);

        if (bytesLeft() < n) {
            if (!expandBuffer(n)) {
                return false;
            }
        }

        va_copy(varg, args);
        ::vsnprintf(reinterpret_cast<char *>(left().data()), n + 1, fmt, varg);
        va_end(varg);

        mWritten += n;
        return true;
    }

    /**
     * @brief Print formatted data into the buffer
     * 
     * @param fmt 
     * @param ... 
     * @return true 
     * @return false 
     */
    auto printf(const char *fmt, ...) -> bool {
        va_list args;
        va_start(args, fmt);
        auto ret = vprintf(fmt, args);
        va_end(args);
        return ret;
    }

#if defined(__cpp_lib_format)
    /**
     * @brief Print formatted data into the buffer
     * 
     * @tparam Args 
     * @param fmt 
     * @param args 
     * @return true 
     * @return false 
     */
    template <typename... Args>
    auto print(std::format_string<Args...> fmt, Args &&... args) -> bool {
        auto size = std::formatted_size(fmt, std::forward<Args>(args)...);
        if (bytesLeft() < size) {
            if (!expandBuffer(size)) {
                return false;
            }
        }
        auto buf = left();
        auto begin = reinterpret_cast<char *>(buf.data());
        std::format_to_n(begin, buf.size(), fmt, std::forward<Args>(args)...);
        mWritten += size;
        return true;
    }
#endif

    /**
     * @brief Get the written area of the buffer
     * 
     * @return std::span<std::byte> 
     */
    auto written() const -> std::span<std::byte> {
        return std::as_writable_bytes(std::span(mBuf)).subspan(mOffset, mWritten);
    }

    /**
     * @brief Get the left area of the buffer
     * 
     * @return std::span<std::byte> 
     */
    auto left() const -> std::span<std::byte> {
        return std::as_writable_bytes(std::span(mBuf)).subspan(mOffset + mWritten);
    }

    /**
     * @brief Check how many bytes have been written
     * 
     * @return size_t 
     */
    auto bytesWritten() const -> size_t { return mWritten; }

    /**
     * @brief Check how many bytes are left in the buffer
     * 
     * @return size_t 
     */
    auto bytesLeft() const -> size_t { return left().size_bytes(); }
private:
    auto expandBuffer(size_t n) -> bool {
        if constexpr (MemExpendable<T>) {
            mBuf.resize((mBuf.size() * 2) + n);
            return true;
        }
        else {
            return false;
        }
    }

    T &mBuf;
    size_t mOffset = 0;  //< Offset in buffer
    size_t mWritten = 0; //< How many bytes have been written
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
    return std::span {
        reinterpret_cast<T *>(in.data()),
        in.size_bytes() / sizeof(T)
    };
}

ILIAS_NS_END