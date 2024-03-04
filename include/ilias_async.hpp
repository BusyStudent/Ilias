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
    auto close() -> Expected<void, SockError>;
protected:
    IOContext *mContext = nullptr;
    Socket     mFd;
};

class TcpClient final : public AsyncSocket {
public:
    TcpClient();
    TcpClient(IOContext &ctxt, int family);

#if defined(__cpp_lib_coroutine)
    /**
     * @brief Recv 
     * 
     * @param buffer 
     * @param n 
     * @return Task<std::expected<size_t, SockError> > bytes on ok, error on fail
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
    mContext->asyncInitialize(mFd);
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

// --- TcpServer Impl
inline TcpServer::TcpServer() { }
inline TcpServer::TcpServer(IOContext &ctxt, int family) : 
    AsyncSocket(ctxt, Socket::create(family, SOCK_STREAM, 0)) 
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