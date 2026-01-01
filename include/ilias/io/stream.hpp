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

#include <ilias/io/method.hpp>
#include <ilias/io/traits.hpp> // Readable Writable
#include <ilias/buffer.hpp> // Buffer MutableBuffer
#include <utility> // std::min
#include <string> // std::string
#include <vector> // std::vector
#include <array> // std::array
#include <limits>

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
    StreamBuffer(size_t maxCapacity) : mMaxCapacity(maxCapacity) {}

    /**
     * @brief Construct a new Stream Buffer object by moving another
     * 
     * @param other 
     */
    StreamBuffer(StreamBuffer &&other) = default;
    StreamBuffer(const StreamBuffer &) = delete;

    // Output window
    /**
     * @brief Prepare a buffer for writing into the stream buffer (it make previous parepared buffer to invalid)
     * 
     * @param size The size of the buffer to prepare
     * @return MutableBuffer 
     */
    auto prepare(size_t size) -> MutableBuffer {
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
            mBuffer.resize(newSize);
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
     * @brief Get the stream buffer's input window
     * 
     * @return Buffer 
     */
    auto data() const -> Buffer {
        return std::span(mBuffer).subspan(mPos, mTail - mPos);
    }

    /**
     * @brief Get the stream buffer's input window (mutable)
     * 
     * @return MutableBuffer 
     */
    auto data() -> MutableBuffer {
        return std::span(mBuffer).subspan(mPos, mTail - mPos);
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
        mBuffer.resize(mTail);
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
        mBuffer.clear();
        mPos = 0;
        mTail = 0;
    }

    /**
     * @brief Move another stream buffer into this one
     * 
     * @param other 
     * @return StreamBuffer& 
     */
    auto operator = (StreamBuffer &&other) -> StreamBuffer & = default;
    auto operator = (const StreamBuffer &) = delete;
private:
    /**
     *  Memory Layout | Input Window  | Output Window | mCapacity
     * 
     */
    std::vector<std::byte> mBuffer;
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
     * @return MutableBuffer 
     */
    auto prepare(size_t size) -> MutableBuffer {
        if (mPos == mTail) { //< Input window is empty, move it and us to the begin of the buffer
            mPos = 0;
            mTail = 0;
        }
        auto space = mBuffer.size() - mTail; // The space left in the buffer
        if ((mTail - mPos) < mBuffer.size() / 8 || space < size) { //< If the input window is too small, or left space is too small, move it to the begin of the buffer
            ::memmove(mBuffer.data(), mBuffer.data() + mPos, mTail - mPos);
            mTail -= mPos;
            mPos = 0;
            space = mBuffer.size() - mTail; // Recalculate it, tail changed
        }
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
     * @return Buffer 
     */
    auto data() const -> Buffer {
        return {mBuffer.data() + mPos, mTail - mPos};
    }

    /**
     * @brief Get the data of the input window (mutable)
     * 
     * @return MutableBuffer
     */
    auto data() -> MutableBuffer {
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

    auto maxCapacity() const -> size_t {
        return N;
    }
private:
    std::array<std::byte, N> mBuffer;
    size_t mPos = 0;
    size_t mTail = 0;
};

enum class FillPolicy {
    None, // Only fill the buffer when it's empty
    More, // Always fill the buffer with more data
};

/**
 * @brief The default buffer capacity when you create a BufReader / BufWriter / BufStream, currently 4K
 * 
 */
inline constexpr size_t DEFAULT_BUFFER_CAPACITY = 4096;

/**
 * @brief Wrap a readable stream with a buffer
 * 
 * @tparam T 
 */
template <Readable T>
class BufReader final : public StreamMethod<BufReader<T> > {
public:
    /**
     * @brief Construct a new Buf Reader with capacity 
     * 
     * @param stream The stream to wrap
     * @param capacity The capacity of the buffer
     */
    BufReader(T stream, size_t capacity = DEFAULT_BUFFER_CAPACITY) : mBuffer(capacity), mStream(std::move(stream)) {}
    BufReader(BufReader &&) = default;
    BufReader() = default;

    // Readable
    auto read(MutableBuffer buffer) -> IoTask<size_t> {
        // No data cached && This buffer is much bigger, read directly
        if (mBuffer.empty() && buffer.size() >= mBuffer.maxCapacity() / 2) {
            co_return co_await mStream.read(buffer);
        }
        auto data = co_await fill();
        if (!data) {
            co_return Err(data.error());
        }
        if (data->empty()) { // EOF
            co_return 0;
        }
        auto size = std::min(buffer.size(), data->size());
        ::memcpy(buffer.data(), data->data(), size);
        mBuffer.consume(size);
        co_return size;
    }

    /**
     * @brief Read a line from stream and append to string
     * 
     * @param str The string to append data to
     * @param delim The delimiter to look for (default: "\n")
     * @return IoTask<size_t> Number of bytes read (including delimiter), 
     *         or error if read fails
     */
    auto readline(std::string &str, std::string_view delim = "\n") -> IoTask<size_t> {
        auto policy = FillPolicy::None;
        while (true) {
            auto data = co_await fill(policy);
            if (data == Err(IoError::UnexpectedEOF) && mBuffer.size() != 0) {
                // EOF, the buffer data has some, return the whole buffer
                auto span = mBuffer.data();
                str.append(reinterpret_cast<const char *>(span.data()), span.size());
                mBuffer.consume(span.size());
                co_return span.size();
            }
            if (!data) {
                co_return Err(data.error());
            }
            auto view = std::string_view(reinterpret_cast<const char *>(data->data()), data->size());
            auto pos = view.find(delim);
            if (pos != std::string_view::npos) {
                str.append(view.substr(0, pos + delim.size()));
                mBuffer.consume(pos + delim.size());
                co_return pos + delim.size(); // Return the size of the line
            }
            policy = FillPolicy::More; // We need more data
        }
    }

    /**
     * @brief Get a line from stream
     * 
     * @param delim The delimiter to search for (default: "\n")
     * @return IoTask<std::string> The line without delimiter, or error if read fails
     */
    auto getline(std::string_view delim = "\n") -> IoTask<std::string> {
        std::string line;
        if (auto res = co_await readline(line, delim); !res) {
            co_return Err(res.error());
        }
        if (line.ends_with(delim)) {
            line.resize(line.size() - delim.size());
        }
        co_return line;
    }

    /**
     * @brief Fill the internal buffer
     * 
     * @param policy Fill policy: None (fill only when empty) or More (always try to fill)
     * @return IoTask<Buffer> The buffer data, or error. Returns UnexpectedEOF if 
     *         policy is More and stream reaches EOF
     */
    auto fill(FillPolicy policy = FillPolicy::None) -> IoTask<Buffer> {
        if (mBuffer.empty() || policy == FillPolicy::More) {
            auto bufsize = mBuffer.maxCapacity() - mBuffer.size(); // Get the remaining capacity of the buffer
            if (bufsize == 0 && policy == FillPolicy::More) {
                co_return Err(IoError::NoBufferSpaceAvailable); // Failed to fill the buffer with more data
            }
            auto buf = mBuffer.prepare(bufsize);
            auto res = co_await mStream.read(buf);
            if (!res) {
                co_return Err(res.error());
            }
            if (res == 0 && policy == FillPolicy::More) {
                co_return Err(IoError::UnexpectedEOF); // Failed to fill the buffer with more data
            }
            mBuffer.commit(*res);
        }
        co_return mBuffer.data();
    }

    // Expose Writable if the stream is writable
    auto write(Buffer buffer) -> IoTask<size_t> requires Writable<T> {
        return mStream.write(buffer);
    }

    auto flush() -> IoTask<void> requires Writable<T> {
        return mStream.flush();
    }

    auto shutdown() -> IoTask<void> requires Writable<T> {
        return mStream.shutdown();
    }

    // Get the wrapped stream
    auto nextLayer() -> T & {
        return mStream;
    }

    // Get the internal buffer's data
    auto buffer() -> MutableBuffer {
        return mBuffer.data();   
    }

    // Consume the data of the buffer
    auto consume(size_t size) -> void {
        return mBuffer.consume(size);
    }

    // Detach the stream
    [[nodiscard]]
    auto detach() -> T {
        return std::move(mStream);
    }

    auto operator =(BufReader &&) -> BufReader & = default;
    auto operator <=>(const BufReader &) const = default;

    explicit operator bool() const noexcept {
        return bool(mStream);
    }
private:
    StreamBuffer mBuffer;
    T mStream;
};

/**
 * @brief Wrap a writable stream with a buffer
 * 
 * @tparam T 
 */
template <Writable T>
class BufWriter final : public StreamMethod<BufWriter<T> > {
public:
    /**
     * @brief Construct a new Buf Writer with capacity
     * 
     * @param stream The stream to wrap
     * @param capacity The capacity of the buffer
     */
    BufWriter(T stream, size_t capacity = DEFAULT_BUFFER_CAPACITY) : mBuffer(capacity), mStream(std::move(stream)) {}
    BufWriter(BufWriter &&) = default;
    BufWriter() = default;

    // Writable
    auto write(Buffer buffer) -> IoTask<size_t> {
        // Bigger than capacity / 2, write directly
        if (buffer.size() >= mBuffer.maxCapacity() / 2) {
            if (auto res = co_await flush(); !res) {
                co_return Err(res.error());
            }
            co_return co_await mStream.write(buffer);
        }
        // No room in buffer
        if (mBuffer.maxCapacity() - mBuffer.size() < buffer.size()) {
            if (auto res = co_await flush(); !res) {
                co_return Err(res.error());
            }
        }
        // Copy into buffer
        auto data = mBuffer.prepare(buffer.size());
        ::memcpy(data.data(), buffer.data(), buffer.size());
        mBuffer.commit(buffer.size());
        co_return buffer.size();
    }

    auto flush() -> IoTask<void> {
        while (!mBuffer.empty()) {
            auto n = co_await mStream.write(mBuffer.data());
            if (!n) {
                co_return Err(n.error());
            }
            if (*n == 0) {
                co_return Err(IoError::WriteZero);
            }
            mBuffer.consume(*n);
        }
        co_return co_await mStream.flush();
    }

    auto shutdown() -> IoTask<void> {
        if (auto res = co_await flush(); !res) {
            co_return Err(res.error());
        }
        co_return co_await mStream.shutdown();
    }

    // Expose Readable if the stream is readable
    auto read(MutableBuffer buffer) -> IoTask<size_t> requires Readable<T> {
        return mStream.read(buffer);   
    }

    // Get the wrapped stream
    auto nextLayer() -> T & {
        return mStream;
    }

    // Prepare write buffer
    [[nodiscard]] 
    auto prepare(size_t n) -> MutableBuffer {
        return mBuffer.prepare(n);
    }

    auto commit(size_t n) -> void {
        return mBuffer.commit(n);
    }

    // Detach the stream
    [[nodiscard]] 
    auto detach() -> T {
        return std::move(mStream);
    }

    auto operator =(BufWriter &&) -> BufWriter & = default;
    auto operator <=>(const BufWriter &) const = default;

    explicit operator bool() const noexcept {
        return bool(mStream);
    }
private:
    StreamBuffer mBuffer;
    T mStream;
};


/**
 * @brief Wrap a readable & writable stream with a buffer
 * 
 * @tparam T 
 */
template <Stream T>
class BufStream final : public StreamMethod<BufStream<T> > {
public:
    /**
     * @brief Construct a new Buf Stream with capacity
     * 
     * @param stream The stream to wrap
     * @param readerCapacity The capacity of the reader buffer
     * @param writerCapacity The capacity of the writer buffer
     */
    BufStream(T stream, size_t readerCapacity, size_t writerCapacity) : 
        mStream {
            BufReader {
                BufWriter { std::move(stream), writerCapacity } , readerCapacity
            }
        }
    {

    }

    BufStream(T stream) : BufStream(std::move(stream), DEFAULT_BUFFER_CAPACITY, DEFAULT_BUFFER_CAPACITY) {}
    BufStream(BufStream &&) = default;
    BufStream() = default;

    // Readable
    auto read(MutableBuffer buffer) -> IoTask<size_t> {
        return mStream.read(buffer);
    }

    /// @copydoc BufReader::readline
    auto readline(std::string &str, std::string_view delim = "\n") -> IoTask<size_t> {
        return mStream.readline(str, delim);
    }

    /// @copydoc BufReader::getline
    auto getline(std::string_view delim = "\n") -> IoTask<std::string> {
        return mStream.getline(delim);
    }

    // Writable
    auto write(Buffer buffer) -> IoTask<size_t> {
        return mStream.nextLayer().write(buffer);
    }

    auto shutdown() -> IoTask<void> {
        return mStream.nextLayer().shutdown();
    }

    auto flush() -> IoTask<void> {
        return mStream.nextLayer().flush();
    }

    // Get
    auto nextLayer() -> T & {
        return mStream.nextLayer().nextLayer();
    }

    // Reader
    /// @copydoc BufReader::fill
    [[nodiscard]]
    auto fill(FillPolicy p = FillPolicy::None) -> IoTask<Buffer> {
        return mStream.fill(p);
    }

    /// @copydoc BufReader::buffer
    [[nodiscard]]
    auto buffer() -> MutableBuffer {
        return mStream.buffer();
    }

    /// @copydoc BufReader::consume
    auto consume(size_t size) -> void {
        return mStream.consume(size);
    }

    // Writer
    /// @copydoc BufWriter::prepare
    [[nodiscard]]
    auto prepare(size_t n) -> MutableBuffer {
        return mStream.nextLayer().prepare(n);
    }

    /// @copydoc BufWriter::commit
    auto commit(size_t n) -> void {
        return mStream.nextLayer().commit(n);
    }

    [[nodiscard]] 
    auto detach() -> T {
        return mStream.detach().detach();
    }

    auto operator =(BufStream &&) -> BufStream & = default;
    auto operator <=>(const BufStream &) const = default;

    explicit operator bool() const noexcept {
        return bool(mStream);
    }
private:
    BufReader<BufWriter<T> > mStream;
};


// For compatible with old code
template <Stream T>
using BufferedStream = BufStream<T>;

ILIAS_NS_END