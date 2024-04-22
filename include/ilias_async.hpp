#pragma once

#include "ilias.hpp"
#include "ilias_expected.hpp"
#include "ilias_backend.hpp"

#undef min
#undef max

ILIAS_NS_BEGIN

/**
 * @brief A helper class for impl async socket
 * 
 */
class AsyncSocket {
public:
    AsyncSocket(IoContext &ctxt, Socket &&sockfd);
    AsyncSocket(const AsyncSocket &) = delete;
    AsyncSocket(AsyncSocket &&) = default;
    AsyncSocket() = default;
    ~AsyncSocket();

    socket_t get() const noexcept {
        return mFd.get();
    }
    auto isValid() const -> bool;
    auto localEndpoint() const -> Result<IPEndpoint>;
    auto close() -> Result<>;
    auto context() const -> IoContext *;
    auto operator =(AsyncSocket &&) -> AsyncSocket &;

    explicit operator SocketView() const noexcept;
protected:
    IoContext *mContext = nullptr;
    Socket     mFd;
};

/**
 * @brief Tcp Socket Client
 * 
 */
class TcpClient final : public AsyncSocket {
public:
    TcpClient();
    /**
     * @brief Construct a new Tcp Client object by family
     * 
     * @param ctxt The IoContext ref
     * @param family The family
     */
    TcpClient(IoContext &ctxt, int family);
    /**
     * @brief Construct a new Tcp Client object by extsts socket
     * 
     * @param ctxt 
     * @param socket 
     */
    TcpClient(IoContext &ctxt, Socket &&socket);

    /**
     * @brief Get the remote endpoint
     * 
     * @return IPEndpoint 
     */
    auto remoteEndpoint() const -> Result<IPEndpoint>;
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
    auto connect(const IPEndpoint &addr) -> Task<void>;
};

/**
 * @brief Tcp Listener for accepting new connections
 * 
 */
class TcpListener final : public AsyncSocket {
public:
    using Client = TcpClient;

    TcpListener();
    TcpListener(IoContext &ctxt, int family);
    TcpListener(IoContext &ctxt, Socket &&socket);
    /**
     * @brief Bind the socket with endpoint
     * 
     * @param endpoint 
     * @param backlog 
     * @return Expected<void, Error> 
     */
    auto bind(const IPEndpoint &endpoint, int backlog = 0) -> Result<>;

#if defined(__cpp_lib_coroutine)
    /**
     * @brief Waiting accept socket
     * 
     * @return IAwaitable<AcceptHandlerArgsT<TcpClient> > 
     */
    auto accept() -> Task<std::pair<TcpClient, IPEndpoint> >;
#endif
};

/**
 * @brief Udp Socket Client
 * 
 */
class UdpClient final : public AsyncSocket {
public:
    UdpClient();
    UdpClient(IoContext &ctxt, int family);
    UdpClient(IoContext &ctxt, Socket &&socket);

    /**
     * @brief Bind the socket with endpoint
     * 
     * @param endpoint 
     * @return Expected<void, Error> 
     */
    auto bind(const IPEndpoint &endpoint) -> Result<>;
    /**
     * @brief Set the Broadcast flags
     * 
     * @param broadcast 
     * @return Result<> 
     */
    auto setBroadcast(bool broadcast) -> Result<>;

#if defined(__cpp_lib_coroutine)
    auto sendto(const void *buffer, size_t n, const IPEndpoint &endpoint) -> Task<size_t>;
    auto recvfrom(void *buffer, size_t n) -> Task<std::pair<size_t, IPEndpoint> >;
#endif
};

/**
 * @brief Helper class for impl getline and another things
 * 
 * @tparam T 
 */
template <typename T = IStreamClient, typename Char = char>
class ByteStream {
public:
    static_assert(sizeof(Char) == sizeof(uint8_t), "Char must be 8-bit");
    using string = std::basic_string<Char>;
    using string_view = std::basic_string_view<Char>;

