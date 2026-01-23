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
 * @brief Write all data to stream
 * 
 * @tparam T 
 * @param stream The stream to write to
 * @param buffer The buffer containing data to write
 * @return IoTask<size_t> Total bytes written (equal to buffer.size()), or error if any write fails
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
            co_return Err(IoError::WriteZero);
        }
        written += *n;
        buffer = buffer.subspan(*n);
    }
    co_return written;
}

/**
 * @brief Read all data from stream
 * 
 * @tparam T 
 * @param stream The stream to read from
 * @param buffer The buffer to read into
 * @return IoTask<size_t> Total bytes read (equal to buffer.size()), or error if any read fails
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
 * @brief Read at least minSize bytes from stream
 * 
 * @tparam T 
 * @param stream The stream to read from
 * @param buffer The buffer to read into
 * @param minSize The minimum number of bytes to read
 * @return IoTask<size_t> Total bytes read (>= minSize), or error if any read fails
 */
template <Readable T>
inline auto readAtleast(T &stream, MutableBuffer buffer, size_t minSize) -> IoTask<size_t> {
    size_t readed = 0;
    if (buffer.size() < minSize) { // We can't read enough data to the buffer
        co_return Err(IoError::InvalidArgument);
    }
    while (readed < minSize) {
        auto n = co_await stream.read(buffer.subspan(readed));
        if (!n && readed == 0) {
            co_return Err(n.error());
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
 * @brief Read all data from stream and append to container
 * 
 * @tparam T 
 * @tparam Container 
 * @param stream The stream to read from
 * @param container The container to append data to
 * @return IoTask<size_t> Total bytes read, or error if any read fails
 */
template <Readable T, MemWritable Container>
inline auto readToEnd(T &stream, Container &container) -> IoTask<size_t> {
    size_t size = container.size();
    size_t readed = 0;
    while (true) {
        container.resize(size + 1024);
        auto buf = makeBuffer(container).subspan(size);
        while (!buf.empty()) {
            auto n = co_await stream.read(buf);
            if (!n) { // Error resize the back
                container.resize(size);
                co_return Err(n.error());
            }
            if (*n == 0) { // EOF
                container.resize(size);
                co_return readed;
            }
            readed += *n;
            size += *n;
            buf = buf.subspan(*n);
        }
    }
}

/**
 * @brief Get the lowest layer of the layered stream
 * 
 * @tparam T 
 * @param layer 
 * @return decltype(auto) 
 */
template <Layer T>
inline auto lowestLayer(T &layer) -> decltype(auto) {
    auto walk = [](auto self, auto &cur) -> decltype(auto) {
        if constexpr (Layer<decltype(cur)>) {
            return self(self, cur.nextLayer());
        }
        else {
            return cur;
        }
    };
    return walk(walk, layer);
}

/**
 * @brief Helper class for Writable types
 * 
 * @tparam T 
 */
template <typename T>
class WritableMethod {
public:
    /**
     * @brief Write All Data to Stream, equal to writeAll(stream, buffer)
     * 
     * @param buffer The buffer containing data to write
     * @return IoTask<size_t> Total bytes written (equal to buffer.size()), or error if any write fails
     */
    auto writeAll(Buffer buffer) -> IoTask<size_t> requires Writable<T> {
        return ::ilias::writeAll(static_cast<T &>(*this), buffer);
    }

    auto operator <=>(const WritableMethod &rhs) const noexcept = default;
};

/**
 * @brief Helper class for Readable types
 * 
 * @tparam T 
 */
template <typename T>
class ReadableMethod {
public:
    /**
     * @brief Read All Data from Stream, equal to readAll(stream, buffer)
     * 
     * @param buffer The buffer to read into
     * @return IoTask<size_t> Total bytes read (equal to buffer.size()), or error if any read fails
     */
    auto readAll(MutableBuffer buffer) -> IoTask<size_t> requires(Readable<T>) {
        return ::ilias::readAll(static_cast<T &>(*this), buffer);
    }

    /**
     * @brief Read at least minSize bytes from stream, equal to readAtleast(stream, buffer, minSize)
     * 
     * @tparam T 
     * @param buffer The buffer to read into
     * @param minSize The minimum number of bytes to read
     * @return IoTask<size_t> 
     */
    auto readAtleast(MutableBuffer buffer, size_t minSize) -> IoTask<size_t> requires(Readable<T>) {
        return ::ilias::readAtleast(static_cast<T &>(*this), buffer, minSize);
    }

    /**
     * @brief Read all data from stream to container, append to container, equal to readToEnd(stream, container)
     * 
     * @tparam Container 
     * @param container The container to append data to
     * @return IoTask<size_t>
     */
    template <MemWritable Container>
    auto readToEnd(Container &container) -> IoTask<size_t> requires(Readable<T>) {
        return ::ilias::readToEnd(static_cast<T &>(*this), container);
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