#pragma once

#include "ilias_async.hpp"

ILIAS_NS_BEGIN

/**
 * @brief Wrap the connection to the socks5 proxy server, use it like normal TcpClient
 * 
 */
class Socks5Client {
public:
    Socks5Client(IoContext &ctxt, int family);
    Socks5Client(IoContext &ctxt, const IPEndpoint &serverEndpoint);
    Socks5Client(const Socks5Client &) = delete;
    Socks5Client(Socks5Client &&) = default;
    ~Socks5Client();

    /**
     * @brief Set the Proxy Server object
     * 
     * @param endpoint 
     */
    auto setServer(const IPEndpoint &endpoint) -> void;
    /**
     * @brief Set the Auth object
     * 
     * @param user 
     * @param password 
     */
    auto setAuth(std::string_view user, std::string_view password) -> void;
    /**
     * @brief Connect to the proxy server
     * 
     * @return Task<void> 
     */
    auto connectProxy() -> Task<void>;
    /**
     * @brief Connect to the server you want to access by socks 5 proxy
     * 
     * @param endpoint The wire-format endpoint
     * @return Task<void> 
     */
    auto connect(const IPEndpoint &endpoint) -> Task<void>;
    /**
     * @brief Connect to the server you want to access by socks 5 proxy
     * 
     * @param host The string of the host
     * @param port The port of the host
     * @return Task<void> 
     */
    auto connect(std::string_view host, uint16_t port) -> Task<void>;
    /**
     * @brief Recv the data
     * 
     * @param buf 
     * @param n 
     * @return Task<size_t> 
     */
    auto recv(void *buf, size_t n) -> Task<size_t>;
    /**
     * @brief Send the data
     * 
     * @param buf 
     * @param n 
     * @return Task<size_t> 
     */
    auto send(const void *buf, size_t n) -> Task<size_t>;
    /**
     * @brief Shutdown conenction
     * 
     * @return Task<void> 
     */
    auto shutdown() -> Task<void>;
private:
    auto _connect(uint8_t type, const void *buf, size_t n, uint16_t port) -> Task<void>;

    IPEndpoint mServer; //< The socks5 server
    TcpClient mClient;
    std::string mUser;
    std::string mPassword;
    bool mIsSocks5Connected = false;
};

// Socks5 Proto
struct Socks5Header {
    uint8_t ver;
    uint8_t nmethods;
    uint8_t methods[0];
};
static_assert(sizeof(Socks5Header) == 2);

inline Socks5Client::Socks5Client(IoContext &ctxt, int family) : mClient(ctxt, family) { }
inline Socks5Client::Socks5Client(IoContext &ctxt, const IPEndpoint &serverEndpoint) : 
    mServer(serverEndpoint),
    mClient(ctxt, mServer.family()) 
{

}
inline Socks5Client::~Socks5Client() { }

inline auto Socks5Client::setServer(const IPEndpoint &endpoint) -> void {
    mServer = endpoint;
}
inline auto Socks5Client::setAuth(std::string_view user, std::string_view password) -> void {
    mUser = user;
    mPassword = password;
}

inline auto Socks5Client::connectProxy() -> Task<void> {
    if (mIsSocks5Connected) {
        co_return Result<>();
    }
    auto err = co_await mClient.connect(mServer);
    if (!err) {
        co_return err;
    }
    uint8_t buf[256];
    auto header = reinterpret_cast<Socks5Header*>(buf);
    header->ver = 0x05;
    header->nmethods = 0x01;
    header->methods[0] = 0x00;

    // Send our require method
    auto n = co_await mClient.send(buf, sizeof(Socks5Header) + 1);
    if (!n) {
        co_return Unexpected(n.error());
    }

    // Try recv the selected method
    // Version uint8_t
    // Method uint8_t
    n = co_await mClient.recv(buf, 2);
    if (!n || n.value() != 2) {
        co_return Unexpected(n.error_or(Error::Socks5Unknown));
    }

    // Check it
    if (buf[0] != 0x05) {
        co_return Unexpected(Error::Socks5Unknown);
    }
    // TODO : Support Auth
    if (buf[1] != 0x00) {
        co_return Unexpected(Error::Socks5AuthenticationFailed);
    }

    // Done
    mIsSocks5Connected = true;
    co_return Result<void>();
}
inline auto Socks5Client::_connect(uint8_t type, const void *buf, size_t bufSize, uint16_t port) -> Task<void> {
    // Send the connect request
    if (auto ok = co_await connectProxy(); !ok) {
        co_return ok;
    }
    
    // Version uint8_t
    // Command uint8_t
    // RSV uint8_t
    // AddressType uint8_t
    // AddressData uint8_t[0]
    // Port uint16_t
    auto tmp = std::make_unique<uint8_t[]>(bufSize + 4 + 2);
    tmp[0] = 0x05;
    tmp[1] = 0x01;
    tmp[2] = 0x00;
    tmp[3] = type;
    ::memcpy(tmp.get() + 4, buf, bufSize);
    ::uint16_t portBe = ::htons(port);
    ::memcpy(tmp.get() + 4 + bufSize, &portBe, 2);

    // Send it
    auto n = co_await mClient.send(tmp.get(), bufSize + 4 + 2);
    if (!n) {
        co_return Unexpected(n.error());
    }

    // Recv the response
    // Response is
    // Version uint8_t
    // Reply uint8_t
    // RSV uint8_t
    // AddressType uint8_t
    // AddressData uint8_t[0]
    // Port uint16_t
    // Check it
    n = co_await mClient.recv(tmp.get(), 4);
    if (!n || n.value() != 4) {
        co_return Unexpected(n.error_or(Error::Socks5Unknown));
    }

    // Check it
    if (tmp[0] != 0x05) {
        co_return Unexpected(Error::Socks5Unknown);
    }
    if (tmp[1] != 0x00) {
        co_return Unexpected(Error::Socks5Unknown);
    }
    if (tmp[2] != 0x00) {
        co_return Unexpected(Error::Socks5Unknown);
    }

    // Discard address data and port left
    auto atype = tmp[3];
    size_t left = 0;
    switch (atype) {
        case 0x01:
            left = 4 + 2;
            break;
        case 0x04:
            left = 16 + 2;
            break;
        default:
            co_return Unexpected(Error::Socks5Unknown);
    }
    n = co_await mClient.recv(tmp.get(), left);
    if (!n || n.value() != left) {
        co_return Unexpected(n.error_or(Error::Socks5Unknown));
    }

    // Done
    co_return Result<void>();
}
inline auto Socks5Client::connect(const IPEndpoint &endpoint) -> Task<void> {
    auto addr = endpoint.address();
    auto type = addr.family() == AF_INET ? 0x01 : 0x04;
    co_return co_await _connect(type, addr.data(), addr.length(), endpoint.port());
}
inline auto Socks5Client::connect(std::string_view host, uint16_t port) -> Task<void> {
    std::string buffer;
    buffer.push_back(char(host.length()));
    buffer.append(host);
    co_return co_await _connect(0x3, buffer.data(), buffer.length(), port);
}
inline auto Socks5Client::send(const void *buf, size_t bufSize) -> Task<size_t> {
    return mClient.send(buf, bufSize);
}
inline auto Socks5Client::recv(void *buf, size_t bufSize) -> Task<size_t> {
    return mClient.recv(buf, bufSize);
}
inline auto Socks5Client::shutdown() -> Task<void> {
    return mClient.shutdown();
}

static_assert(StreamClient<Socks5Client>);

ILIAS_NS_END