/**
 * @file socks5.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The socks5 protocol implementation.
 * @version 0.1
 * @date 2024-09-17
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/net/endpoint.hpp>
#include <ilias/task/task.hpp>
#include <ilias/io/traits.hpp>
#include <ilias/buffer.hpp>
#include <ilias/error.hpp>
#include <ilias/log.hpp>
#include <string>

ILIAS_NS_BEGIN

namespace detail {

struct Socks5Header {
    uint8_t ver;
    uint8_t nmethods;
    uint8_t methods[0];
};

static_assert(sizeof(Socks5Header) == 2);

} // namespace detail

/**
 * @brief The Connector for the socks5 protocol. do the handshake and authentication.
 * 
 * @tparam T 
 */
template <Stream T>
class Socks5Connector {
public:
    /**
     * @brief Construct a new Socks 5 Connector object
     * 
     * @param stream The stream to send / recv the socks 5 handshake and authentication. we doesnot take the ownership of the stream.
     * @param user 
     * @param password 
     */
    Socks5Connector(T &stream, std::string_view user = {}, std::string_view password = {}) : 
        mStream(stream), mUser(user), mPassword(password) 
    {

    }

    /**
     * @brief Do the handshake and authentication.
     * 
     * @return Task<void> 
     */
    auto handshake() -> Task<void> {
        uint8_t buf[256];
        auto header = reinterpret_cast<detail::Socks5Header *>(buf);
        header->ver = 0x05;

        // Check if we has the password and user.
        if (mUser.empty() && mPassword.empty()) {
            header->nmethods = 1;
            header->methods[0] = 0x00;
        }
        else {
            header->nmethods = 2;
            header->methods[0] = 0x02;
            header->methods[1] = 0x00;
        }
        ILIAS_TRACE("Socks5", "Begin handshake, user: {}, password: {}", mUser, mPassword);
        auto n = co_await mStream.write(makeBuffer(buf, sizeof(detail::Socks5Header) + header->nmethods));
        if (!n || *n != sizeof(detail::Socks5Header) + header->nmethods) {
            co_return Unexpected(n.error_or(Error::Socks5Unknown));
        }

        // Try recv the selected method
        // Version uint8_t
        // Method uint8_t
        n = co_await mStream.read(makeBuffer(buf, 2));
        if (!n || *n != 2 || buf[0] != 0x05 || buf[1] == 0xFF) {
            // NetworkError | Unknown method | No acceptable method
            co_return Unexpected(n.error_or(Error::Socks5Unknown));
        }

        if (buf[1] == 0x02) {
            // User and password authentication
            // TODO: Implement the authentication
            co_return Unexpected(Error::Socks5AuthenticationFailed);
        }
        
        if (buf[1] != 0x00) {
            // Unknown method
            co_return Unexpected(Error::Socks5Unknown);
        }

        ILIAS_TRACE("Socks5", "Handshake done");
        mHandshakeDone = true;
        co_return {};
    }

    /**
     * @brief Connect to the endpoint. (IPV4 or IPV6 address)
     * 
     * @param endpoint 
     * @return Task<void> 
     */
    auto connect(const IPEndpoint &endpoint) -> Task<void> {
        auto addr = endpoint.address();
        co_return co_await connectImpl(
            addr.family() == AF_INET ? 0x01 : 0x04,
            addr.span(),
            endpoint.port()
        );
    }

