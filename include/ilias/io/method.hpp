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
#include <concepts>
#include <cstddef>
#include <span>
#include <bit>

// For .writeU8 writeU16BE etc....
#define ILIAS_DEFINE_EXT_WRITE(name, type)                                     \
    template <char = 0>                                                        \
    auto write##name(type value) -> IoTask<void> requires Writable<T> {        \
        return io::write##name(static_cast<T &>(*this), value);                \
    }

#define ILIAS_DEFINE_EXT_READ(name, type)                                      \
    template <char = 0>                                                        \
    auto read##name() -> IoTask<type> requires Readable<T> {                   \
        return io::read##name(static_cast<T &>(*this));                        \
    }

#define ILIAS_DEFINE_EXT_READ_INT_PAIR(bits)                                   \
    ILIAS_DEFINE_EXT_READ(U##bits##Be, uint##bits##_t)                         \
    ILIAS_DEFINE_EXT_READ(U##bits##Le, uint##bits##_t)                         \
    ILIAS_DEFINE_EXT_READ(I##bits##Be, int##bits##_t)                          \
    ILIAS_DEFINE_EXT_READ(I##bits##Le, int##bits##_t)

#define ILIAS_DEFINE_EXT_WRITE_INT_PAIR(bits)                                  \
    ILIAS_DEFINE_EXT_WRITE(U##bits##Be, uint##bits##_t)                        \
    ILIAS_DEFINE_EXT_WRITE(U##bits##Le, uint##bits##_t)                        \
    ILIAS_DEFINE_EXT_WRITE(I##bits##Be, int##bits##_t)                         \
    ILIAS_DEFINE_EXT_WRITE(I##bits##Le, int##bits##_t)                         \

// For io::writeU8 io::readU8 etc....
#define ILIAS_DEFINE_IO_INT_FUNC(name, type, endian_)                          \
    template <Writable T>                                                      \
    inline auto write##name(T &stream, type value) -> IoTask<void> {           \
        return io::writeInt<std::endian::endian_>(stream, value);              \
    }                                                                          \
    template <Readable T>                                                      \
    inline auto read##name(T &stream) -> IoTask<type> {                        \
        return io::readInt<std::endian::endian_, type>(stream);                \
    }                                                                          \

#define ILIAS_DEFINE_IO_INT_PAIR(bits)                                         \
    ILIAS_DEFINE_IO_INT_FUNC(U##bits##Be, uint##bits##_t, big)                 \
    ILIAS_DEFINE_IO_INT_FUNC(U##bits##Le, uint##bits##_t, little)              \
    ILIAS_DEFINE_IO_INT_FUNC(I##bits##Be, int##bits##_t,  big)                 \
    ILIAS_DEFINE_IO_INT_FUNC(I##bits##Le, int##bits##_t,  little)


ILIAS_NS_BEGIN

// Utility functions for io traits
namespace io {

// MARK: Writable
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
 * @brief Write a single integer to stream
 * 
 * @tparam E The target endian
 * @tparam T The stream to write to
 * @tparam Int The integer type
 * @param stream 
 * @param value 
 * @return IoTask<void> 
 */
template <std::endian E, Writable T, std::integral Int>
inline auto writeInt(T &stream, Int value) -> IoTask<void> {
    if constexpr (std::endian::native != E) {
        value = byteswap(value);
    }
    if (auto res = co_await io::writeAll(stream, makeBuffer(&value, sizeof(value))); !res) {
        co_return Err(res.error());
    }
    co_return {};
}

// MARK: Readable
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
 * @brief Read a single integer from stream
 * 
 * @tparam E The source endian
 * @tparam Int The integer type
 * @tparam T 
 * @param stream 
 * @return IoTask<Int> The integer read, or error if any read fails
 */
template <std::endian E, std::integral Int, Readable T>
inline auto readInt(T &stream) -> IoTask<Int> {
    Int value {};
    if (auto res = co_await io::readAll(stream, makeBuffer(&value, sizeof(value))); !res) {
        co_return Err(res.error());
    }
    if constexpr (std::endian::native != E) {
        value = byteswap(value);
    }
    co_return value;
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

// MARK: BufReadable
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

// Read / Wrtie Integer
ILIAS_DEFINE_IO_INT_FUNC(U8, uint8_t, big);
ILIAS_DEFINE_IO_INT_FUNC(I8, int8_t,  big);
ILIAS_DEFINE_IO_INT_PAIR(16);
ILIAS_DEFINE_IO_INT_PAIR(32);
ILIAS_DEFINE_IO_INT_PAIR(64);

} // namespace io

/**
 * @brief Extension for Writable types
 * 
 * @tparam T 
 */
template <typename T>
class WritableExt {
public:
    // MARK: Writable
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

    /**
     * @brief Write all string data to stream, equal to writeAll(stream, makeBuffer(string_view{str}))
     * 
     * @param str The string
     * @return IoTask<size_t> Total bytes written (equal to str.size() * sizeof(Char)), or error if any write fails
     */
    template <char = 0> 
    auto writeString(std::string_view str) -> IoTask<size_t> requires Writable<T> {
        return io::writeAll(static_cast<T &>(*this), makeBuffer(str));
    }

    // Write integer family
    ILIAS_DEFINE_EXT_WRITE(U8, uint8_t);
    ILIAS_DEFINE_EXT_WRITE(I8, int8_t);
    ILIAS_DEFINE_EXT_WRITE_INT_PAIR(16);
    ILIAS_DEFINE_EXT_WRITE_INT_PAIR(32);
    ILIAS_DEFINE_EXT_WRITE_INT_PAIR(64);

    auto operator <=>(const WritableExt &rhs) const noexcept = default;
};

/**
 * @brief Extension for Readable types
 * 
 * @tparam T 
 */
template <typename T>
class ReadableExt {
public:
    // MARK: Readable
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

    // Read integer family
    ILIAS_DEFINE_EXT_READ(U8, uint8_t);
    ILIAS_DEFINE_EXT_READ(I8, int8_t);
    ILIAS_DEFINE_EXT_READ_INT_PAIR(16);
    ILIAS_DEFINE_EXT_READ_INT_PAIR(32);
    ILIAS_DEFINE_EXT_READ_INT_PAIR(64);

    // MARK: BufReadable
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

    auto operator <=>(const ReadableExt &rhs) const noexcept = default;
};
            
/**
 * @brief Extension for both Readable and Writable
 * 
 * @tparam T 
 */
template <typename T>
class StreamExt : public WritableExt<T>, public ReadableExt<T> {
public:
    auto operator <=>(const StreamExt &rhs) const noexcept -> std::strong_ordering = default;
};

// Compatible with old code
template <typename T>
using ReadableMethod [[deprecated("Use ReadableExt instead")]] = ReadableExt<T>;
template <typename T>
using WritableMethod [[deprecated("Use WritableExt instead")]] = WritableExt<T>;
template <typename T>
using StreamMethod [[deprecated("Use StreamExt instead")]] = StreamExt<T>;

ILIAS_NS_END