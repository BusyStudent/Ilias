#pragma once

#include "socket.hpp"
#include "traits.hpp"

ILIAS_NS_BEGIN

/**
 * @brief Udp Socket Client
 * 
 */
class UdpClient final : public AsyncSocket {
public:
    UdpClient();
    UdpClient(IoContext &ctxt, int family);
    UdpClient(IoContext &ctxt, Socket &&socket);

    /**
     * @brief Bind the socket with endpoint
     * 
     * @param endpoint 
     * @return Expected<void, Error> 
     */
    auto bind(const IPEndpoint &endpoint) -> Result<>;
    /**
     * @brief Set the Broadcast flags
     * 
     * @param broadcast 
     * @return Result<> 
     */
    auto setBroadcast(bool broadcast) -> Result<>;

    /**
     * @brief Send num of the bytes to the target
     * 
     * @param buffer 
     * @param n 
     * @param endpoint 
     * @return Task<size_t> 
     */
    auto sendto(const void *buffer, size_t n, const IPEndpoint &endpoint) -> Task<size_t>;

    /**
     * @brief Recv num of the bytes from
     * 
     * @param buffer 
     * @param n 
     * @return Task<std::pair<size_t, IPEndpoint> > 
     */
    auto recvfrom(void *buffer, size_t n) -> Task<std::pair<size_t, IPEndpoint> >;
};


// --- UdpSocket Impl
inline UdpClient::UdpClient() { }
inline UdpClient::UdpClient(IoContext &ctxt, int family) :
    AsyncSocket(ctxt, Socket(family, SOCK_DGRAM, IPPROTO_UDP)) 
{

}
inline UdpClient::UdpClient(IoContext &ctxt, Socket &&socket): 
    AsyncSocket(ctxt, std::move(socket)) 
{

}
inline auto UdpClient::bind(const IPEndpoint &endpoint) -> Result<> {
    return mFd.bind(endpoint);
}
inline auto UdpClient::setBroadcast(bool v) -> Result<> {
    int intFlags = v ? 1 : 0;
    return mFd.setOption(SOL_SOCKET, SO_BROADCAST, &intFlags, sizeof(intFlags));
}

inline auto UdpClient::sendto(const void *buffer, size_t n, const IPEndpoint &endpoint) -> Task<size_t> {
    return mContext->sendto(mFd, buffer, n, endpoint);
}
inline auto UdpClient::recvfrom(void *buffer, size_t n) -> Task<std::pair<size_t, IPEndpoint> > {
    return mContext->recvfrom(mFd, buffer, n);
}

ILIAS_NS_END