    /**
     * @brief Connect to the host and port. (Domain name)
     * 
     * @param host 
     * @param port 
     * @return Task<void> 
     */
    auto connect(std::string_view host, uint16_t port) -> Task<void> {
        // Buf first byte is the length of the host
        std::string buf;
        buf.reserve(host.size() + 1);
        buf.push_back(host.size());
        buf.append(host.begin(), host.end());
        co_return co_await connectImpl(0x03, makeBuffer(buf), port);
    }
private:
    auto connectImpl(uint8_t type, std::span<const std::byte> addr, uint16_t port) -> Task<void> {
        if (!mHandshakeDone) {
            if (auto ret = co_await handshake(); !ret) {
                co_return Unexpected(ret.error());
            }
        }

        ILIAS_TRACE("Socks5", "Connecting to type: {} addrlen({}):{}", type, addr.size(), port);
        port = ::htons(port);
        
        // Version uint8_t
        // Command uint8_t
        // Reserved uint8_t
        // AddressType uint8_t
        // Address uint8_t[n]
        // Port uint16_t
        auto bufSize = 4 + addr.size() + sizeof(uint16_t);
        auto buf = std::make_unique<uint8_t[]>(bufSize);
        buf[0] = 0x05;
        buf[1] = 0x01;
        buf[2] = 0x00;
        buf[3] = type;
        ::memcpy(buf.get() + 4, addr.data(), addr.size());
        ::memcpy(buf.get() + 4 + addr.size(), &port, sizeof(port));

        auto n = co_await mStream.write(makeBuffer(buf.get(), bufSize));
        if (!n || *n != bufSize) {
            co_return Unexpected(n.error_or(Error::Socks5Unknown));
        }

        // Version uint8_t
        // Reply uint8_t
        // Reserved uint8_t
        // AddressType uint8_t
        // Address uint8_t[n]
        // Port uint16_t
        auto recvBufSize = 4 + 16 + sizeof(uint16_t); //< Max size of IPV6 address
        if (bufSize < recvBufSize) {
            bufSize = recvBufSize;
            buf = std::make_unique<uint8_t[]>(bufSize);
        }
        n = co_await mStream.read(makeBuffer(buf.get(), bufSize));
        if (!n || *n < 4 + 2 + sizeof(uint16_t)) {
            // NetworkError | Unknown reply (less than minimum size)
            co_return Unexpected(n.error_or(Error::Socks5Unknown));
        }
        if (buf[0] != 0x05 || buf[2] != 0x00) {
            // Bad Reply
            co_return Unexpected(Error::Socks5Unknown);
        }
        switch (buf[2]) { // The Reply field contains the result of the request.
            case 0x00: break;
            default: co_return Unexpected(Error::Socks5Unknown); //< TODO: Add more error codes
        }
        switch (buf[3]) {
            case 0x01: { // IPV4
                if (*n < 4 + 4 + sizeof(uint16_t)) {
                    co_return Unexpected(Error::Socks5Unknown);
                }
                auto port = ::ntohs(*reinterpret_cast<const uint16_t *>(buf.get() + 4 + 4));
                auto addr = IPAddress4::fromRaw(buf.get() + 4, 4);
                mServerBound = IPEndpoint(addr, port);
                ILIAS_TRACE("Socks5", "Server bound to {}", mServerBound);
                break;
            }
            case 0x04: { // IPV6
                if (*n < 4 + 16 + sizeof(uint16_t)) {
                    co_return Unexpected(Error::Socks5Unknown);
                }
                auto port = ::ntohs(*reinterpret_cast<const uint16_t *>(buf.get() + 4 + 16));
                auto addr = IPAddress6::fromRaw(buf.get() + 4, 16);
                mServerBound = IPEndpoint(addr, port);
                ILIAS_TRACE("Socks5", "Server bound to {}", mServerBound);
                break;
            }
            case 0x03: { // Domain name
                auto len = buf[4];
                if (*n < 4 + 1 + len + sizeof(uint16_t)) {
                    co_return Unexpected(Error::Socks5Unknown);
                }
                break;
            }
            default: {
                co_return Unexpected(Error::Socks5Unknown);
            }
        }
        ILIAS_TRACE("Socks5", "Connect done");
        co_return {};
    }

    T &mStream;
    std::string mUser;
    std::string mPassword;
    bool mHandshakeDone = false;
    IPEndpoint mServerBound;
};

ILIAS_NS_END