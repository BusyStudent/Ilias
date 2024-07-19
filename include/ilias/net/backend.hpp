#pragma once

#include "../detail/expected.hpp"
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

ILIAS_NS_END