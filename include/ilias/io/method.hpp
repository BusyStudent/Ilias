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

// Utility functions for io traits
namespace io {

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
 * @brief Read a line from stream and append to string
 * 
 * @param stream The stream to read from
 * @param str The string to append data to
 * @param delim The delimiter to look for (default: "\n")
 * @return IoTask<size_t> Number of bytes read (including delimiter on not EOF), 
 *         or error if read fails
 */
template <BufReadable T>
inline auto readline(T &stream, std::string &str, std::string_view delim = "\n") -> IoTask<size_t> {
    size_t readed = 0;
    while (true) {
        auto buf = co_await stream.fill(FillPolicy::None);
        if (!buf) {
            co_return Err(buf.error());
        }
        if (buf->empty() && readed != 0) { // EOF, but we have readed some data
            co_return readed;
        }
        if (buf->empty()) { // EOF
            co_return Err(IoError::UnexpectedEOF);
        }
        auto view = std::string_view {reinterpret_cast<const char *>(buf->data()), buf->size()};
        auto pos = view.find(delim);
        if (pos == std::string_view::npos) {
            str.append(view);
            stream.consume(view.size()); 
            readed += view.size();
            continue;
        }
        else {
            auto sub = view.substr(0, pos + delim.size());
            str.append(sub);
            stream.consume(sub.size());
            readed += sub.size();
            break;
        }
    }
    co_return readed;
}

/**
 * @brief Get a line from stream
 * 
 * @tparam T 
 * @param stream The stream to read from
 * @param delim The delimiter to look for (default: "\n")
 * @return IoTask<std::string> The line read (Not including delimiter), or error if read fails
 */
template <BufReadable T>
inline auto getline(T &stream, std::string_view delim = "\n") -> IoTask<std::string> {
    std::string str {};
    auto res = co_await io::readline(stream, str, delim);
    if (!res) {
        co_return Err(res.error());
    }
    if (str.ends_with(delim)) {
        str.resize(str.size() - delim.size());
    }
    co_return str;
}


/**
 * @brief Copy all data from src to dst
 * 
 * @note This function will read until EOF(0)
 * @tparam T 
 * @tparam U 
 * @param dst The stream to write to
 * @param src The stream to read from
 * @return IoTask<size_t> The total number of bytes copied, or error if any read or write fails
 */
template <Writable T, Readable U>
inline auto copy(T &dst, U &src) -> IoTask<size_t> {
    std::byte buffer[1024 * 8]; // 8KB buffer, change the size if needed
    size_t written = 0;
    while (true) {
        auto readed = co_await src.read(buffer);
        if (!readed) {
            co_return Err(readed.error());
        }
        if (*readed == 0) { // EOF
            break;
        }
        if (auto res = co_await io::writeAll(dst, std::span{buffer}.subspan(0, *readed)); !res) {
            co_return Err(res.error());
        }
        written += *readed;
    }
    co_return written;
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

} // namespace io

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
    template <char = 0>
    auto writeAll(Buffer buffer) -> IoTask<size_t> requires Writable<T> {
        return io::writeAll(static_cast<T &>(*this), buffer);
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
     * @brief Read All Data from Stream
     * @note equal to io::readAll(stream, buffer)
     * 
     * @param buffer The buffer to read into
     * @return IoTask<size_t> Total bytes read (equal to buffer.size()), or error if any read fails
     */
    template <char = 0>
    auto readAll(MutableBuffer buffer) -> IoTask<size_t> requires(Readable<T>) {
        return io::readAll(static_cast<T &>(*this), buffer);
    }

    /**
     * @brief Read all data from stream to container, append to container
     * @note equal to io::readToEnd(stream, container)
     * 
     * @tparam Container 
     * @param container The container to append data to
     * @return IoTask<size_t>
     */
    template <MemWritable Container>
    auto readToEnd(Container &container) -> IoTask<size_t> requires(Readable<T>) {
        return io::readToEnd(static_cast<T &>(*this), container);
    }

    /**
     * @brief Copy all data from self to dst
     * @note equal to io::copy(dst, self)
     * 
     * @tparam U 
     * @param dst The stream to write to
     * @return IoTask<size_t> The total number of bytes copied, or error if any read or write fails
     */
    template <Writable U>
    auto copyTo(U &dst) -> IoTask<size_t> requires(Readable<T>) {
        return io::copy(dst, static_cast<T &>(*this));
    }

    /**
     * @brief Read a line from stream
     * @note equal to io::readline(stream, str, delim)
     * 
     * @param str The string to read into
     * @param delim The delimiter to use, default to "\n"
     * 
     * @return IoTask<size_t> The number of bytes read, or error if any read fails
     */
    template <char = 0>
    auto readline(std::string &str, std::string_view delim = "\n") -> IoTask<size_t> requires(BufReadable<T>) {
        return io::readline(static_cast<T &>(*this), str, delim);
    }

    /**
     * @brief Get a line from stream
     * @note equal to io::getline(stream, delim)
     * 
     * @param delim The delimiter to use, default to "\n"
     * 
     * @return IoTask<std::string> The line read (Not including the delimiter), or error if any read fails
     */
    template <char = 0>
    auto getline(std::string_view delim = "\n") -> IoTask<std::string> requires(BufReadable<T>) {
        return io::getline(static_cast<T &>(*this), delim);
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