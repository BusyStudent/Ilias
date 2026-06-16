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
#include <ilias/net/msghdr.hpp> // MsgHdr
#include <ilias/io/context.hpp>

ILIAS_NS_BEGIN

// Forward declaration
class UdpSocket;

/**
 * @brief The builder used to build udp socket.
 * 
 * @code
 *  UdpBuilder {AF_INET}
 *   .option(sockopt::ReuseAddress(true)),
 *   .bind("127.0.0.1:0")
 * @endcode
 * 
 */
class UdpBuilder {
public:
    UdpBuilder(int family) : mFd(Socket::make(family, SOCK_DGRAM, IPPROTO_UDP)) {}
    UdpBuilder(UdpBuilder &&) = default;

    /**
     * @brief Set an new socket option on this builder
     * 
     * @tparam T 
     * @param opt 
     * @return UdpBuilder && 
     */
    template <SetSockOption T>
    auto option(const T &opt) -> UdpBuilder && {
        if (mFd) {
            if (auto res = mFd->setOption(opt); !res) {
                mFd = Err(res.error());
            }
        }
        return std::move(*this);
    }

    /**
     * @brief Bind the socket to the specified endpoint.
     * @note It will consume the builder.
     * @param endpoint
     * 
     * @return IoTask<UdpSocket> 
     */
    auto bind(IPEndpoint endpoint) -> IoTask<UdpSocket>;
private:
    IoResult<Socket> mFd;
};

/**
 * @brief The Udp Socket wrapper.
 * 
 */
class UdpSocket {
public:
    UdpSocket() = default;
    UdpSocket(IoHandle<Socket> h) : mHandle(std::move(h)) {}

    auto close() { return mHandle.close(); }
    auto cancel() const { return mHandle.cancel(); }

    /**
     * @brief Receive datagram from the socket
     * 
     * @param buffer The buffer to receive the data into.
     * @return IoTask<std::pair<size_t, IPEndpoint> > (The datagram size and the endpoint)
     */
    auto recvfrom(MutableBuffer buffer) const -> IoTask<std::pair<size_t, IPEndpoint> > {
        IPEndpoint endpoint;
        ILIAS_CO_TRY(auto n, co_await mHandle.recvfrom(buffer, 0, &endpoint));
        co_return std::pair {n, endpoint};
    }

    /**
     * @brief Send the datagram to the specified endpoint.
     * 
     * @param buffer A single buffer to send.
     * @param endpoint The endpoint to send the datagram to.
     * @return IoTask<size_t> 
     */
    auto sendto(Buffer buffer, const IPEndpoint &endpoint) const -> IoTask<size_t> {
        return mHandle.sendto(buffer, 0, &endpoint);
    }

    // Vectorized
    /**
     * @brief Receive datagram from the socket, it only recives one datagram.
     * 
     * @tparam T 
     * @param buffers The buffers sequence to receive the data into.
     * @return IoTask<std::pair<size_t, IPEndpoint> > 
     */
    template <MutableBufferSequence T>
    auto recvfrom(T &buffers) const -> IoTask<std::pair<size_t, IPEndpoint> > {
        auto sequence = makeMutableIoSequence(buffers); // Convert To ABI-Compatible Sequence
        MutableMsgHdr msg;
        IPEndpoint endpoint;
        msg.setBuffers(sequence);
        msg.setEndpoint(endpoint);
        ILIAS_CO_TRY(auto n, co_await mHandle.recvmsg(msg, 0));
        co_return std::pair {n, endpoint};
    }

    /**
     * @brief Send the datagram to the specified endpoint.
     * 
     * @tparam T 
     * @param buffers The buffers sequence to send.
     * @param endpoint The endpoint to send the datagram to.
     * @return IoTask<size_t> 
     */
    template <BufferSequence T>
    auto sendto(const T &buffers, const IPEndpoint &endpoint) const -> IoTask<size_t> {
        auto sequence = makeIoSequence(buffers); // Convert To ABI-Compatible Sequence
        MsgHdr msg;
        msg.setEndpoint(endpoint);
        msg.setBuffers(sequence);
        co_return co_await mHandle.sendmsg(msg, 0);
    }

    /**
     * @brief Set the socket option.
     * 
     * @tparam T 
     * @param opt 
     * @return IoResult<void> 
     */
    template <SetSockOption T>
    auto setOption(const T &opt) const -> IoResult<void> {
        return mHandle.fd().setOption(opt);
    }

    /**
     * @brief Get the socket option.
     * 
     * @tparam T 
     * @return IoResult<T> 
     */
    template <GetSockOption T>
    auto getOption() const -> IoResult<T> {
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
    auto poll(uint32_t events) const -> IoTask<uint32_t> {
        return mHandle.poll(events);
    }

    auto operator <=>(const UdpSocket &) const = default;

    /**
     * @brief Bind the socket to the specified endpoint.
     * 
     * @param endpoint 
     * @return IoTask<UdpSocket> 
     */
    static auto bind(IPEndpoint endpoint) -> IoTask<UdpSocket> {
        return UdpBuilder {endpoint.family()}.bind(endpoint);
    }

    /**
     * @brief Wrap a socket into a UdpSocket.
     * 
     * @param socket 
     * @return IoResult<UdpSocket> 
     */
    static auto from(Socket socket) -> IoResult<UdpSocket> {
        if (socket.type() != SOCK_DGRAM) {
            return Err(IoError::InvalidArgument);
        }
        ILIAS_TRY(auto handle, IoHandle<Socket>::make(std::move(socket), IoDescriptor::Socket));
        return UdpSocket {std::move(handle)};
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

// Impl
inline auto UdpBuilder::bind(IPEndpoint endpoint) -> IoTask<UdpSocket> {
    auto fn = [](UdpBuilder self, IPEndpoint endpoint) -> IoTask<UdpSocket> {
        ILIAS_CO_TRY(auto sockfd, std::move(self.mFd));
        ILIAS_CO_TRYV(sockfd.bind(endpoint));
        ILIAS_CO_TRY(auto handle, IoHandle<Socket>::make(std::move(sockfd), IoDescriptor::Socket));
        co_return UdpSocket {std::move(handle)};
    };
    return fn(std::move(*this), endpoint);
}

// For compatibility with the old API
using UdpClient [[deprecated("Use UdpSocket instead")]] = UdpSocket;

ILIAS_NS_END