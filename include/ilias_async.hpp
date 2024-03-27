#pragma once

#include "ilias.hpp"
#include "ilias_expected.hpp"
#include "ilias_backend.hpp"

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
    auto close() -> Expected<void, SockError>;
    auto context() const -> IOContext *;
    auto cancel() const -> bool;
    
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

    /**
     * @brief Recv data from
     * 
     * @param buffer 
     * @param n 
     * @param timeout 
     * @param handler 
     * @return void * 
     */
    auto recv(void *buffer, size_t n, int64_t timeout, RecvHandler &&handler) -> void *;
    /**
     * @brief Send data to
     * 
     * @param buffer 
     * @param n 
     * @param timeout 
     * @param handler 
     * @return void * 
     */
    auto send(const void *buffer, size_t n, int64_t timeout, SendHandler &&handler) -> void *;
    /**
     * @brief Connect to
     * 
     * @param endpoint 
     * @param timeout 
     * @param handler 
     * @return void * 
     */
    auto connect(const IPEndpoint &endpoint, int64_t timeout, ConnectHandler &&handler) -> void *;

#if defined(__cpp_lib_coroutine)
    /**
     * @brief Recv data from
     * 
     * @param buffer 
     * @param n 
     * @param timeout
     * @return AwaitResult<std::Expected<size_t, SockError> > bytes on ok, error on fail
     */
    auto recv(void *buffer, size_t n, int64_t timeout = -1) -> AwaitResult<RecvHandlerArgs>;
    /**
     * @brief Send data to
     * 
     * @param buffer 
     * @param n 
     * @param timeout
     * @return AwaitResult<Expected<size_t, SockError> > 
     */
    auto send(const void *buffer, size_t n, int64_t timeout = -1) -> AwaitResult<SendHandlerArgs>;
    /**
     * @brief Connect to
     * 
     * @param addr 
     * @param timeout
     * @return AwaitResult<Expected<void, SockError> > 
     */
    auto connect(const IPEndpoint &addr, int64_t timeout = -1) -> AwaitResult<ConnectHandlerArgs>;
#endif
};

class TcpListener final : public AsyncSocket {
public:
    using Client = TcpClient;

    TcpListener();
    TcpListener(IOContext &ctxt, int family);
    TcpListener(IOContext &ctxt, Socket &&socket);
    /**
     * @brief Bind the socket with endpoint
     * 
     * @param endpoint 
     * @param backlog 
     * @return Expected<void, SockError> 
     */
    auto bind(const IPEndpoint &endpoint, int backlog = 0) -> BindHandlerArgs;

#if defined(__cpp_lib_coroutine)
    /**
     * @brief Waiting accept socket
     * 
     * @return AwaitResult<AcceptHandlerArgsT<TcpClient> > 
     */
    auto accept(int64_t timeout = -1) -> AwaitResult<AcceptHandlerArgsT<TcpClient> >;
#endif
};

class UdpClient final : public AsyncSocket {
public:
    UdpClient();
    UdpClient(IOContext &ctxt, int family);
    UdpClient(IOContext &ctxt, Socket &&socket);

