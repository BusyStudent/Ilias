#pragma once

#include "../expected.hpp"
#include "../task.hpp"
#include "../inet.hpp"

ILIAS_NS_BEGIN

/**
 * @brief Interface for provide async network services
 * 
 */
class IoContext : public EventLoop {
public:
    // Socket
    virtual auto addSocket(SocketView fd) -> Result<void> = 0;
    virtual auto removeSocket(SocketView fd) -> Result<void> = 0;
    
    virtual auto send(SocketView fd, const void *buffer, size_t n) -> Task<size_t> = 0;
    virtual auto recv(SocketView fd, void *buffer, size_t n) -> Task<size_t> = 0;
    virtual auto connect(SocketView fd, const IPEndpoint &endpoint) -> Task<void> = 0;
    virtual auto accept(SocketView fd) -> Task<std::pair<Socket, IPEndpoint> > = 0;
    virtual auto sendto(SocketView fd, const void *buffer, size_t n, const IPEndpoint &endpoint) -> Task<size_t> = 0;
    virtual auto recvfrom(SocketView fd, void *buffer, size_t n) -> Task<std::pair<size_t, IPEndpoint> > = 0;

    // Poll socket fd    
    virtual auto poll(SocketView fd, uint32_t events) -> Task<uint32_t> = 0;

    // Instance
    static auto instance() -> IoContext * {
        return dynamic_cast<IoContext*>(EventLoop::instance());
    }
};

// --- Forward declare
template <typename T>
inline auto SendAll(T &client, const void *buffer, size_t n) -> Task<size_t>;
template <typename T>
inline auto RecvAll(T &client, void *buffer, size_t n) -> Task<size_t>;

/**
 * @brief Send all the data to, it will send data as more as possible
 * 
 * @tparam T
 * 
 * @param client 
 * @param buffer 
 * @param n 
 * @return Task<size_t> 
 */
template <typename T>
inline auto SendAll(T &client, const void *buffer, size_t n) -> Task<size_t> {
    auto cur = static_cast<const uint8_t*>(buffer);
    size_t sended = 0;
    while (n > 0) {
        auto ret = co_await client.send(buffer, n);
        if (!ret) {
            co_return Unexpected(ret.error());
        }
        if (*ret == 0) {
            break;
        }
        sended += *ret;
        n -= *ret;
        cur += *ret;
    }
    co_return sended;
}

/**
 * @brief Recv data from, it will try to recv data as more as possible
 * 
 * @param client 
 * @param buffer 
 * @param n 
 * @return Task<size_t> 
 */
template <typename T>
inline auto RecvAll(T &client, void *buffer, size_t n) -> Task<size_t> {
    auto cur = static_cast<uint8_t*>(buffer);
    size_t received = 0;
    while (n > 0) {
        auto ret = co_await client.recv(cur, n);
        if (!ret) {
            co_return Unexpected(ret.error());
        }
        if (*ret == 0) {
            break;
        }
        received += *ret;
        n -= *ret;
        cur += *ret;
    }
    co_return received;
}

ILIAS_NS_END