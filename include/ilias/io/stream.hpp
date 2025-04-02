/**
 * @file stream.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Stream Buffer & Stream Wrapper classes
 * @version 0.1
 * @date 2024-10-24
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/io/dyn_traits.hpp>
#include <ilias/io/method.hpp>
#include <ilias/io/traits.hpp>
#include <ilias/detail/mem.hpp>
#include <ilias/buffer.hpp>
#include <cstring>
#include <string>
#include <vector>
#include <limits>
#include <array>
#include <span>

// For min max macro in windows.h :(
#undef min
#undef max

ILIAS_NS_BEGIN

/**
 * @brief Concept for Stream Buffer Like, StreamBuffer or FixedStreamBuffer
 * 
 * @tparam T 
 */
template <typename T>
concept StreamBufferLike = requires(T t) {
    t.prepare(0);
    t.commit(0);
    t.data();
    t.size();
    t.consume(0);
};

/**
 * @brief A Buffer for stream, like a pipe (write => output window --- input window => read)
 * 
 */
class StreamBuffer {
public:
    /**
     * @brief Construct a new Stream Buffer object
     * 
     */
    StreamBuffer() = default;

    /**
     * @brief Construct a new Stream Buffer object with a max capacity
     * 
     * @param maxCapacity The max capacity of the buffer
     */
    StreamBuffer(size_t maxCapacity) : mMaxCapacity(maxCapacity) { }

    /**
     * @brief Construct a new Stream Buffer object by moving another
     * 
     * @param other 
     */
    StreamBuffer(StreamBuffer &&other) : 
        mBuffer(std::exchange(other.mBuffer, {})),
        mPos(std::exchange(other.mPos, 0)),
        mTail(std::exchange(other.mTail, 0))
    {
        
    }

    StreamBuffer(const StreamBuffer &) = delete;

    ~StreamBuffer() {
        if (!mBuffer.empty()) {
            std::free(mBuffer.data());
        }
    }

    // Output window
    /**
     * @brief Prepare a buffer for writing into the stream buffer (it make previous parepared buffer to invalid)
     * 
     * @param size The size of the buffer to prepare
     * @return std::span<std::byte> 
     */
    auto prepare(size_t size) -> std::span<std::byte> {
        if (mPos == mTail) { //< Input window is empty, move it and us to the begin of the buffer
            mPos = 0;
            mTail = 0;
        }
        if ((mTail - mPos) < mBuffer.size() / 8) { //< If the input window is too small, move it to the begin of the buffer
            ::memmove(mBuffer.data(), mBuffer.data() + mPos, mTail - mPos);
            mTail -= mPos;
            mPos = 0;
        }
        if ((mTail - mPos) + size > mMaxCapacity) { //< Reach the limit
            return {};
        }
        auto space = mBuffer.size() - mTail; // The space left in the buffer
        if (space < size) { //< Reallocate the buffer if there is not enough space
            auto newSize = std::min((mBuffer.size() + size) * 2, mMaxCapacity);
            auto newBuffer = static_cast<std::byte *>(::realloc(mBuffer.data(), newSize));
            if (newBuffer == nullptr) { //< What?, failed to allocate memory
                return {};
            }
            mBuffer = {newBuffer, newSize};
        }
        return mBuffer.subspan(mTail, size);
    }

    /**
     * @brief Commit the size of data from output window into the input window
     * 
     * @param size The size of data to commit (can't exceed the capacity of the output window)
     */
    auto commit(size_t size) -> void {
        ILIAS_ASSERT_MSG(size <= (mBuffer.size() - mTail), "Commit size exceed the capacity");
        size = std::min(size, mBuffer.size() - mTail); //< In release version, the assert may be removed, so we add it
        mTail += size;
    }

    // Input Window
    /**
     * @brief Get the stream buffer's input window
     * 
     * @return std::span<std::byte> 
     */
    auto data() const -> std::span<const std::byte> {
        return mBuffer.subspan(mPos, mTail - mPos);
    }

    /**
     * @brief Get the stream buffer's input window (mutable)
     * 
     * @return std::span<std::byte> 
     */
    auto data() -> std::span<std::byte> {
        return mBuffer.subspan(mPos, mTail - mPos);
    }

    /**
     * @brief Get the size of the input window
     * 
     * @return size_t 
     */
    auto size() const -> size_t {
        return mTail - mPos;
    }

    /**
     * @brief Check the stream buffer's input window is empty or not
     * 
     * @return true 
     * @return false 
     */
    auto empty() const -> bool {
        return mPos == mTail;
    }

    /**
     * @brief Consume the size of data from the input window
     * 
     * @param size The size of data to consume (can't exceed the capacity of the input window)
     */
    auto consume(size_t size) -> void {
        ILIAS_ASSERT_MSG(size <= (mTail - mPos), "Consume size exceed the capacity");
        size = std::min(size, mTail - mPos);
        mPos += size;
    }

