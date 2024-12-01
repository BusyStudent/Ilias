/**
 * @file udp.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Udp socket wrapper
 * @version 0.1
 * @date 2024-09-02
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/net/detail/sockbase.hpp>
#include <ilias/net/endpoint.hpp>
#include <ilias/io/context.hpp>

ILIAS_NS_BEGIN

/**
 * @brief The Udp Client, wraps a UDP socket.
 * 
 */
class UdpClient {
public:
    UdpClient() = default;
    UdpClient(IoContext &ctxt, int family) : mBase(ctxt, Socket(family, SOCK_DGRAM, IPPROTO_UDP)) { }
    UdpClient(IoContext &ctxt, Socket &&sock) : mBase(ctxt, std::move(sock)) { }

    auto close() { return mBase.close(); }

    /**
     * @brief Set if the socket should reuse the address.
     * 
     * @param on 
     * @return Result<void> 
     */
    auto setReuseAddr(bool on) -> Result<void> {
        return mBase.socket().setReuseAddr(on);
    }

    /**
     * @brief Bind the socket to the specified endpoint.
     * 
     * @param endpoint 
     * @return Result<void> 
     */
    auto bind(const IPEndpoint &endpoint) -> Result<void> {
        return mBase.bind(endpoint);
    }

    /**
     * @brief Receive datagram from the socket
     * 
     * @param buffer The buffer to receive the data into.
     * @param endpoint The ptr of the endpoint to receive the datagram's endpoint.
     * @return Task<size_t> 
     */
    auto recvfrom(std::span<std::byte> buffer, IPEndpoint *endpoint) -> Task<size_t> {
        return mBase.recvfrom(buffer, 0, endpoint);
    }

    /**
     * @brief Receive datagram from the socket
     * 
     * @param buffer The buffer to receive the data into.
     * @param endpoint The endpoint reference to receive the datagram's endpoint.
     * @return Task<size_t> 
     */
    auto recvfrom(std::span<std::byte> buffer, IPEndpoint &endpoint) -> Task<size_t> {
        return mBase.recvfrom(buffer, 0, &endpoint);
    }

    /**
     * @brief Receive datagram from the socket
     * 
     * @param buffer The buffer to receive the data into.
     * @return Task<std::pair<size_t, IPEndpoint> > (The datagram size and the endpoint)
     */
    auto recvfrom(std::span<std::byte> buffer) -> Task<std::pair<size_t, IPEndpoint> > {
        IPEndpoint endpoint;
        auto n = co_await recvfrom(buffer, endpoint);
        if (!n) {
            co_return Unexpected(n.error());
        }
        co_return std::make_pair(n.value(), endpoint);
    }

    /**
     * @brief Send the datagram to the specified endpoint.
     * 
     * @param buffer 
     * @param endpoint 
     * @return Task<size_t> 
     */
    auto sendto(std::span<const std::byte> buffer, const IPEndpoint &endpoint) -> Task<size_t> {
        return mBase.sendto(buffer, 0, &endpoint);
    }

    /**
     * @brief Set the socket option.
     * 
     * @tparam T 
     * @param opt 
     * @return Result<void> 
     */
    template <SetSockOption T>
    auto setOption(const T &opt) -> Result<void> {
        return socket().setOption(opt);
    }

    /**
     * @brief Get the socket option.
     * 
     * @tparam T 
     * @return Result<T> 
     */
    template <GetSockOption T>
    auto getOption() -> Result<T> {
        return socket().getOption<T>();
    }

    /**
     * @brief Get the local endpoint associated with the socket.
     * 
     * @return Result<IPEndpoint> 
     */
    auto localEndpoint() const -> Result<IPEndpoint> { 
        return mBase.localEndpoint<IPEndpoint>(); 
    }

    /**
     * @brief Poll the socket for events.
     * 
     * @param events 
     * @return Task<uint32_t> 
     */
    auto poll(uint32_t events) -> Task<uint32_t> {
        return mBase.poll(events);
    }

    /**
     * @brief Get the underlying socket.
     * 
     * @return SocketView 
     */
    auto socket() const -> SocketView {
        return mBase.socket();
    }

    auto operator <=>(const UdpClient &) const = default;

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