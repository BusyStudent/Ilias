#pragma once

#include "ilias.hpp"
#include "ilias_expected.hpp"

// --- Import async backend
#if 0
#include "ilias_iocp.hpp"
#else
#include "ilias_poll.hpp"
#endif

// --- Import coroutine if
#if defined(__cpp_lib_coroutine)
#include "ilias_co.hpp"
#endif

ILIAS_NS_BEGIN

/**
 * @brief A helper class for impl async socket
 * 
 */
class AsyncSocket {
public:
    AsyncSocket(IOContext &ctxt, Socket &&sockfd);
    AsyncSocket(const AsyncSocket &) = delete;
    AsyncSocket(AsyncSocket &&) = default;
    AsyncSocket() = default;
    ~AsyncSocket();

    socket_t get() const noexcept {
        return mFd.get();
    }
    auto isValid() const -> bool;
    auto localEndpoint() const -> IPEndpoint;
    explicit operator SocketView() const noexcept;
    auto close() -> Expected<void, SockError>;
protected:
    IOContext *mContext = nullptr;
    Socket     mFd;
};

class TcpClient final : public AsyncSocket {
public:
    TcpClient();
    TcpClient(IOContext &ctxt, int family);

    /**
     * @brief Get the remote endpoint
     * 
     * @return IPEndpoint 
     */
    auto remoteEndpoint() const -> IPEndpoint;

#if defined(__cpp_lib_coroutine)
    /**
     * @brief Recv 
     * 
     * @param buffer 
     * @param n 
     * @return Task<std::Expected<size_t, SockError> > bytes on ok, error on fail
     */
    auto recv(void *buffer, size_t n) -> Task<Expected<size_t, SockError> >;
    auto send(const void *buffer, size_t n) -> Task<Expected<size_t, SockError> >;
    auto connect(const IPEndpoint &addr) -> Task<Expected<void, SockError> >;
#endif
    /**
     * @brief Create a socket from a raw socket fd, this return object will take the fd ownship !!!
     * 
     * @param sockfd 
     * @return TcpClient 
     */
    static TcpClient from(socket_t sockfd);
};

class TcpServer final : public AsyncSocket {
public:
    TcpServer();
    TcpServer(IOContext &ctxt, int family);

    auto remoteEndpoint() const -> IPEndpoint;

    /**
     * @brief Bind the socket with endpoint
     * 
     * @param endpoint 
     * @param backlog 
     * @return Expected<void, SockError> 
     */
    auto bind(const IPEndpoint &endpoint, int backlog = 0) -> Expected<void, SockError>;

#if defined(__cpp_lib_coroutine)
    auto accept() -> Task<Expected<std::pair<TcpClient, IPEndpoint> , SockError> >;
#endif

    static TcpServer from(socket_t sockfd);
};

class UdpSocket final : public AsyncSocket {
public:
    UdpSocket();
    UdpSocket(IOContext &ctxt, int family);

    static UdpSocket from(socket_t sockfd);
};


// --- AsyncSocket Impl
inline AsyncSocket::AsyncSocket(IOContext &ctxt, Socket &&sockfd) : 
    mContext(&ctxt), mFd(std::move(sockfd)) 
{
    if (!mContext->asyncInitialize(mFd)) {
        mFd.close();
    }
}
inline AsyncSocket::~AsyncSocket() {
    if (mContext && mFd.isValid()) {
        mContext->asyncCleanup(mFd);
    }
}
inline auto AsyncSocket::close() -> Expected<void, SockError> {
    if (mContext && mFd.isValid()) {
        mContext->asyncCleanup(mFd);
        if (!mFd.close()) {
            return Unexpected(SockError::fromErrno());
        }
    }
    return Expected<void, SockError>();
}
inline auto AsyncSocket::localEndpoint() const -> IPEndpoint {
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
inline TcpClient::TcpClient(IOContext &ctxt, int family) :
    AsyncSocket(ctxt, Socket::create(family, SOCK_STREAM, IPPROTO_TCP))
{ 

}

inline auto TcpClient::remoteEndpoint() const -> IPEndpoint {
    return mFd.remoteEndpoint();
}

#if defined(__cpp_lib_coroutine)
inline auto TcpClient::recv(void *buffer, size_t n) -> Task<Expected<size_t, SockError> > {
    co_return co_await CallbackAwaitable<Expected<size_t, SockError> >(
        [=, this](CallbackAwaitable<Expected<size_t, SockError> >::ResumeFunc &&func) {
            mContext->asyncRecv(mFd, buffer, n, [=, f = std::move(func)](Expected<size_t, SockError> result) mutable {
                f(std::move(result));  
            });
        }
    );
}
inline auto TcpClient::send(const void *buffer, size_t n) -> Task<Expected<size_t, SockError> > {
    co_return co_await CallbackAwaitable<Expected<size_t, SockError> >(
        [=, this](CallbackAwaitable<Expected<size_t, SockError> >::ResumeFunc &&func) {
            mContext->asyncSend(mFd, buffer, n, [=, f = std::move(func)](Expected<size_t, SockError> result) mutable {
                f(std::move(result));  
            });
        }
    );
}
#endif


// --- TcpServer Impl
inline TcpServer::TcpServer() { }
inline TcpServer::TcpServer(IOContext &ctxt, int family) : 
    AsyncSocket(ctxt, Socket::create(family, SOCK_STREAM, IPPROTO_TCP)) 
{

}
inline auto TcpServer::bind(const IPEndpoint &endpoint, int backlog) -> Expected<void, SockError> {
    if (!mFd.bind(endpoint)) {
        return Unexpected(SockError::fromErrno());
    }
    if (!mFd.listen(backlog)) {
        return Unexpected(SockError::fromErrno());
    }
    return Expected<void, SockError>();
}


ILIAS_NS_END