    /**
     * @brief Bind the socket with endpoint
     * 
     * @param endpoint 
     * @return Expected<void, SockError> 
     */
    auto bind(const IPEndpoint &endpoint) -> BindHandlerArgs;

#if defined(__cpp_lib_coroutine)
    auto sendto(const void *buffer, size_t n, const IPEndpoint &endpoint, int64_t timeout = -1) -> AwaitResult<SendtoHandlerArgs>;
    auto recvfrom(void *buffer, size_t n, int64_t timeout = -1) -> AwaitResult<RecvfromHandlerArgs>;
#endif
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
inline auto AsyncSocket::context() const -> IOContext * {
    return mContext;
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
inline auto AsyncSocket::cancel() const -> bool {
    return mContext->asyncCancel(mFd, nullptr);
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
inline auto TcpClient::recv(void *buffer, size_t n, int64_t timeout, RecvHandler &&fn) -> void * {
    return mContext->asyncRecv(mFd, buffer, n, timeout, std::move(fn));
}
inline auto TcpClient::send(const void *buffer, size_t n, int64_t timeout, SendHandler &&fn) -> void * {
    return mContext->asyncSend(mFd, buffer, n, timeout, std::move(fn));
}
inline auto TcpClient::connect(const IPEndpoint &endpoint, int64_t timeout, ConnectHandler &&fn) -> void * {
    return mContext->asyncConnect(mFd, endpoint, timeout, std::move(fn));
}

#if defined(__cpp_lib_coroutine)
inline auto TcpClient::recv(void *buffer, size_t n, int64_t timeout) -> AwaitResult<RecvHandlerArgs> {
    using Awaitable = AwaitResult<RecvHandlerArgs>;
    return Awaitable(
        [=, this](Awaitable::ResumeFunc &&func) {
            this->recv(buffer, n, timeout, [=, f = std::move(func)](auto result) mutable {
                f(std::move(result));  
            });
        }
    );
}
inline auto TcpClient::send(const void *buffer, size_t n, int64_t timeout) -> AwaitResult<SendHandlerArgs> {
    using Awaitable = AwaitResult<SendHandlerArgs>;
    return Awaitable(
        [=, this](Awaitable::ResumeFunc &&func) {
            this->send(buffer, n, timeout, [=, f = std::move(func)](auto result) mutable {
                f(std::move(result));  
            });
        }
    );
}
inline auto TcpClient::connect(const IPEndpoint &endpoint, int64_t timeout) -> AwaitResult<ConnectHandlerArgs> {
    using Awaitable = AwaitResult<ConnectHandlerArgs>;
    return Awaitable(
        [=, this](Awaitable::ResumeFunc &&func) {
            this->connect(endpoint, timeout, [=, f = std::move(func)](auto result) mutable {
                f(std::move(result));  
            });
        }
    );
}

#endif


// --- TcpListener Impl
inline TcpListener::TcpListener() { }
inline TcpListener::TcpListener(IOContext &ctxt, int family) : 
    AsyncSocket(ctxt, Socket::create(family, SOCK_STREAM, IPPROTO_TCP)) 
{

}
inline TcpListener::TcpListener(IOContext &ctxt, Socket &&socket):
    AsyncSocket(ctxt, std::move(socket))
{

}

inline auto TcpListener::bind(const IPEndpoint &endpoint, int backlog) -> BindHandlerArgs {
    if (!mFd.bind(endpoint)) {
        return Unexpected(SockError::fromErrno());
    }
    if (!mFd.listen(backlog)) {
        return Unexpected(SockError::fromErrno());
    }
    return BindHandlerArgs();
}
#if defined(__cpp_lib_coroutine)
inline auto TcpListener::accept(int64_t timeout) -> AwaitResult<AcceptHandlerArgsT<TcpClient> > {
    using Awaitable = AwaitResult<AcceptHandlerArgsT<TcpClient> >;
    return Awaitable([=, this](Awaitable::ResumeFunc &&func) {
        mContext->asyncAccept(mFd, timeout, [this, cb = std::move(func)](auto &&result) mutable {
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

// --- UdpSocket Impl
inline UdpClient::UdpClient() { }
inline UdpClient::UdpClient(IOContext &ctxt, int family) :
    AsyncSocket(ctxt, Socket::create(family, SOCK_DGRAM, IPPROTO_UDP)) 
{

}
inline UdpClient::UdpClient(IOContext &ctxt, Socket &&socket): 
    AsyncSocket(ctxt, std::move(socket)) 
{

}
inline auto UdpClient::bind(const IPEndpoint &endpoint) -> BindHandlerArgs {
    if (!mFd.bind(endpoint)) {
        return Unexpected(SockError::fromErrno());
    }
    return BindHandlerArgs();
}


#if defined(__cpp_lib_coroutine)
inline auto UdpClient::sendto(const void *buffer, size_t n, const IPEndpoint &endpoint, int64_t timeout) -> AwaitResult<SendtoHandlerArgs> {
    using Awaitable = AwaitResult<SendtoHandlerArgs>;
    return Awaitable([=, this](Awaitable::ResumeFunc &&func) {
        mContext->asyncSendto(mFd, buffer, n, endpoint, timeout, [this, cb = std::move(func)](auto &&result) mutable {
            cb(std::move(result));
        });
    });
}
inline auto UdpClient::recvfrom(void *buffer, size_t n, int64_t timeout) -> AwaitResult<RecvfromHandlerArgs> {
    using Awaitable = AwaitResult<RecvfromHandlerArgs>;
    return Awaitable([=, this](Awaitable::ResumeFunc &&func) {
        mContext->asyncRecvfrom(mFd, buffer, n, timeout, [this, cb = std::move(func)](auto &&result) mutable {
            cb(std::move(result));
        });
    });
}
#endif

ILIAS_NS_END