/**
 * @file method.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief For basic traits helper method
 * @version 0.1
 * @date 2024-08-26
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/task/task.hpp>
#include <ilias/io/traits.hpp>
#include <ilias/io/error.hpp>
#include <ilias/buffer.hpp>
#include <cstddef>
#include <span>

ILIAS_NS_BEGIN

// Utility Functions
/**
 * @brief Write All Data to Stream, if failed and has any bytes written, ignore the error, return the written size, if not, return error
 * 
 * @tparam T 
 * @param stream 
 * @param buffer 
 * @return IoTask<size_t> 
 */
template <Writable T>
inline auto writeAll(T &stream, Buffer buffer) -> IoTask<size_t> {
    size_t written = 0;
    while (!buffer.empty()) {
        auto n = co_await stream.write(buffer);
        if (!n) {
            co_return Err(n.error());
        }
        if (*n == 0) {
            co_return Err(IoError::UnexpectedEOF);
        }
        written += *n;
        buffer = buffer.subspan(*n);
    }
    co_return written;
}

/**
 * @brief Read All Data from Stream, if failed and has any bytes readed, ignore the error, return the read size, if not, return error
 * 
 * @tparam T 
 * @param stream 
 * @param buffer 
 * @return IoTask<size_t> 
 */
template <Readable T>
inline auto readAll(T &stream, MutableBuffer buffer) -> IoTask<size_t> {
    size_t read = 0;
    while (!buffer.empty()) {
        auto n = co_await stream.read(buffer);
        if (!n) {
            co_return Err(n.error());
        }
        if (*n == 0) {
            co_return Err(IoError::UnexpectedEOF);
        }
        read += *n;
        buffer = buffer.subspan(*n);
    }
    co_return read;
}

/**
 * @brief Read at least minSize bytes from stream, if failed and has any bytes readed, ignore the error, return the read size, if not, return error
 * 
 * @tparam T 
 * @param stream The stream to read from
 * @param buffer The buffer to read into
 * @param minSize The minimum size to read
 * @return IoTask<size_t> 
 */
template <Readable T>
inline auto readAtleast(T &stream, MutableBuffer buffer, size_t minSize) -> IoTask<size_t> {
    size_t readed = 0;
    while (readed < minSize) {
        auto n = co_await stream.read(buffer.subspan(readed));
        if (!n && readed == 0) {
            co_return Unexpected(n.error());
        }
        if (!n) {
            break;
        }
        if (*n == 0) {
            break;
        }
        readed += *n;
    }
    co_return readed;
}


/**
 * @brief Writeable Helper Method
 * 
 * @tparam T 
 */
template <typename T>
class WritableMethod {
public:
    /**
     * @brief Write All Data to Stream, equal to writeAll(stream, buffer)
     * 
     * @param buffer The 
     * @return IoTask<size_t> 
     */
    auto writeAll(Buffer buffer) -> IoTask<size_t> {
        return ILIAS_NAMESPACE::writeAll(static_cast<T &>(*this), buffer);
    }

    auto operator <=>(const WritableMethod &rhs) const noexcept = default;
};

/**
 * @brief Helper Method for Readable
 * 
 * @tparam T 
 */
template <typename T>
class ReadableMethod {
public:
    /**
     * @brief Read All Data from Stream, equal to readAll(stream, buffer)
     * 
     * @param buffer The 
     * @return IoTask<size_t> 
     */
    auto readAll(MutableBuffer buffer) -> IoTask<size_t> {
        return ILIAS_NAMESPACE::readAll(static_cast<T &>(*this), buffer);
    }
    /**
     * @brief Read at least minSize bytes from stream, if failed and has any bytes readed, ignore the error, return the read size, if not, return error
     * 
     * @tparam T 
     * @param buffer The buffer to read into
     * @param minSize The minimum size to read
     * @return IoTask<size_t> 
     */
    auto readAtleast(MutableBuffer buffer, size_t minSize) -> IoTask<size_t> {
        return ILIAS_NAMESPACE::readAtleast(static_cast<T &>(*this), buffer, minSize);
    }

    auto operator <=>(const ReadableMethod &rhs) const noexcept = default;
};
            
/**
 * @brief Helper for both Readable and Writable
 * 
 * @tparam T 
 */
template <typename T>
class StreamMethod : public WritableMethod<T>, public ReadableMethod<T> {
public:
    auto operator <=>(const StreamMethod &rhs) const noexcept -> std::strong_ordering = default;
};

ILIAS_NS_END