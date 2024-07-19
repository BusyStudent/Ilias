#pragma once

#include "traits.hpp"
#include <cstring>
#include <span>

#undef min
#undef max

ILIAS_NS_BEGIN

/**
 * @brief A Adaptor for StreamClient with a internal buffer, so it has getline and something else
 * 
 * @tparam T 
 */
template <StreamClient T = IStreamClient>
class BufferedStream final : public AddStreamMethod<BufferedStream<T> > {
public:
    using string = std::string;
    using string_view = std::string_view;

    BufferedStream();
    BufferedStream(T &&mFd);
    BufferedStream(BufferedStream &&);
    ~BufferedStream();

    /**
     * @brief Get a new line from buffer by delim
     * 
     * @param delim 
     * @return Task<string> 
     */
    auto getline(string_view delim = "\n") -> Task<string>;

    /**
     * @brief Recv data from
     * 
     * @param buffer 
     * @param n 
     * @param timeout
     * @return Task<size_t> bytes on ok, error on fail
     */
    auto recv(void *buffer, size_t n) -> Task<size_t>;

    /**
     * @brief Send data to
     * 
     * @param buffer 
     * @param n 
     * @param timeout
     * @return Task<size_t>
     */
    auto send(const void *buffer, size_t n) -> Task<size_t>;

    /**
     * @brief Connect to
     * 
     * @param addr 
     * @param timeout
     * @return Task<void>
     */
    auto connect(const IPEndpoint &addr) -> Task<>;

    /**
     * @brief Put back this data to buffer
     * 
     * @param buffer 
     * @param n 
     */
    auto unget(const void *buffer, size_t n) -> void;

    /**
     * @brief Unget this data to buffer
     * 
     * @param string 
     */
    auto unget(string_view string) -> void;

    /**
     * @brief Close the byte stream
     * 
     */
    auto close() -> void;

    /**
     * @brief Shutdown current stream
     * 
     * @return Task<> 
     */
    auto shutdown() -> Task<>;

    /**
     * @brief Assign a BufferedStream from a moved
     * 
     * @return BufferedStream &&
     */
    auto operator =(BufferedStream &&) -> BufferedStream &;
    auto operator =(T          &&) -> BufferedStream &;
private:
    /**
     * @brief Request a write window in recv buffer
     * 
     * @param n 
     * @return void* 
     */
    auto _allocWriteWindow(size_t n) -> void *;
    auto _allocUngetWindow(size_t n) -> void *;
    auto _writeWindow() -> std::pair<void *, size_t>;
    auto _readWindow() -> std::pair<void *, size_t>;

    T mFd;
    uint8_t *mBuffer = nullptr;
    size_t mBufferCapacity = 0;
    size_t mBufferTail = 0; //< Current position of valid data end
    size_t mPosition = 0; //< Current position
    // 
    // <mBuffer> UngetWindow  <mPosition> ReadWindow <mBufferTail>  WriteWindow <mBufferCapicity>
};

// --- BufferedStream Impl
template <StreamClient T>
inline BufferedStream<T>::BufferedStream() {

}
template <StreamClient T>
inline BufferedStream<T>::BufferedStream(T &&mFd) : mFd(std::move(mFd)) {

}
template <StreamClient T>
inline BufferedStream<T>::BufferedStream(BufferedStream &&other) : mFd(std::move(other.mFd)) {
    mBuffer = other.mBuffer;
    mBufferCapacity = other.mBufferCapacity;
    mBufferTail = other.mBufferTail;
    mPosition = other.mPosition;
    other.mBuffer = nullptr;
    other.mBufferCapacity = 0;
    other.mBufferTail = 0;
    other.mPosition = 0;
}
template <StreamClient T>
inline BufferedStream<T>::~BufferedStream() {
    close();
}

template <StreamClient T>
inline auto BufferedStream<T>::operator =(BufferedStream &&other) -> BufferedStream & {
    if (&other == this) {
        return *this;
    }
    close();
    mBuffer = other.mBuffer;
    mBufferCapacity = other.mBufferCapacity;
    mBufferTail = other.mBufferTail;
    mPosition = other.mPosition;
    other.mBuffer = nullptr;
    other.mBufferCapacity = 0;
    other.mBufferTail = 0;
    other.mPosition = 0;
    mFd = std::move(other.mFd);
    return *this;
}
template <StreamClient T>
inline auto BufferedStream<T>::operator =(T &&mFd) -> BufferedStream & {
    (*this) = BufferedStream(std::move(mFd));
    return *this;
};
template <StreamClient T>
inline auto BufferedStream<T>::close() -> void {
    if (mBuffer) {
        ILIAS_FREE(mBuffer);
    }
    mFd = T();
    mBuffer = nullptr;
    mBufferCapacity = 0;
    mBufferTail = 0;
    mPosition = 0;
}

