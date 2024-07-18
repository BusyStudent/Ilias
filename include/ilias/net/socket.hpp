#pragma once

#include "backend.hpp"

ILIAS_NS_BEGIN

/**
 * @brief A helper class for impl async socket
 * 
 */
class AsyncSocket {
public:
    AsyncSocket(IoContext &ctxt, Socket &&sockfd);
    AsyncSocket(const AsyncSocket &) = delete;
    AsyncSocket(AsyncSocket &&) = default;
    AsyncSocket() = default;
    ~AsyncSocket();

    /**
     * @brief Get the os socket fd
     * 
     * @return socket_t 
     */
    auto get() const -> socket_t;

    /**
     * @brief Get the contained socket's view
     * 
     * @return SocketView 
     */
    auto view() const -> SocketView;

    /**
     * @brief Check current socket is valid
     * 
     * @return true 
     * @return false 
     */
    auto isValid() const -> bool;

    /**
     * @brief Get the endpoint of the socket
     * 
     * @return Result<IPEndpoint> 
     */
    auto localEndpoint() const -> Result<IPEndpoint>;

    /**
     * @brief Set the Reuse Addr object, allow multiple sockets bind to same address
     * 
     * @param reuse 
     * @return Result<> 
     */
    auto setReuseAddr(bool reuse) -> Result<>;

    /**
     * @brief Set the Socket Option object
     * 
     * @param level 
     * @param optname 
     * @param optval 
     * @param optlen 
     * @return Result<> 
     */
    auto setOption(int level, int optname, const void *optval, socklen_t optlen) -> Result<>;
    
    /**
     * @brief Get the Socket Option object
     * 
     * @param level 
     * @param optname 
     * @param optval 
     * @param optlen 
     * @return Result<> 
     */
    auto getOption(int level, int optname, void *optval, socklen_t *optlen) -> Result<>;

    /**
     * @brief Close current socket
     * 
     * @return Result<> 
     */
    auto close() -> Result<>;

    /**
     * @brief Shutdown current socket
     * 
     * @param how default in Shutdown::Both (all read and write)
     * @return Task<> 
     */
    auto shutdown(int how = Shutdown::Both) -> Task<>;

    /**
     * @brief Poll current socket use PollEvent::In or PollEvent::Out
     * 
     * @param event 
     * @return Task<uint32_t> actually got events
     */
    auto poll(uint32_t event) -> Task<uint32_t>;

    /**
     * @brief Get the context of the socket
     * 
     * @return IoContext* 
     */
    auto context() const -> IoContext *;
    
    /**
     * @brief Assign
     * 
     * @return AsyncSocket& 
     */
    auto operator =(AsyncSocket &&) -> AsyncSocket &;

    /**
     * @brief Cast to socket view
     * 
     * @return SocketView 
     */
    explicit operator SocketView() const noexcept;
protected:
    IoContext *mContext = nullptr;
    Socket     mFd;
};

// --- AsyncSocket Impl
inline AsyncSocket::AsyncSocket(IoContext &ctxt, Socket &&sockfd) : 
    mContext(&ctxt), mFd(std::move(sockfd)) 
{
    if (!mContext->addSocket(mFd)) {
        mFd.close();
    }
}
inline AsyncSocket::~AsyncSocket() {
    if (mContext && mFd.isValid()) {
        mContext->removeSocket(mFd);
    }
}

inline auto AsyncSocket::get() const -> socket_t {
    return mFd.get();
}
inline auto AsyncSocket::view() const -> SocketView {
    return mFd;
}
inline auto AsyncSocket::context() const -> IoContext * {
    return mContext;
}
inline auto AsyncSocket::close() -> Result<> {
    if (mContext && mFd.isValid()) {
        mContext->removeSocket(mFd);
        if (!mFd.close()) {
            return Unexpected(Error::fromErrno());
        }
    }
    return Result<>();
}
inline auto AsyncSocket::shutdown(int how) -> Task<> {
    co_return mFd.shutdown(how);
}
inline auto AsyncSocket::poll(uint32_t event) -> Task<uint32_t> {
    return mContext->poll(mFd, event);
}
inline auto AsyncSocket::setReuseAddr(bool reuse) -> Result<> {
    return mFd.setReuseAddr(reuse);
}
inline auto AsyncSocket::setOption(int level, int optname, const void *optval, socklen_t optlen) -> Result<> {
    return mFd.setOption(level, optname, optval, optlen);
}
inline auto AsyncSocket::getOption(int level, int optname, void *optval, socklen_t *optlen) -> Result<> {
    return mFd.getOption(level, optname, optval, optlen);
}
inline auto AsyncSocket::localEndpoint() const -> Result<IPEndpoint> {
    return mFd.localEndpoint();
}
inline auto AsyncSocket::isValid() const -> bool {
    return mFd.isValid();
}
inline auto AsyncSocket::operator =(AsyncSocket &&other) -> AsyncSocket & {
    if (&other == this) {
        return *this;
    }
    close();
    mContext = other.mContext;
    mFd = std::move(other.mFd);
    return *this;
}
inline AsyncSocket::operator SocketView() const noexcept {
    return mFd;
}

ILIAS_NS_END