    // Misc
    /**
     * @brief Get the size of the whole stream buffer (output + input window)
     * 
     * @return size_t 
     */
    auto capacity() const -> size_t {
        return mBuffer.size();
    }

    /**
     * @brief Get the maximum capacity of the stream buffer
     * 
     * @return size_t 
     */
    auto maxCapacity() const -> size_t {
        return mMaxCapacity;
    }

    /**
     * @brief shrink the whole buffer to input window size, and drop the output window
     * 
     */
    auto shrinkToFit() -> void {
        if (size() > 0) {
            ::memmove(mBuffer.data(), mBuffer.data() + mPos, mTail - mPos);
            mTail -= mPos;
            mPos = 0;
        }
        auto newBuffer = static_cast<std::byte *>(std::realloc(mBuffer.data(), mTail));
        ILIAS_ASSERT_MSG(newBuffer != nullptr, "Failed to allocate memory");
        mBuffer = {newBuffer, mTail};
    }

    /**
     * @brief Set the Max Capacity of the stream buffer, it just set the limit of the buffer, won't shrink the buffer if it's already larger than the limit
     * 
     * @param capacity 
     */
    auto setMaxCapacity(size_t capacity) -> void {
        mMaxCapacity = capacity;
    }

    /**
     * @brief Clear the whole stream buffer
     * 
     */
    auto clear() -> void {
        ::free(mBuffer.data());
        mBuffer = { };
        mPos = 0;
        mTail = 0;
    }

    /**
     * @brief Move another stream buffer into this one
     * 
     * @param other 
     * @return StreamBuffer& 
     */
    auto operator = (StreamBuffer &&other) -> StreamBuffer & {
        if (this == &other) {
            return *this;
        }
        mBuffer = std::exchange(other.mBuffer, mBuffer); //< Exchange the buffer
        mPos = std::exchange(other.mPos, 0);
        mTail = std::exchange(other.mTail, 0);
        return *this;
    }
    auto operator = (const StreamBuffer &) = delete;
private:
    /**
     *  Memory Layout | Input Window  | Output Window | mCapacity
     * 
     */
    std::span<std::byte> mBuffer;
    size_t mPos = 0; //< Current position to read (input window)
    size_t mTail = 0; //< Current position to write (output window)
    size_t mMaxCapacity = std::numeric_limits<size_t>::max(); //< The maximum capacity of the buffer
};

/**
 * @brief The Stream Buffer with fixed size, non memory allocation
 * 
 * @tparam N The capacity of the buffer (Must be greater than 0)
 */
template <size_t N> requires (N > 0)
class FixedStreamBuffer {
public:
    /**
     * @brief Construct a new Fixed Stream Buffer object
     * 
     */
    FixedStreamBuffer() = default;

    // Output window
    /**
     * @brief Prepare a buffer for writing into the stream buffer (it make previous parepared buffer to invalid)
     * @note if the size is too large, it will return an empty buffer
     * 
     * @param size The size of the buffer to prepare
     * @return std::span<std::byte> 
     */
    auto prepare(size_t size) -> std::span<std::byte> {
        if (mPos == mTail) { //< Input window is empty, move it and us to the begin of the buffer
            mPos = 0;
            mTail = 0;
        }
        if ((mTail - mPos) < mBuffer.size() / 8) { //< If the input window is too small, move it to the begin of the buffer
            ::memmove(mBuffer.data(), mBuffer.data() + mPos, mTail - mPos);
            mTail -= mPos;
            mPos = 0;
        }
        auto space = mBuffer.size() - mTail; // The space left in the buffer
        if (space < size) {
            return {};
        }
        return std::span(mBuffer).subspan(mTail, size);
    }

    /**
     * @brief Commit the size of data from output window into the input window
     * 
     * @param size The size of data to commit (can't exceed the capacity of the output window)
     */
    auto commit(size_t size) -> void {
        ILIAS_ASSERT_MSG(size <= (mBuffer.size() - mTail), "Commit size exceed the capacity");
        size = std::min(size, mBuffer.size() - mTail); //< In release version, the assert may be removed, so we add it
        mTail += size;
    }

    // Input Window
    /**
     * @brief Get the data of the input window
     * 
     * @return std::span<const std::byte> 
     */
    auto data() const -> std::span<const std::byte> {
        return {mBuffer.data() + mPos, mTail - mPos};
    }

    /**
     * @brief Get the data of the input window (mutable)
     * 
     * @return std::span<std::byte> 
     */
    auto data() -> std::span<std::byte> {
        return {mBuffer.data() + mPos, mTail - mPos};
    }

