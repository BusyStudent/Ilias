/**
 * @file uring_sock.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Wrapping socket operation on io_uring
 * @version 0.1
 * @date 2024-09-26
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/platform/detail/uring_core.hpp>
#include <ilias/io/system_error.hpp>
#include <ilias/net/endpoint.hpp>
#include <ilias/net/sockfd.hpp>
#include <span>

ILIAS_NS_BEGIN

namespace detail {

/**
 * @brief Awaiter wrapping the sendmsg
 * 
 */
class UringSendmsgAwaiter final : public UringAwaiter<UringSendmsgAwaiter> {
public:
    UringSendmsgAwaiter(::io_uring &ring, int fd, const ::msghdr &msg, int flags) : 
        UringAwaiter(ring), mMsg(msg), mFd(fd), mFlags(flags)
    {
        
    }

    auto onSubmit() {
        ILIAS_TRACE("Uring", "Prep sendmsg for fd {}", mFd);
        ::io_uring_prep_sendmsg(sqe(), mFd, &mMsg, mFlags);
    }

    auto onComplete(int64_t ret) -> Result<size_t> {
        if (ret < 0) { //< Acrodding to the man page, negative return values is error (-errno)
            return Unexpected(SystemError(-ret));
        }
        return size_t(ret);
    }
private:
    const ::msghdr &mMsg;
    int             mFd;
    int             mFlags;
};

/**
 * @brief Wrapping the recvmsg
 * 
 */
class UringRecvmsgAwaiter final : public UringAwaiter<UringRecvmsgAwaiter> {
public:
    UringRecvmsgAwaiter(::io_uring &ring, int fd, ::msghdr &msg, int flags) :
        UringAwaiter(ring), mMsg(msg), mFd(fd), mFlags(flags)
    {

    }

    auto onSubmit() {
        ILIAS_TRACE("Uring", "Prep recvmsg for fd {}", mFd);
        ::io_uring_prep_recvmsg(sqe(), mFd, &mMsg, mFlags);
    }

    auto onComplete(int64_t ret) -> Result<size_t> {
        if (ret < 0) {
            return Unexpected(SystemError(-ret));
        }
        return size_t(ret);
    }
private:
    ::msghdr &mMsg;
    int       mFd;
    int       mFlags;
};

/**
 * @brief Wrapping the connect
 * 
 */
class UringConnectAwaiter final : public UringAwaiter<UringConnectAwaiter> {
public:
    UringConnectAwaiter(::io_uring &ring, int fd, EndpointView endpoint) : 
        UringAwaiter(ring), mFd(fd), mEndpoint(endpoint) 
    {
        
    }

    auto onSubmit() {
        ILIAS_TRACE("Uring", "Prep connect {} for fd {}", mEndpoint, mFd);
        ::io_uring_prep_connect(sqe(), mFd, mEndpoint.data(), mEndpoint.length());
    }

    auto onComplete(int64_t ret) -> Result<void> {
        if (ret < 0) {
            return Unexpected(SystemError(-ret));
        }
        return {};
    }
private:
    int mFd;
    EndpointView mEndpoint;
};

/**
 * @brief Wrapping the accept
 * 
 */
class UringAcceptAwaiter final : public UringAwaiter<UringAcceptAwaiter> {
public:
    UringAcceptAwaiter(::io_uring &ring, int fd, MutableEndpointView endpoint) : 
        UringAwaiter(ring), mFd(fd)
    {
        mAddr = endpoint.data();
        mLen = endpoint.bufsize();
    }

    auto onSubmit() {
        ILIAS_TRACE("Uring", "Prep accept for fd {}", mFd);
        ::io_uring_prep_accept(sqe(), mFd, mAddr, &mLen, 0);
    }

    auto onComplete(int64_t ret) -> Result<socket_t> {
        if (ret < 0) {
            return Unexpected(SystemError(-ret));
        }
        return socket_t(ret);
    }
private:
    int mFd;
    ::sockaddr *mAddr;
    ::socklen_t mLen;
};

/**
 * @brief Wrapping the poll
 * 
 */
class UringPollAwaiter final : public UringAwaiter<UringPollAwaiter> {
public:
    UringPollAwaiter(::io_uring &ring, int fd, uint32_t events) :
        UringAwaiter(ring), mFd(fd), mEvents(events)
    {

    }

    auto onSubmit() {
        ILIAS_TRACE("Uring", "Prep poll for fd {}, events {}", mFd, mEvents);
        ::io_uring_prep_poll_add(sqe(), mFd, mEvents);
    }

    auto onComplete(int64_t ret) -> Result<uint32_t> {
        if (ret < 0) {
            return Unexpected(SystemError(-ret));
        }
        return uint32_t(ret);
    }
private:
    int mFd;
    uint32_t mEvents;
};

}

ILIAS_NS_END