#pragma once

#include "ilias.hpp"
#include "ilias_expected.hpp"
#include "ilias_backend.hpp"

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
    auto close() -> Expected<void, Error>;
    auto context() const -> IoContext *;
    auto cancel() const -> bool;
    
    explicit operator SocketView() const noexcept;
protected:
    IoContext *mContext = nullptr;
    Socket     mFd;
};

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
     * @return IAwaitable<std::Expected<size_t, Error> > bytes on ok, error on fail
     */
    auto recv(void *buffer, size_t n) -> Task<size_t>;
    /**
     * @brief Send data to
     * 
     * @param buffer 
     * @param n 
     * @param timeout
     * @return IAwaitable<Expected<size_t, Error> > 
     */
    auto send(const void *buffer, size_t n) -> Task<size_t>;
    /**
     * @brief Connect to
     * 
     * @param addr 
     * @param timeout
     * @return IAwaitable<Expected<void, Error> > 
     */
    auto connect(const IPEndpoint &addr) -> Task<void>;
};

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
    auto bind(const IPEndpoint &endpoint, int backlog = 0) -> Result<void>;

#if defined(__cpp_lib_coroutine)
    /**
     * @brief Waiting accept socket
     * 
     * @return IAwaitable<AcceptHandlerArgsT<TcpClient> > 
     */
    auto accept() -> Task<std::pair<TcpClient, IPEndpoint> >;
#endif
};

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
    auto bind(const IPEndpoint &endpoint) -> Result<void>;

#if defined(__cpp_lib_coroutine)
    auto sendto(const void *buffer, size_t n, const IPEndpoint &endpoint) -> Task<size_t>;
    auto recvfrom(void *buffer, size_t n) -> Task<std::pair<size_t, IPEndpoint> >;
#endif
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
inline auto AsyncSocket::close() -> Expected<void, Error> {
    if (mContext && mFd.isValid()) {
        mContext->removeSocket(mFd);
        if (!mFd.close()) {
            return Unexpected(Error::fromErrno());
        }
    }
    return Expected<void, Error>();
}
inline auto AsyncSocket::localEndpoint() const -> Result<IPEndpoint> {
    return mFd.localEndpoint();
}
inline auto AsyncSocket::isValid() const -> bool {
    return mFd.isValid();
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

inline auto TcpListener::bind(const IPEndpoint &endpoint, int backlog) -> Result<void> {
    if (auto ret = mFd.bind(endpoint); !ret) {
        return Unexpected(ret.error());
    }
    if (auto ret = mFd.listen(backlog); !ret) {
        return Unexpected(ret.error());
    }
    return Result<void>();
}
#if defined(__cpp_lib_coroutine)
inline auto TcpListener::accept() -> Task<std::pair<TcpClient, IPEndpoint> > {
    return [](IoContext *ctxt, SocketView fd) -> Task<std::pair<TcpClient, IPEndpoint> > {
        auto ret = co_await ctxt->accept(fd);
        if (!ret) {
            co_return Unexpected(ret.error());
        }
        co_return std::pair{TcpClient(*ctxt, std::move(ret->first)), ret->second};
    }(mContext, mFd);
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


#if defined(__cpp_lib_coroutine)
inline auto UdpClient::sendto(const void *buffer, size_t n, const IPEndpoint &endpoint) -> Task<size_t> {
    return mContext->sendto(mFd, buffer, n, endpoint);
}
inline auto UdpClient::recvfrom(void *buffer, size_t n) -> Task<std::pair<size_t, IPEndpoint> > {
    return mContext->recvfrom(mFd, buffer, n);
}
#endif

ILIAS_NS_END