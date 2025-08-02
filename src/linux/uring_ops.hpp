/**
 * @file uring_sock.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Wrapping all operation on io_uring
 * @version 0.1
 * @date 2024-09-26
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/io/system_error.hpp>
#include <ilias/net/endpoint.hpp>
#include <ilias/net/sockfd.hpp>
#include <span>
#include "uring_core.hpp"

ILIAS_NS_BEGIN

namespace linux {

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

    auto onComplete(int64_t ret) -> IoResult<size_t> {
        if (ret < 0) { //< Acrodding to the man page, negative return values is error (-errno)
            return Err(SystemError(-ret));
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

    auto onComplete(int64_t ret) -> IoResult<size_t> {
        if (ret < 0) {
            return Err(SystemError(-ret));
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

    auto onComplete(int64_t ret) -> IoResult<void> {
        if (ret < 0) {
            return Err(SystemError(-ret));
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

    auto onComplete(int64_t ret) -> IoResult<socket_t> {
        if (ret < 0) {
            return Err(SystemError(-ret));
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

    auto onComplete(int64_t ret) -> IoResult<uint32_t> {
        if (ret < 0) {
            return Err(SystemError(-ret));
        }
        return uint32_t(ret);
    }
private:
    int mFd;
    uint32_t mEvents;
};

class UringWriteAwaiter final : public UringAwaiter<UringWriteAwaiter> {
public:
    UringWriteAwaiter(::io_uring &ring, int fd, Buffer buffer, std::optional<size_t> offset) :
        UringAwaiter(ring), mFd(fd), mBuffer(buffer), mOffset(offset)
    {

    }

    auto onSubmit() {
        ILIAS_TRACE("Uring", "Prep write for fd {}, {} bytes", mFd, mBuffer.size());
        __u64 offset = mOffset ? mOffset.value() : __u64(-1);
        ::io_uring_prep_write(sqe(), mFd, mBuffer.data(), mBuffer.size(), offset);
    }

    auto onComplete(int64_t ret) -> IoResult<size_t> {
        if (ret < 0) {
            return Err(SystemError(ret));
        }
        return size_t(ret);
    }
private:
    int mFd;
    Buffer mBuffer;
    std::optional<size_t> mOffset;    
};

class UringReadAwaiter final : public UringAwaiter<UringReadAwaiter> {
public:
    UringReadAwaiter(::io_uring &ring, int fd, MutableBuffer buffer, std::optional<size_t> offset) :
        UringAwaiter(ring), mFd(fd), mBuffer(buffer), mOffset(offset)
    {

    }

    auto onSubmit() {
        ILIAS_TRACE("Uring", "Prep read for fd {}, {} bytes", mFd, mBuffer.size());
        __u64 offset = mOffset ? mOffset.value() : __u64(-1);
        ::io_uring_prep_read(sqe(), mFd, mBuffer.data(), mBuffer.size(), offset);
    }

    auto onComplete(int64_t ret) -> IoResult<size_t> {
        if (ret < 0) {
            return Err(SystemError(ret));
        }
        return size_t(ret);
    }
private:
    int mFd;
    MutableBuffer mBuffer;
    std::optional<size_t> mOffset;    
};

}

ILIAS_NS_END