    /**
     * @brief Get the size of the input window
     * 
     * @return size_t 
     */
    auto size() const -> size_t {
        return mTail - mPos;
    }

    /**
     * @brief Check the input window is empty or not
     * 
     * @return true 
     * @return false 
     */
    auto empty() const -> bool {
        return mPos == mTail;
    }

    /**
     * @brief Consume the size of data from the input window
     * 
     * @param size The size of data to consume (can't exceed the capacity of the input window)
     */
    auto consume(size_t size) -> void {
        ILIAS_ASSERT_MSG(size <= (mTail - mPos), "Consume size exceed the capacity");
        size = std::min(size, mTail - mPos);
        mPos += size;
    }

    // Misc
    /**
     * @brief Clear the whole stream buffer
     * 
     */
    auto clear() -> void {
        mPos = 0;
        mTail = 0;
    }

    /**
     * @brief Get the size of the whole stream buffer (output + input window)
     * 
     * @return size_t 
     */
    auto capacity() const -> size_t {
        return N;
    }
private:
    std::array<std::byte, N> mBuffer;
    size_t mPos = 0;
    size_t mTail = 0;
};

/**
 * @brief A Wrapper for buffered a stream
 * 
 * @tparam T The underlying stream
 */
template <Stream T = DynStreamClient>
class BufferedStream final : public StreamMethod<BufferedStream<T> > {
public:
    /**
     * @brief Construct a new empty Buffered Stream object
     * 
     */
    BufferedStream() = default;

    BufferedStream(BufferedStream &&) = default;

    BufferedStream(const BufferedStream &) = delete;

    BufferedStream(T &&stream) : mStream(std::move(stream)) { }

    /**
     * @brief Read a new line by delim character
     * 
     * @param delim 
     * @return IoTask<std::string> 
     */
    auto getline(std::string_view delim) -> IoTask<std::string> {
        size_t lastPos = 0; // Last position of we scanned
        while (true) {
            // Scanning current buffer
            auto buf = mBuf.data();
            if (buf.size() >= delim.size()) {
                ILIAS_ASSERT_MSG(buf.size() >= lastPos, "Buffer size is smaller than last we scanned");
                std::string_view view(reinterpret_cast<const char*>(buf.data()), buf.size());
                size_t pos = view.find(delim, lastPos);
                if (pos != std::string_view::npos) {
                    // Found the delimiter
                    auto content = view.substr(0, pos);
                    mBuf.consume(pos + delim.size());
                    co_return std::string(content);
                }
                lastPos = buf.size(); // We already scanned the whole buffer, so we start from the beginning
            }
            // Try fill the buffer
            auto wbuf = mBuf.prepare(4096 + delim.size());
            if (wbuf.empty()) {
                co_return Unexpected(Error::OutOfMemory);
            }
            auto ret = co_await mStream.read(wbuf);
            if (!ret || *ret == 0) {
                co_return Unexpected(ret.error_or(Error::ZeroReturn));
            }
            mBuf.commit(*ret);
        }
    }

    /**
     * @brief Write data to the stream
     * 
     * @param buffer 
     * @return IoTask<size_t> 
     */
    auto write(std::span<const std::byte> buffer) -> IoTask<size_t> {
        return mStream.write(buffer);
    }

    /**
     * @brief Read data from the stream
     * 
     * @param buffer 
     * @return IoTask<size_t> 
     */
    auto read(std::span<std::byte> buffer) -> IoTask<size_t> {
        const auto n = buffer.size();
        while (true) {
            auto buf = mBuf.data();
            if (!buf.empty() || n == 0) {
                // Read data from the buffer
                auto len = std::min(buf.size(), n);
                ::memcpy(buffer.data(), buf.data(), len);
                mBuf.consume(len);
                co_return len;
            }
            // Try fill the buffer
            auto wbuf = mBuf.prepare(n);
            if (wbuf.empty()) {
                co_return Unexpected(Error::OutOfMemory);
            }
            auto ret = co_await mStream.read(wbuf);
            if (!ret) {
                co_return Unexpected(ret.error());
            }
            if (*ret == 0) {
                co_return 0;
            }
            mBuf.commit(*ret);
        }
    }

    /**
     * @brief Connect to a remote endpoint
     * 
     * @tparam U 
     */
    template <Connectable U = T, typename EndpointLike>
    auto connect(const EndpointLike &endpoint) -> IoTask<void> {
        return static_cast<U&>(mStream).connect(endpoint);
    }

    /**
     * @brief Shutdown the stream
     * 
     * @tparam U 
     */
    template <Shuttable U = T>
    auto shutdown() -> IoTask<void> {
        return static_cast<U&>(mStream).shutdown();
    }

