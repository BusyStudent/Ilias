/**
 * @file sockbase.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The base class for all async socket classes.
 * @version 0.1
 * @date 2024-08-13
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/io/context.hpp>
#include <ilias/net/sockfd.hpp>

ILIAS_NS_BEGIN

namespace detail {

// TODO: Imrpove the socket base class
/**
 * @brief The RAII class take the ownship of the socket and register it to the context
 * 
 */
class SocketBase {
public:
    SocketBase() = default;
    SocketBase(std::nullptr_t) { }
    SocketBase(const SocketBase &) = delete;

    /**
     * @brief Construct a new Socket Base object
     * 
     * @param ctxt The io context reference
     * @param sock The socket, this class will take the ownship of this
     */
    SocketBase(IoContext &ctxt, Socket &&sock) : mCtxt(&ctxt), mSock(std::move(sock)) {
        auto fd = mCtxt->addDescriptor(fd_t(mSock.get()), IoDescriptor::Socket);
        if (!fd) {
            mSock.reset();
            return;
        }
        mFd = fd.value();
    }

    /**
     * @brief Construct a new Socket Base object by moving
     * 
     * @param other 
     */
    SocketBase(SocketBase &&other) : mFd(other.mFd), mCtxt(other.mCtxt), mSock(std::move(other.mSock)) {
        other.mFd = nullptr;
        other.mCtxt = nullptr;
    }

    /**
     * @brief Destroy the Socket Base object, do close()
     * 
     */
    ~SocketBase() {
        close();
    }

    /**
     * @brief Close remove the socket from the io context and then close the socket
     * 
     */
    auto close() -> void {
        if (!mFd) {
            return;
        }
        mCtxt->removeDescriptor(mFd);
        mSock.close();
        mCtxt = nullptr;
        mFd   = nullptr;
    }

    // as same as sync system socket
    auto send(std::span<const std::byte> data, int flags = 0) -> Task<size_t> {
        return mCtxt->sendto(mFd, data, flags, nullptr);
    }

    auto recv(std::span<std::byte> data, int flags = 0) -> Task<size_t> {
        return mCtxt->recvfrom(mFd, data, flags, nullptr);
    }

    auto sendto(std::span<const std::byte> data, int flags, EndpointView endpoint) -> Task<size_t> {
        return mCtxt->sendto(mFd, data, flags, endpoint);
    }

    auto recvfrom(std::span<std::byte> data, int flags, MutableEndpointView endpoint) -> Task<size_t> {
        return mCtxt->recvfrom(mFd, data, flags, endpoint);
    }

    auto connect(EndpointView endpoint) -> Task<void> {
        return mCtxt->connect(mFd, endpoint);
    }

    auto poll(uint32_t events) -> Task<uint32_t> {
        return mCtxt->poll(mFd, events);
    }

    auto bind(EndpointView endpoint) {
        return mSock.bind(endpoint);
    }

    auto listen(int backlog) {
        return mSock.listen(backlog);
    }

    template <MutableEndpoint T = IPEndpoint>
    auto localEndpoint() const -> Result<T> {
        return mSock.localEndpoint<T>();
    }

    template <MutableEndpoint T = IPEndpoint>
    auto remoteEndpoint() const -> Result<T> {
        return mSock.remoteEndpoint<T>();
    }

    auto accept(MutableEndpointView endpoint) -> Task<socket_t> {
        return mCtxt->accept(mFd, endpoint);
    }

    auto shutdown(int how = Shutdown::Both) -> Task<void> {
        co_return mSock.shutdown(how);
    }

    /**
     * @brief Get the IoContext*
     * 
     * @return IoContext* 
     */
    auto context() const -> IoContext * { return mCtxt; }

    /**
     * @brief Get the system socket file descriptor
     * 
     * @return const Socket& 
     */
    auto socket() const -> const Socket & { return mSock; }

    /**
     * @brief Get the IoDescriptor* 
     * 
     * @return IoDescriptor* 
     */
    auto fd() const -> IoDescriptor * { return mFd; }

    /**
     * @brief Allow comparison
     * 
     */
    auto operator <=>(const SocketBase &) const = default;

    /**
     * @brief Disallow copy assignment
     * 
     * @return auto 
     */
    auto operator =(const SocketBase &) const = delete;

    /**
     * @brief Move assignment
     * 
     * @param other 
     * @return SocketBase& 
     */
    auto operator =(SocketBase &&other) -> SocketBase & {
        if (this == &other) {
            return *this;
        }
        close();
        mFd = other.mFd;
        mCtxt = other.mCtxt;
        mSock = std::move(other.mSock);
        other.mFd = nullptr;
        other.mCtxt = nullptr;
        return *this;
    }

    /**
     * @brief Clear the socket
     * 
     * @return SocketBase& 
     */
    auto operator =(std::nullptr_t) -> SocketBase & {
        close();
        return *this;
    }

    /**
     * @brief Check the it is in valid state
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept {
        return mFd;
    }
private:
    IoDescriptor *mFd = nullptr;
    IoContext    *mCtxt = nullptr;
    Socket        mSock;
};

} // namespace detail

ILIAS_NS_END