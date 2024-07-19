#pragma once

#include "traits.hpp"
#include "socket.hpp"

ILIAS_NS_BEGIN

/**
 * @brief Tcp Socket Client
 * 
 */
class TcpClient final : public AsyncSocket, public AddStreamMethod<TcpClient> {
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



ILIAS_NS_END