    ByteStream();
    ByteStream(T &&mFd);
    ByteStream(ByteStream &&);
    ~ByteStream();

#if defined(__cpp_lib_coroutine)
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
     * @brief Recv data from, it will try to recv data as more as possible
     * 
     * @param buffer 
     * @param n 
     * @return Task<size_t> 
     */
    auto recvAll(void *buffer, size_t n) -> Task<size_t>;
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
     * @brief Send all the data to, it will send data as more as possible
     * 
     * @param buffer 
     * @param n 
     * @return Task<size_t> 
     */
    auto sendAll(const void *buffer, size_t n) -> Task<size_t>;
    /**
     * @brief Connect to
     * 
     * @param addr 
     * @param timeout
     * @return Task<void>
     */
    auto connect(const IPEndpoint &addr) -> Task<void>;
#endif
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
     * @brief Assign a ByteStream from a moved
     * 
     * @return ByteStream &&
     */
    auto operator =(ByteStream &&) -> ByteStream &;
    auto operator =(T          &&) -> ByteStream &;
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


// --- AsyncSocket Impl
inline AsyncSocket::AsyncSocket(IoContext &ctxt, Socket &&sockfd) : 
    mContext(&ctxt), mFd(std::move(sockfd)) 
{
    if (!mContext->addSocket(mFd)) {
        mFd.close();
    }
}
inline AsyncSocket::~AsyncSocket() {
    if (mContext && mFd.isValid()) {
        mContext->removeSocket(mFd);
    }
}
inline auto AsyncSocket::context() const -> IoContext * {
    return mContext;
}
inline auto AsyncSocket::close() -> Result<> {
    if (mContext && mFd.isValid()) {
        mContext->removeSocket(mFd);
        if (!mFd.close()) {
            return Unexpected(Error::fromErrno());
        }
    }
    return Result<>();
}
inline auto AsyncSocket::localEndpoint() const -> Result<IPEndpoint> {
    return mFd.localEndpoint();
}
inline auto AsyncSocket::isValid() const -> bool {
    return mFd.isValid();
}
inline auto AsyncSocket::operator =(AsyncSocket &&other) -> AsyncSocket & {
    if (&other == this) {
        return *this;
    }
    close();
    mContext = other.mContext;
    mFd = std::move(other.mFd);
    return *this;
}
inline AsyncSocket::operator SocketView() const noexcept {
    return mFd;
}

// --- TcpClient Impl
inline TcpClient::TcpClient() { }
inline TcpClient::TcpClient(IoContext &ctxt, int family) :
    AsyncSocket(ctxt, Socket(family, SOCK_STREAM, IPPROTO_TCP))
{ 

}
inline TcpClient::TcpClient(IoContext &ctxt, Socket &&socket):
    AsyncSocket(ctxt, std::move(socket))
{

}

inline auto TcpClient::remoteEndpoint() const -> Result<IPEndpoint> {
    return mFd.remoteEndpoint();
}
inline auto TcpClient::recv(void *buffer, size_t n) -> Task<size_t> {
    return mContext->recv(mFd, buffer, n);
}
inline auto TcpClient::send(const void *buffer, size_t n) -> Task<size_t> {
    return mContext->send(mFd, buffer, n);
}
inline auto TcpClient::connect(const IPEndpoint &endpoint) -> Task<void> {
    return mContext->connect(mFd, endpoint);
}


// --- TcpListener Impl
inline TcpListener::TcpListener() { }
inline TcpListener::TcpListener(IoContext &ctxt, int family) : 
    AsyncSocket(ctxt, Socket(family, SOCK_STREAM, IPPROTO_TCP)) 
{

}
inline TcpListener::TcpListener(IoContext &ctxt, Socket &&socket):
    AsyncSocket(ctxt, std::move(socket))
{

}

inline auto TcpListener::bind(const IPEndpoint &endpoint, int backlog) -> Result<> {
    if (auto ret = mFd.bind(endpoint); !ret) {
        return Unexpected(ret.error());
    }
    if (auto ret = mFd.listen(backlog); !ret) {
        return Unexpected(ret.error());
    }
    return Result<>();
}
#if defined(__cpp_lib_coroutine)
inline auto TcpListener::accept() -> Task<std::pair<TcpClient, IPEndpoint> > {
    auto ret = co_await mContext->accept(mFd);
    if (!ret) {
        co_return Unexpected(ret.error());
    }
    co_return std::pair{TcpClient(*mContext, std::move(ret->first)), ret->second};
}
#endif

// --- UdpSocket Impl
inline UdpClient::UdpClient() { }
inline UdpClient::UdpClient(IoContext &ctxt, int family) :
    AsyncSocket(ctxt, Socket(family, SOCK_DGRAM, IPPROTO_UDP)) 
{

}
inline UdpClient::UdpClient(IoContext &ctxt, Socket &&socket): 
    AsyncSocket(ctxt, std::move(socket)) 
{

}
inline auto UdpClient::bind(const IPEndpoint &endpoint) -> Result<> {
    return mFd.bind(endpoint);
}
inline auto UdpClient::setBroadcast(bool v) -> Result<> {
    int intFlags = v ? 1 : 0;
    return mFd.setOption(SOL_SOCKET, SO_BROADCAST, &intFlags, sizeof(intFlags));
}

#if defined(__cpp_lib_coroutine)
inline auto UdpClient::sendto(const void *buffer, size_t n, const IPEndpoint &endpoint) -> Task<size_t> {
    return mContext->sendto(mFd, buffer, n, endpoint);
}
inline auto UdpClient::recvfrom(void *buffer, size_t n) -> Task<std::pair<size_t, IPEndpoint> > {
    return mContext->recvfrom(mFd, buffer, n);
}
#endif

// --- ByteStream Impl
template <typename T, typename Char>
inline ByteStream<T, Char>::ByteStream() {

}
template <typename T, typename Char>
inline ByteStream<T, Char>::ByteStream(T &&mFd) : mFd(std::move(mFd)) {

}
template <typename T, typename Char>
inline ByteStream<T, Char>::ByteStream(ByteStream &&other) : mFd(std::move(other.mFd)) {
    mBuffer = other.mBuffer;
    mBufferCapacity = other.mBufferCapacity;
    mBufferTail = other.mBufferTail;
    mPosition = other.mPosition;
    other.mBuffer = nullptr;
    other.mBufferCapacity = 0;
    other.mBufferTail = 0;
    other.mPosition = 0;
}
template <typename T, typename Char>
inline ByteStream<T, Char>::~ByteStream() {
    close();
}

template <typename T, typename Char>
inline auto ByteStream<T, Char>::operator =(ByteStream &&other) -> ByteStream & {
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
template <typename T, typename Char>
inline auto ByteStream<T, Char>::operator =(T &&mFd) -> ByteStream & {
    (*this) = ByteStream(std::move(mFd));
    return *this;
};
template <typename T, typename Char>
inline auto ByteStream<T, Char>::close() -> void {
    if (mBuffer) {
        ILIAS_FREE(mBuffer);
    }
    mFd = T();
    mBuffer = nullptr;
    mBufferCapacity = 0;
    mBufferTail = 0;
    mPosition = 0;
}

template <typename T, typename Char>
inline auto ByteStream<T, Char>::recv(void *buffer, size_t n) -> Task<size_t> {
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
template <typename T, typename Char>
inline auto ByteStream<T, Char>::recvAll(void *buffer, size_t n) -> Task<size_t> {
    auto cur = static_cast<uint8_t*>(buffer);
    size_t received = 0;
    while (n > 0) {
        auto ret = co_await recv(cur, n);
        if (!ret) {
            co_return Unexpected(ret.error());
        }
        if (*ret == 0) {
            break;
        }
        received += *ret;
        n -= *ret;
        cur += *ret;
    }
    co_return received;
}
template <typename T, typename Char>
inline auto ByteStream<T, Char>::send(const void *buffer, size_t n) -> Task<size_t> {
    return mFd.send(buffer, n);
}
template <typename T, typename Char>
inline auto ByteStream<T, Char>::sendAll(const void *buffer, size_t n) -> Task<size_t> {
    auto cur = static_cast<const uint8_t*>(buffer);
    size_t sended = 0;
    while (n > 0) {
        auto ret = co_await send(buffer, n);
        if (!ret) {
            co_return Unexpected(ret.error());
        }
        if (*ret == 0) {
            break;
        }
        sended += *ret;
        n -= *ret;
        cur += *ret;
    }
    co_return sended;
}
template <typename T, typename Char>
inline auto ByteStream<T, Char>::connect(const IPEndpoint &endpoint) -> Task<void> {
    return mFd.connect(endpoint);
}
template <typename T, typename Char>
inline auto ByteStream<T, Char>::getline(string_view delim) -> Task<string> {
    while (true) {
        // Scanning current buffer
        auto [ptr, len] = _readWindow();
        if (len >= delim.size()) {
            string_view view(static_cast<Char*>(ptr), len);
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
template <typename T, typename Char>
inline auto ByteStream<T, Char>::unget(const void *buffer, size_t n) -> void {
    // Check position > 0 so we can add data to the front of the buffer
    auto ptr = _allocUngetWindow(n);
    ::memcpy(ptr, buffer, n);
    mPosition -= n;
}
template <typename T, typename Char>
inline auto ByteStream<T, Char>::unget(string_view buffer) -> void {
    unget(buffer.data(), buffer.size());
}

template <typename T, typename Char>
inline auto ByteStream<T, Char>::_allocWriteWindow(size_t n) -> void * {
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
template <typename T, typename Char>
inline auto ByteStream<T, Char>::_allocUngetWindow(size_t n) -> void * {
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
template <typename T, typename Char>
inline auto ByteStream<T, Char>::_writeWindow() -> std::pair<void *, size_t> {
    size_t still = mBufferCapacity - mBufferTail;
    if (still == 0) {
        return std::pair(nullptr, 0);
    }
    return std::pair(mBuffer + mBufferTail, still);
}
template <typename T, typename Char>
inline auto ByteStream<T, Char>::_readWindow() -> std::pair<void *, size_t> {
    size_t still = mBufferTail - mPosition;
    if (still == 0) {
        return std::pair(nullptr, 0);
    }
    return std::pair(mBuffer + mPosition, still);
}

ILIAS_NS_END