    /**
     * @brief Get the underlying stream buffer object
     * 
     * @return const StreamBuffer & 
     */
    auto buffer() const -> const StreamBuffer & {
        return mBuf;
    }

    /**
     * @brief Get the underlying stream buffer object
     * 
     * @return StreamBuffer & 
     */
    auto buffer() -> StreamBuffer & {
        return mBuf;
    }

    /**
     * @brief Get the underlying stream object
     * 
     * @return const T & 
     */
    auto stream() const -> const T & {
        return mStream;
    }

    /**
     * @brief Get the underlying stream object
     * 
     * @return T & 
     */
    auto stream() -> T & {
        return mStream;
    }

    /**
     * @brief Move another buffered stream into this one
     * 
     * @param other 
     * @return BufferedStream& 
     */
    auto operator = (BufferedStream &&other) -> BufferedStream & {
        mStream = std::move(other.mStream);
        mBuf = std::move(other.mBuf);
        return *this;
    }

    /**
     * @brief Check the stream is valid or not
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept {
        return bool(mStream);
    }
private:
    T mStream;
    StreamBuffer mBuf;
};

// Utility functions

/**
 * @brief Read bytes from the stream into buffer until the delimiter is found
 * 
 * @tparam T 
 * @tparam Stream 
 * @param stream The stream to read from (must has readable concept)
 * @param streamBuf The stream buffer to read into (must has stream buffer concept)
 * @param delim The delimiter to stop reading
 * @return IoTask<size_t> The position of the delimiter in the buffer
 */
template <StreamBufferLike T, Readable Stream>
inline auto readUntil(Stream &stream, T &streamBuf, std::span<const std::byte> delim) -> IoTask<size_t> {
    size_t lastPos = 0;
    while (true) {
        auto span = streamBuf.data();
        if (span.size() >= delim.size()) { // We can scan it
            auto pos = mem::memmem(span.subspan(lastPos), delim);
            if (pos) { // Got it!
                co_return *pos + lastPos;
            }
            lastPos = span.size();
        }
        // Try read more data!
        auto wbuf = streamBuf.prepare(4096 + delim.size());
        if (wbuf.empty()) {
            co_return Unexpected(Error::OutOfMemory);
        }
        auto ret = co_await stream.read(wbuf);
        if (!ret || *ret == 0) {
            co_return Unexpected(ret.error_or(Error::ZeroReturn));
        }
        streamBuf.commit(*ret);
    }
}

/**
 * @brief print the string into the stream buffer's input window
 * 
 * @tparam T 
 * @param stream 
 * @param fmt 
 * @param args 
 * @return size_t The number of bytes written (0 on failed to prepare the space)
 */
template <StreamBufferLike T>
inline auto vsprintfTo(T &streamBuf, const char *fmt, va_list args) -> size_t {
    va_list copy;
    va_copy(copy, args);
    auto size = vsprintfSize(fmt, args);
    auto buf = streamBuf.prepare(size + 1); // +1 for the null terminator
    if (buf.empty() || size == 0) {
        return 0;
    }
    size = ::vsnprintf(reinterpret_cast<char*>(buf.data()), buf.size(), fmt, copy);
    streamBuf.commit(size); // commit discard the null terminator
    va_end(copy);
    return size;
}

/**
 * @brief print the string into the stream buffer's input window
 * 
 * @tparam T 
 * @param stream 
 * @param fmt 
 * @param ... 
 * @return size_t The number of bytes written (0 on failed to prepare the space)
 */
template <StreamBufferLike T>
inline auto sprintfTo(T &streamBuf, const char *fmt, ...) -> size_t {
    va_list args;
    va_start(args, fmt);
    auto size = vsprintfTo(streamBuf, fmt, args);
    va_end(args);
    return size;
}

// Format support
#if !defined(ILIAS_NO_FORMAT)
/**
 * @brief Format the string into the stream buffer's input window
 * 
 * @tparam T 
 * @tparam Args 
 * @param streamBuf 
 * @param fmt 
 * @param args 
 * @return size_t The number of bytes written (0 on failed to prepare the space)
 */
template <StreamBufferLike T, typename ...Args>
inline auto formatTo(T &streamBuf, fmtlib::format_string<Args...> fmt, Args &&...args) -> size_t {
    auto size = fmtlib::formatted_size(fmt, std::forward<Args>(args)...);
    auto buf = streamBuf.prepare(size);
    if (buf.empty() || size == 0) {
        return 0;
    }
    auto res = fmtlib::format_to_n(reinterpret_cast<char*>(buf.data()), buf.size(), fmt, std::forward<Args>(args)...);
    buf.commit(res.size);
    return res.size;
}
#endif // !defined(ILIAS_NO_FORMAT)

ILIAS_NS_END