template <StreamClient T>
inline auto BufferedStream<T>::shutdown() -> Task<> {
    return mFd.shutdown();
} 

template <StreamClient T>
inline auto BufferedStream<T>::recv(void *buffer, size_t n) -> Task<size_t> {
    while (true) {
        auto [ptr, len] = _readWindow();
        if (len > 0) {
            // Read data from the buffer
            len = std::min(len, n);
            ::memcpy(buffer, ptr, len);
            mPosition += len;
            co_return len;
        }
        // Try fill the buffer
        auto wptr = _allocWriteWindow(n);
        auto ret = co_await mFd.recv(wptr, n);
        if (!ret) {
            co_return Unexpected(ret.error());
        }
        if (*ret == 0) {
            co_return 0;
        }
        mBufferTail += ret.value();
    }
}
template <StreamClient T>
inline auto BufferedStream<T>::send(const void *buffer, size_t n) -> Task<size_t> {
    return mFd.send(buffer, n);
}
template <StreamClient T>
inline auto BufferedStream<T>::connect(const IPEndpoint &endpoint) -> Task<void> {
    return mFd.connect(endpoint);
}
template <StreamClient T>
inline auto BufferedStream<T>::getline(string_view delim) -> Task<string> {
    while (true) {
        // Scanning current buffer
        auto [ptr, len] = _readWindow();
        if (len >= delim.size()) {
            string_view view(static_cast<char*>(ptr), len);
            size_t pos = view.find(delim);
            if (pos != string_view::npos) {
                // Found the delimiter
                auto content = view.substr(0, pos);
                mPosition += (pos + delim.size());
                co_return string(content);
            }
        }
        // Try fill the buffer
        auto wptr = _allocWriteWindow(1024);
        auto ret = co_await mFd.recv(wptr, 1024);
        if (!ret) {
            co_return Unexpected(ret.error());
        }
        if (*ret == 0) {
            co_return string();
        }
        mBufferTail += ret.value();
    }
}
template <StreamClient T>
inline auto BufferedStream<T>::unget(const void *buffer, size_t n) -> void {
    // Check position > 0 so we can add data to the front of the buffer
    auto ptr = _allocUngetWindow(n);
    ::memcpy(ptr, buffer, n);
    mPosition -= n;
}
template <StreamClient T>
inline auto BufferedStream<T>::unget(string_view buffer) -> void {
    unget(buffer.data(), buffer.size());
}

template <StreamClient T>
inline auto BufferedStream<T>::_allocWriteWindow(size_t n) -> void * {
    // Reset the read position
    if (mPosition == mBufferTail) {
        mBufferTail = 0;
        mPosition = 0;
    }
    // Check the data len is less than 
    if ((mBufferTail - mPosition) < mBufferCapacity / 2) {
        // Move the valid data to the head of buffer
        ::memmove(mBuffer, mBuffer + mPosition, mBufferTail - mPosition);
        mBufferTail -= mPosition;
        mPosition = 0;
    }

    size_t still = mBufferCapacity - mBufferTail;
    if (n <= still) {
        return mBuffer + mBufferTail;
    }
    size_t newCapacity = (mBufferCapacity + n) * 2;
    if (newCapacity < n) {
        newCapacity = n;
    }
    auto newBuffer = (uint8_t*) ILIAS_REALLOC(mBuffer, newCapacity);
    if (newBuffer) {
        mBuffer = newBuffer;
        mBufferCapacity = newCapacity;
        return mBuffer + mBufferTail;
    }
    return nullptr;
}
template <StreamClient T>
inline auto BufferedStream<T>::_allocUngetWindow(size_t n) -> void * {
    if (n > mPosition) {
        // Bigger than current unget window, expand the buffer to n
        auto newCapicity = mBufferCapacity + n;
        auto newBuffer = (uint8_t*) ILIAS_REALLOC(mBuffer, newCapicity);
        // Move the valid data after n bytes
        ::memmove(newBuffer + mPosition + n, newBuffer + mPosition, mBufferTail - mPosition);
        mBuffer = newBuffer;
        mBufferCapacity = newCapicity;
        mBufferTail += n;
        mPosition += n;
    }
    return mBuffer + mPosition - n;
}
template <StreamClient T>
inline auto BufferedStream<T>::_writeWindow() -> std::pair<void *, size_t> {
    size_t still = mBufferCapacity - mBufferTail;
    if (still == 0) {
        return std::pair(nullptr, 0);
    }
    return std::pair(mBuffer + mBufferTail, still);
}
template <StreamClient T>
inline auto BufferedStream<T>::_readWindow() -> std::pair<void *, size_t> {
    size_t still = mBufferTail - mPosition;
    if (still == 0) {
        return std::pair(nullptr, 0);
    }
    return std::pair(mBuffer + mPosition, still);
}

template <StreamClient T>
using ByteStream [[deprecated("Use BufferedStream instead")]] = BufferedStream<T>;

ILIAS_NS_END