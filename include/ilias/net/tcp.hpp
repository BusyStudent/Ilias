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

ILIAS_NS_BEGIN

// TODO: Finish implementation.
/**
 * @brief The tcp client class.
 * 
 */
class TcpClient {
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

    auto accept() -> Task<std::pair<TcpClient, IPEndpoint> >;
private:
    detail::SocketBase mBase;
};

ILIAS_NS_END