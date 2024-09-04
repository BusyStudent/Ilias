/**
 * @file tcp.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Provides TCP socket functionality.
 * @version 0.1
 * @date 2024-08-13
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/net/detail/sockbase.hpp>
#include <ilias/net/endpoint.hpp>
#include <ilias/io/context.hpp>
#include <ilias/io/method.hpp>

ILIAS_NS_BEGIN

/**
 * @brief The tcp client class.
 * 
 */
class TcpClient final : public StreamMethod<TcpClient> {
public:
    TcpClient() = default;
    TcpClient(IoContext &ctxt, int family) : mBase(ctxt, Socket(family, SOCK_STREAM, IPPROTO_TCP)) { }
    TcpClient(IoContext &ctxt, Socket &&sock) : mBase(ctxt, std::move(sock)) { }

    auto close() { return mBase.close(); }

    // Stream Concept
    /**
     * @brief Write data to the socket.
     * 
     * @param buffer 
     * @return Task<size_t> 
     */
    auto write(std::span<const std::byte> buffer) -> Task<size_t> {
        return mBase.send(buffer);
    }

    /**
     * @brief Read data from the socket.
     * 
     * @param data 
     * @return Task<size_t> 
     */
    auto read(std::span<std::byte> data) -> Task<size_t> {
        return mBase.recv(data);
    }

    // Extension Methods
    /**
     * @brief Send data to the socket. like write but with flags.
     * 
     * @param buffer 
     * @param flags 
     * @return Task<size_t> 
     */
    auto send(std::span<const std::byte> buffer, int flags = 0) -> Task<size_t> {
        return mBase.send(buffer, flags);
    }

    /**
     * @brief Receive data from the socket. like read but with flags.
     * 
     * @param data 
     * @param flags 
     * @return Task<size_t> 
     */
    auto recv(std::span<std::byte> data, int flags = 0) -> Task<size_t> {
        return mBase.recv(data, flags);
    }

    /**
     * @brief Peek at the data in the socket.
     * 
     * @param data 
     * @return Result<size_t> 
     */
    auto peek(std::span<std::byte> data) -> Result<size_t> {
        return mBase.socket().recv(data, MSG_PEEK);
    }

    /**
     * @brief Shutdown the socket.
     * 
     * @param how 
     * @return Task<void> 
     */
    auto shutdown(int how = Shutdown::Both) -> Task<void> {
        return mBase.shutdown(how);
    }

    // Connectable Connept
    /**
     * @brief Connect to a remote endpoint.
     * 
     * @param endpoint 
     * @return Task<void> 
     */
    auto connect(const IPEndpoint &endpoint) -> Task<void> {
        return mBase.connect(endpoint);
    }

    /**
     * @brief Get the local endpoint associated with the socket.
     * 
     * @return Result<IPEndpoint> 
     */
    auto localEndpoint() const -> Result<IPEndpoint> { 
        return mBase.localEndpoint(); 
    }

    /**
     * @brief Get the remote endpoint associated with the socket.
     * 
     * @return Result<IPEndpoint> 
     */
    auto remoteEndpoint() const -> Result<IPEndpoint> {
        return mBase.remoteEndpoint();
    }

    /**
     * @brief Get the underlying socket.
     * 
     * @return SocketView 
     */
    auto socket() const -> SocketView {
        return mBase.socket();
    }

    auto operator <=>(const TcpClient &) const = default;

    /**
     * @brief Check if the socket is valid.
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const { return bool(mBase); }
private:
    detail::SocketBase mBase;
};

/**
 * @brief The tcp listener class.
 * 
 */
class TcpListener {
public:
    TcpListener() = default;
    TcpListener(IoContext &ctxt, int family) : mBase(ctxt, Socket(family, SOCK_STREAM, IPPROTO_TCP)) { }
    TcpListener(IoContext &ctxt, Socket &&sock) : mBase(ctxt, std::move(sock)) { }

    auto close() { return mBase.close(); }

    /**
     * @brief Bind the listener to a local endpoint.
     * 
     * @param endpoint The local endpoint to bind to.
     * @param backlog The maximum length to which the queue of pending connections may grow.
     * @return Result<void> 
     */
    auto bind(const IPEndpoint &endpoint, int backlog = 0) -> Result<void> {
        if (auto ret = mBase.bind(endpoint); !ret) {
            return Unexpected(ret.error());
        }
        return mBase.listen(backlog);
    }

    /**
     * @brief Accept a connection from a remote endpoint.
     * 
     * @return Task<std::pair<TcpClient, IPEndpoint> > 
     */
    auto accept() -> Task<std::pair<TcpClient, IPEndpoint> > {
        IPEndpoint endpoint;
        auto sock = co_await mBase.accept(&endpoint);
        if (!sock) {
            co_return Unexpected(sock.error());
        }
        TcpClient client(*(mBase.context()), Socket(sock.value()));
        co_return std::make_pair(std::move(client), endpoint);
    }

    /**
     * @brief Get the local endpoint associated with the socket.
     * 
     * @return Result<IPEndpoint> 
     */
    auto localEndpoint() const -> Result<IPEndpoint> { 
        return mBase.localEndpoint(); 
    }

    /**
     * @brief Check if the socket is valid.
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const { return bool(mBase); }
private:
    detail::SocketBase mBase;
};

ILIAS_NS_END