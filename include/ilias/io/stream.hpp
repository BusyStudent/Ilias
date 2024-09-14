#pragma once

#include <ilias/io/dyn_traits.hpp>
#include <ilias/io/method.hpp>
#include <ilias/io/traits.hpp>
#include <string>
#include <vector>
#include <span>

#undef min
#undef max

ILIAS_NS_BEGIN

/**
 * @brief A Wrapper for buffered a stream
 * 
 * @tparam T 
 */
template <Stream T = IStreamClient>
class BufferedStream : public StreamMethod<BufferedStream<T> > {
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
     * @return Task<std::string> 
     */
    auto getline(std::string_view delim) -> Task<std::string> {
        while (true) {
            // Scanning current buffer
            auto buf = readWindow();
            if (buf.size() >= delim.size()) {
                std::string_view view(reinterpret_cast<char*>(buf.data()), buf.size());
                size_t pos = view.find(delim);
                if (pos != std::string_view::npos) {
                    // Found the delimiter
                    auto content = view.substr(0, pos);
                    mPosition += (pos + delim.size());
                    co_return std::string(content);
                }
            }
            // Try fill the buffer
            auto wbuf = allocWriteWindow(1024);
            auto ret = co_await mStream.read(wbuf);
            if (!ret || *ret == 0) {
                co_return Unexpected(ret.error_or(Error::ZeroReturn));
            }
            mBufferTail += ret.value();
        }
    }

    auto write(std::span<const std::byte> buffer) -> Task<size_t> {
        return mStream.write(buffer);
    }

    auto read(std::span<std::byte> buffer) -> Task<size_t> {
        const auto n = buffer.size();
        while (true) {
            auto buf = readWindow();
            if (!buf.empty()) {
                // Read data from the buffer
                auto len = std::min(buf.size(), n);
                ::memcpy(buffer.data(), buf.data(), len);
                mPosition += len;
                co_return len;
            }
            // Try fill the buffer
            auto wbuf = allocWriteWindow(n);
            auto ret = co_await mStream.read(wbuf);
            if (!ret) {
                co_return Unexpected(ret.error());
            }
            if (*ret == 0) {
                co_return 0;
            }
            mBufferTail += ret.value();
        }
    }

    /**
     * @brief Connect to a remote endpoint
     * 
     * @tparam U 
     */
    template <typename U = T> requires (Connectable<U>)
    auto connect(const IPEndpoint &endpoint) -> Task<void> {
        return static_cast<U&>(mStream).connect(endpoint);
    }

    /**
     * @brief Shutdown the stream
     * 
     * @tparam U 
     */
    template <typename U = T> requires (Shuttable<U>)
    auto shutdown() -> Task<void> {
        return static_cast<U&>(mStream).shutdown();
    }
private:
    /**
     * @brief Get the internal read buffer's read window (how many data are available)
     * 
     * @return std::span<std::byte> 
     */
    auto readWindow() -> std::span<std::byte> {
        size_t still = mBufferTail - mPosition;
        return std::span(mBuffer).subspan(mPosition, still);
    }

    auto allocWriteWindow(size_t n) -> std::span<std::byte> {
        // Reset the read position
        if (mPosition == mBufferTail) {
            mBufferTail = 0;
            mPosition = 0;
        }
        // Check the data len is less than 
        if ((mBufferTail - mPosition) < mBuffer.capacity() / 2) {
            // Move the valid data to the head of buffer
            ::memmove(mBuffer.data(), mBuffer.data() + mPosition, mBufferTail - mPosition);
            mBufferTail -= mPosition;
            mPosition = 0;
        }

        size_t still = mBuffer.capacity() - mBufferTail;
        if (n <= still) {
            return std::span(mBuffer).subspan(mBufferTail, n);
        }
        size_t newCapacity = (mBuffer.capacity() + n) * 2;
        if (newCapacity < n) {
            newCapacity = n;
        }
        mBuffer.resize(newCapacity);
        return std::span(mBuffer).subspan(mBufferTail, n);
    }

    T mStream;
    size_t mBufferTail = 0; //< Current position of valid data end
    size_t mPosition = 0; //< Current readed position
    std::vector<std::byte> mBuffer; //< temporary buffer
};


ILIAS_NS_END