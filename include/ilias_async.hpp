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

#if defined(__cpp_lib_span)
#include <span>
#endif

ILIAS_NS_BEGIN

#if defined(__cpp_lib_coroutine)
template <typename T>
using AwaitResult = CallbackAwaitable<T>;
#endif

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
    auto close() -> Expected<void, SockError>;
    
    explicit operator SocketView() const noexcept;
protected:
    IOContext *mContext = nullptr;
    Socket     mFd;
};

class TcpClient final : public AsyncSocket {
public:
    TcpClient();
    /**
     * @brief Construct a new Tcp Client object by family
     * 
     * @param ctxt The IOContext ref
     * @param family The family
     */
    TcpClient(IOContext &ctxt, int family);
    /**
     * @brief Construct a new Tcp Client object by extsts socket
     * 
     * @param ctxt 
     * @param socket 
     */
    TcpClient(IOContext &ctxt, Socket &&socket);

    /**
     * @brief Get the remote endpoint
     * 
     * @return IPEndpoint 
     */
    auto remoteEndpoint() const -> IPEndpoint;

#if defined(__cpp_lib_coroutine)
    /**
     * @brief Recv data from
     * 
     * @param buffer 
     * @param n 
     * @param timeout
     * @return AwaitResult<std::Expected<size_t, SockError> > bytes on ok, error on fail
     */
    auto recv(void *buffer, size_t n, int timeout = -1) -> AwaitResult<Expected<size_t, SockError> >;
    /**
     * @brief Send data to
     * 
     * @param buffer 
     * @param n 
     * @param timeout
     * @return AwaitResult<Expected<size_t, SockError> > 
     */
    auto send(const void *buffer, size_t n, int timeout = -1) -> AwaitResult<Expected<size_t, SockError> >;
    /**
     * @brief Connect to
     * 
     * @param addr 
     * @param timeout
     * @return AwaitResult<Expected<void, SockError> > 
     */
    auto connect(const IPEndpoint &addr, int timeout = -1) -> AwaitResult<Expected<void, SockError> >;
#endif
};

class TcpServer final : public AsyncSocket {
public:
    TcpServer();
    TcpServer(IOContext &ctxt, int family);
    TcpServer(IOContext &ctxt, Socket &&socket);

    /**
     * @brief Bind the socket with endpoint
     * 
     * @param endpoint 
     * @param backlog 
     * @return Expected<void, SockError> 
     */
    auto bind(const IPEndpoint &endpoint, int backlog = 0) -> Expected<void, SockError>;

#if defined(__cpp_lib_coroutine)
    /**
     * @brief Waiting accept socket
     * 
     * @return AwaitResult<Expected<std::pair<TcpClient, IPEndpoint> , SockError> > 
     */
    auto accept() -> AwaitResult<Expected<std::pair<TcpClient, IPEndpoint> , SockError> >;
#endif
};

class UdpSocket final : public AsyncSocket {
public:
    UdpSocket();
    UdpSocket(IOContext &ctxt, int family);
    UdpSocket(IOContext &ctxt, Socket &&socket);

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
inline TcpClient::TcpClient(IOContext &ctxt, Socket &&socket):
    AsyncSocket(ctxt, std::move(socket))
{

}

inline auto TcpClient::remoteEndpoint() const -> IPEndpoint {
    return mFd.remoteEndpoint();
}

#if defined(__cpp_lib_coroutine)
inline auto TcpClient::recv(void *buffer, size_t n, int timeout) -> AwaitResult<Expected<size_t, SockError> > {
    using Awaitable = AwaitResult<Expected<size_t, SockError> >;
    return Awaitable(
        [=, this](Awaitable::ResumeFunc &&func) {
            mContext->asyncRecv(mFd, buffer, n, [=, f = std::move(func)](auto result) mutable {
                f(std::move(result));  
            });
        }
    );
}
inline auto TcpClient::send(const void *buffer, size_t n, int timeout) -> AwaitResult<Expected<size_t, SockError> > {
    using Awaitable = AwaitResult<Expected<size_t, SockError> >;
    return Awaitable(
        [=, this](Awaitable::ResumeFunc &&func) {
            mContext->asyncSend(mFd, buffer, n, [=, f = std::move(func)](auto result) mutable {
                f(std::move(result));  
            });
        }
    );
}
inline auto TcpClient::connect(const IPEndpoint &endpoint, int timeout) -> AwaitResult<Expected<void, SockError> > {
    using Awaitable = AwaitResult<Expected<void, SockError> >;
    return Awaitable(
        [=, this](Awaitable::ResumeFunc &&func) {
            mContext->asyncConnect(mFd, endpoint, [=, f = std::move(func)](auto result) mutable {
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
inline TcpServer::TcpServer(IOContext &ctxt, Socket &&socket):
    AsyncSocket(ctxt, std::move(socket))
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
#if defined(__cpp_lib_coroutine)
inline auto TcpServer::accept() -> AwaitResult<Expected<std::pair<TcpClient, IPEndpoint> , SockError> > {
    using Awaitable = AwaitResult<Expected<std::pair<TcpClient, IPEndpoint> , SockError> >;
    return Awaitable([this](Awaitable::ResumeFunc &&func) {
        mContext->asyncAccept(mFd, [this, cb = std::move(func)](auto &&result) mutable {
            if (!result) {
                // Has Error
                cb(Unexpected<SockError>(result.error()));
                return;
            }
            else {
                auto &[sock, addr] = *result;
                cb(std::make_pair(TcpClient(*mContext, std::move(sock)), addr));
                return;
            }
        });
    });
}
#endif


ILIAS_NS_END