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
    SocketBase(SocketBase &&other) noexcept : 
        mFd(std::exchange(other.mFd, nullptr)), 
        mCtxt(std::exchange(other.mCtxt, nullptr)), 
        mSock(std::move(other.mSock)) 
    {

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
    auto send(std::span<const std::byte> data, int flags = 0) const -> IoTask<size_t> {
        return mCtxt->sendto(mFd, data, flags, nullptr);
    }

    auto recv(std::span<std::byte> data, int flags = 0) const -> IoTask<size_t> {
        return mCtxt->recvfrom(mFd, data, flags, nullptr);
    }

    auto sendto(std::span<const std::byte> data, int flags, EndpointView endpoint) const -> IoTask<size_t> {
        return mCtxt->sendto(mFd, data, flags, endpoint);
    }

    auto recvfrom(std::span<std::byte> data, int flags, MutableEndpointView endpoint) const -> IoTask<size_t> {
        return mCtxt->recvfrom(mFd, data, flags, endpoint);
    }

    auto connect(EndpointView endpoint) const -> IoTask<void> {
        return mCtxt->connect(mFd, endpoint);
    }

    auto poll(uint32_t events) const -> IoTask<uint32_t> {
        return mCtxt->poll(mFd, events);
    }

    auto bind(EndpointView endpoint) const {
        return mSock.bind(endpoint);
    }

    auto listen(int backlog) const {
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

    auto accept(MutableEndpointView endpoint) const -> IoTask<socket_t> {
        return mCtxt->accept(mFd, endpoint);
    }

    auto shutdown(int how = Shutdown::Both) const -> IoTask<void> {
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
        mFd = std::exchange(other.mFd, nullptr);
        mCtxt = std::exchange(other.mCtxt, nullptr);
        mSock = std::exchange(other.mSock, Socket{});
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
     * @brief Create the socket and added it into the context
     * 
     * @param ctxt 
     * @param family 
     * @param type 
     * @param protocol 
     * @return Result<SocketBase> 
     */
    template <typename T = SocketBase>
    static auto make(IoContext &ctxt, int family, int type, int protocol = 0) -> Result<T> {
        auto sockfd = Socket::make(family, type, protocol);
        if (!sockfd) {
            return Unexpected(sockfd.error());
        }
        auto fd = ctxt.addDescriptor(fd_t(sockfd->get()), IoDescriptor::Socket);
        if (!fd) {
            return Unexpected(fd.error());
        }
        // Done, combine them
        T base;
        base.mFd = fd.value();
        base.mCtxt = &ctxt;
        base.mSock = std::move(sockfd.value());
        return base;
    }

    /**
     * @brief Create an awaiter for construct the T object
     * 
     * @tparam T 
     * @param family 
     * @param type 
     * @param protocol 
     * @return auto 
     */
    template <typename T>
    static auto make(int family, int type, int protocol = 0) {
        struct Awaiter : public GetContextAwaiter {
            auto await_resume() -> Result<T> {
                auto base = SocketBase::make(*context(), family, type, protocol);
                if (!base) {
                    return Unexpected(base.error());
                }
                return T(std::move(*base));
            }
            int family;
            int type;
            int protocol;
        };
        Awaiter awaiter;
        awaiter.family = family;
        awaiter.type = type;
        awaiter.protocol = protocol;
        return awaiter;
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