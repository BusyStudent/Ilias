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

    /**
     * @brief Get the os socket fd
     * 
     * @return socket_t 
     */
    auto get() const -> socket_t;

    /**
     * @brief Get the contained socket's view
     * 
     * @return SocketView 
     */
    auto view() const -> SocketView;

    /**
     * @brief Check current socket is valid
     * 
     * @return true 
     * @return false 
     */
    auto isValid() const -> bool;

    /**
     * @brief Get the endpoint of the socket
     * 
     * @return Result<IPEndpoint> 
     */
    auto localEndpoint() const -> Result<IPEndpoint>;

    /**
     * @brief Set the Reuse Addr object, allow multiple sockets bind to same address
     * 
     * @param reuse 
     * @return Result<> 
     */
    auto setReuseAddr(bool reuse) -> Result<>;

    /**
     * @brief Set the Socket Option object
     * 
     * @param level 
     * @param optname 
     * @param optval 
     * @param optlen 
     * @return Result<> 
     */
    auto setOption(int level, int optname, const void *optval, socklen_t optlen) -> Result<>;
    
    /**
     * @brief Get the Socket Option object
     * 
     * @param level 
     * @param optname 
     * @param optval 
     * @param optlen 
     * @return Result<> 
     */
    auto getOption(int level, int optname, void *optval, socklen_t *optlen) -> Result<>;

    /**
     * @brief Close current socket
     * 
     * @return Result<> 
     */
    auto close() -> Result<>;

    /**
     * @brief Shutdown current socket
     * 
     * @param how default in Shutdown::Both (all read and write)
     * @return Task<> 
     */
    auto shutdown(int how = Shutdown::Both) -> Task<>;

    /**
     * @brief Poll current socket use PollEvent::In or PollEvent::Out
     * 
     * @param event 
     * @return Task<uint32_t> actually got events
     */
    auto poll(uint32_t event) -> Task<uint32_t>;

    /**
     * @brief Get the context of the socket
     * 
     * @return IoContext* 
     */
    auto context() const -> IoContext *;
    
    /**
     * @brief Assign
     * 
     * @return AsyncSocket& 
     */
    auto operator =(AsyncSocket &&) -> AsyncSocket &;

    /**
     * @brief Cast to socket view
     * 
     * @return SocketView 
     */
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
     * @brief Set the Tcp Client Recv Buffer Size object
     * 
     * @param size 
     * @return Result<> 
     */
    auto setRecvBufferSize(size_t size) -> Result<>;

    /**
     * @brief Set the Send Buffer Size object
     * 
     * @param size 
     * @return Result<> 
     */
    auto setSendBufferSize(size_t size) -> Result<>;

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

    /**
     * @brief Waiting accept socket
     * 
     * @return IAwaitable<AcceptHandlerArgsT<TcpClient> > 
     */
    auto accept() -> Task<std::pair<TcpClient, IPEndpoint> >;
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

    /**
     * @brief Send num of the bytes to the target
     * 
     * @param buffer 
     * @param n 
     * @param endpoint 
     * @return Task<size_t> 
     */
    auto sendto(const void *buffer, size_t n, const IPEndpoint &endpoint) -> Task<size_t>;

    /**
     * @brief Recv num of the bytes from
     * 
     * @param buffer 
     * @param n 
     * @return Task<std::pair<size_t, IPEndpoint> > 
     */
    auto recvfrom(void *buffer, size_t n) -> Task<std::pair<size_t, IPEndpoint> >;
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
    auto recvAll(std::span<std::byte> b) -> Task<size_t>;

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
    auto sendAll(std::span<const std::byte> b) -> Task<size_t>;

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

inline auto AsyncSocket::get() const -> socket_t {
    return mFd.get();
}
inline auto AsyncSocket::view() const -> SocketView {
    return mFd;
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
inline auto AsyncSocket::shutdown(int how) -> Task<> {
    co_return mFd.shutdown(how);
}
inline auto AsyncSocket::poll(uint32_t event) -> Task<uint32_t> {
    return mContext->poll(mFd, event);
}
inline auto AsyncSocket::setReuseAddr(bool reuse) -> Result<> {
    return mFd.setReuseAddr(reuse);
}
inline auto AsyncSocket::setOption(int level, int optname, const void *optval, socklen_t optlen) -> Result<> {
    return mFd.setOption(level, optname, optval, optlen);
}
inline auto AsyncSocket::getOption(int level, int optname, void *optval, socklen_t *optlen) -> Result<> {
    return mFd.getOption(level, optname, optval, optlen);
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
inline auto TcpListener::accept() -> Task<std::pair<TcpClient, IPEndpoint> > {
    auto ret = co_await mContext->accept(mFd);
    if (!ret) {
        co_return Unexpected(ret.error());
    }
    co_return std::pair{TcpClient(*mContext, std::move(ret->first)), ret->second};
}

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

inline auto UdpClient::sendto(const void *buffer, size_t n, const IPEndpoint &endpoint) -> Task<size_t> {
    return mContext->sendto(mFd, buffer, n, endpoint);
}
inline auto UdpClient::recvfrom(void *buffer, size_t n) -> Task<std::pair<size_t, IPEndpoint> > {
    return mContext->recvfrom(mFd, buffer, n);
}

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
inline auto ByteStream<T, Char>::shutdown() -> Task<> {
    return mFd.shutdown();
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
    return RecvAll(*this, buffer, n);
}
template <typename T, typename Char>
inline auto ByteStream<T, Char>::recvAll(std::span<std::byte> buffer) -> Task<size_t> {
    return RecvAll(*this, buffer.data(), buffer.size());
}
template <typename T, typename Char>
inline auto ByteStream<T, Char>::send(const void *buffer, size_t n) -> Task<size_t> {
    return mFd.send(buffer, n);
}
template <typename T, typename Char>
inline auto ByteStream<T, Char>::sendAll(const void *buffer, size_t n) -> Task<size_t> {
    return SendAll(*this, buffer, n);
}
template <typename T, typename Char>
inline auto ByteStream<T, Char>::sendAll(std::span<const std::byte> buffer) -> Task<size_t> {
    return sendAll(*this, buffer.data(), buffer.size());
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