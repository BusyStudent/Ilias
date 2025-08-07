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

#include <ilias/net/endpoint.hpp>
#include <ilias/net/sockfd.hpp>
#include <ilias/io/context.hpp>

ILIAS_NS_BEGIN

/**
 * @brief The Udp Client, wraps a UDP socket.
 * 
 */
class UdpClient {
public:
    UdpClient() = default;
    UdpClient(IoHandle<Socket> h) : mHandle(std::move(h)) {}

    auto close() { return mHandle.close(); }
    auto cancel() { return mHandle.cancel(); }

    /**
     * @brief Receive datagram from the socket
     * 
     * @param buffer The buffer to receive the data into.
     * @param endpoint The ptr of the endpoint to receive the datagram's endpoint.
     * @return IoTask<size_t> 
     */
    auto recvfrom(MutableBuffer buffer, IPEndpoint *endpoint) -> IoTask<size_t> {
        return mHandle.recvfrom(buffer, 0, endpoint);
    }

    /**
     * @brief Receive datagram from the socket
     * 
     * @param buffer The buffer to receive the data into.
     * @param endpoint The endpoint reference to receive the datagram's endpoint.
     * @return IoTask<size_t> 
     */
    auto recvfrom(MutableBuffer buffer, IPEndpoint &endpoint) -> IoTask<size_t> {
        return mHandle.recvfrom(buffer, 0, &endpoint);
    }

    /**
     * @brief Receive datagram from the socket
     * 
     * @param buffer The buffer to receive the data into.
     * @return IoTask<std::pair<size_t, IPEndpoint> > (The datagram size and the endpoint)
     */
    auto recvfrom(MutableBuffer buffer) -> IoTask<std::pair<size_t, IPEndpoint> > {
        IPEndpoint endpoint;
        auto n = co_await recvfrom(buffer, endpoint);
        if (!n) {
            co_return Err(n.error());
        }
        co_return std::make_pair(n.value(), endpoint);
    }

    /**
     * @brief Send the datagram to the specified endpoint.
     * 
     * @param buffer 
     * @param endpoint 
     * @return IoTask<size_t> 
     */
    auto sendto(Buffer buffer, const IPEndpoint &endpoint) -> IoTask<size_t> {
        return mHandle.sendto(buffer, 0, &endpoint);
    }

    /**
     * @brief Set the socket option.
     * 
     * @tparam T 
     * @param opt 
     * @return IoResult<void> 
     */
    template <SetSockOption T>
    auto setOption(const T &opt) -> IoResult<void> {
        return mHandle.fd().setOption(opt);
    }

    /**
     * @brief Get the socket option.
     * 
     * @tparam T 
     * @return IoResult<T> 
     */
    template <GetSockOption T>
    auto getOption() -> IoResult<T> {
        return mHandle.fd().getOption<T>();
    }

    /**
     * @brief Get the local endpoint associated with the socket.
     * 
     * @return IoResult<IPEndpoint> 
     */
    auto localEndpoint() const -> IoResult<IPEndpoint> { 
        return mHandle.fd().localEndpoint<IPEndpoint>(); 
    }

    /**
     * @brief Poll the socket for events.
     * 
     * @param events 
     * @return IoTask<uint32_t> 
     */
    auto poll(uint32_t events) -> IoTask<uint32_t> {
        return mHandle.poll(events);
    }

    auto operator <=>(const UdpClient &) const = default;

    /**
     * @brief Bind the socket to the specified endpoint.
     * 
     * @param endpoint 
     * @return IoTask<UdpClient> 
     */
    static auto bind(IPEndpoint endpoint) -> IoTask<UdpClient> {
        auto sockfd = Socket::make(endpoint.family(), SOCK_DGRAM, 0);
        if (!sockfd) {
            co_return Err(sockfd.error());
        }
        if (auto res = sockfd->bind(endpoint); !res) {
            co_return Err(res.error());
        }
        auto handle = IoHandle<Socket>::make(std::move(*sockfd), IoDescriptor::Socket);
        if (!handle) {
            co_return Err(handle.error());
        }
        co_return UdpClient(std::move(*handle));
    }

    /**
     * @brief Wrap a socket into a UdpClient.
     * 
     * @param socket 
     * @return IoResult<UdpClient> 
     */
    static auto from(Socket socket) -> IoResult<UdpClient> {
        if (socket.type() != SOCK_DGRAM) {
            return Err(IoError::InvalidArgument);
        }
        auto handle = IoHandle<Socket>::make(std::move(socket), IoDescriptor::Socket);
        if (!handle) {
            return Err(handle.error());
        }
        return UdpClient(std::move(*handle));
    }

    /**
     * @brief Check if the socket is valid.
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const { return bool(mHandle); }
private:
    IoHandle<Socket> mHandle;
};

ILIAS_NS_END