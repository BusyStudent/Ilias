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
#include <ilias/buffer.hpp>
#include <cstddef>
#include <span>

ILIAS_NS_BEGIN

// Forward Declaration
template <Writable T>
inline auto writeAll(T &stream, std::span<const std::byte> buffer) -> IoTask<size_t>;

template <Readable T>
inline auto readAll(T &stream, std::span<std::byte> buffer) -> IoTask<size_t>;

template <MemContainer Container, Readable T>
inline auto readAll(T &stream, size_t firstReadedSize = 4096) -> IoTask<Container>;

/**
 * @brief Writeable Helper Method
 * 
 * @tparam T 
 */
template <typename T>
class WritableMethod {
public:
    /**
     * @brief Write All Data to Stream, equal to writeAll(stream, data)
     * 
     * @param data The 
     * @return IoTask<size_t> 
     */
    auto writeAll(std::span<const std::byte> data) -> IoTask<size_t> {
        return ILIAS_NAMESPACE::writeAll(static_cast<T &>(*this), data);
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
     * @brief Read All Data from Stream, equal to readAll(stream, data)
     * 
     * @param data The 
     * @return IoTask<size_t> 
     */
    auto readAll(std::span<std::byte> data) -> IoTask<size_t> {
        return ILIAS_NAMESPACE::readAll(static_cast<T &>(*this), data);
    }

    /**
     * @brief Read All Data from Stream, equal to readAll<Container>(stream, data, firstReadedSize)
     *  (not recommended for large body, for large body, please use for + read until zero)
     * 
     * @tparam Container 
     * @param firstReadedSize 
     * @return IoTask<Container> 
     */
    template <MemContainer Container>
    auto readAll(size_t firstReadedSize = 4096) -> IoTask<Container> {
        return ILIAS_NAMESPACE::readAll<Container>(static_cast<T &>(*this), firstReadedSize);
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
inline auto writeAll(T &stream, std::span<const std::byte> buffer) -> IoTask<size_t> {
    size_t written = 0;
    while (!buffer.empty()) {
        auto n = co_await stream.write(buffer);
        if (!n && written == 0) {
            // First time write failed, return error
            co_return Unexpected(n.error());
        }
        if (!n) {
            break;
        }
        if (*n == 0) {
            break;
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
inline auto readAll(T &stream, std::span<std::byte> buffer) -> IoTask<size_t> {
    size_t read = 0;
    while (!buffer.empty()) {
        auto n = co_await stream.read(buffer);
        if (!n && read == 0) {
            // First time read failed, return error
            co_return Unexpected(n.error());
        }
        if (!n) {
            break;
        }
        if (*n == 0) {
            break;
        }
        read += *n;
        buffer = buffer.subspan(*n);
    }
    co_return read;
}

/**
 * @brief Read all data from stream, and store it in a container
 *  (not recommended for large body, for large body, please use for + read until zero)
 * 
 * @tparam Container
 * @tparam T 
 * @param stream 
 * @param firstReadedSize The first buffer size of read, default is 4096
 * @return IoTask<Container> If failed, it will discard the readed data and return error
 */
template <MemContainer Container, Readable T>
inline auto readAll(T &stream, size_t firstReadedSize) -> IoTask<Container> {
    Container container;
    size_t readed = 0;
    container.resize(firstReadedSize);

    static_assert(sizeof(container[0]) == sizeof(std::byte), "The container must be a byte container");
    while (true) {
        auto buffer = makeBuffer(container).subspan(readed);
        if (buffer.empty()) {
            container.resize(container.size() * 1.5);
            continue;
        }
        auto n = co_await stream.read(buffer);
        if (!n) {
            co_return Unexpected(n.error());
        }

        readed += *n;
        if (*n == 0) { // End of stream
            break;
        }
    }
    container.resize(readed); // Drop unused bytes
    co_return container;
}

ILIAS_NS_END