/**
 * @file uring.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Impl the io uring Context on the linux
 * @version 0.1
 * @date 2024-09-15
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/platform/detail/uring_core.hpp>
#include <ilias/platform/detail/uring_sock.hpp>
#include <ilias/platform/detail/uring_fs.hpp>
#include <ilias/task/executor.hpp>
#include <ilias/task/task.hpp>
#include <ilias/net/endpoint.hpp>
#include <ilias/net/system.hpp>
#include <ilias/net/sockfd.hpp>
#include <ilias/io/context.hpp>
#include <liburing.h>

ILIAS_NS_BEGIN

namespace detail {

/**
 * @brief The Uring Descriptor, store platform spec data
 * 
 */
class UringDescriptor final : public IoDescriptor {
public:
    int           fd;   
    struct ::stat stat; //< The file stat
};

/**
 * @brief The callback used in post
 * 
 */
class UringFunc {
public:
    void (*fn)(void *args);
    void *args;
};

} // namespace detail

/**
 * @brief The Configuration for io_uring
 * 
 */
struct UringConfig {
    unsigned int entries = 64;
    unsigned int flags = 0;
};

/**
 * @brief The io context by using io_uring
 * 
 */
class UringContext : public IoContext {
public:
    UringContext(UringConfig conf = {});
    UringContext(const UringContext &) = delete;
    ~UringContext();

    //< For Executor
    auto post(void (*fn)(void *), void *args) -> void override;
    auto run(CancellationToken &token) -> void override;

    // < For IoContext
    auto addDescriptor(fd_t fd, IoDescriptor::Type type) -> Result<IoDescriptor*> override;
    auto removeDescriptor(IoDescriptor* fd) -> Result<void> override;

    auto sleep(uint64_t ms) -> IoTask<void> override;

    auto read(IoDescriptor *fd, std::span<std::byte> buffer, std::optional<size_t> offset) -> IoTask<size_t> override;
    auto write(IoDescriptor *fd, std::span<const std::byte> buffer, std::optional<size_t> offset) -> IoTask<size_t> override;

    auto accept(IoDescriptor *fd, MutableEndpointView endpoint) -> IoTask<socket_t> override;
    auto connect(IoDescriptor *fd, EndpointView endpoint) -> IoTask<void> override;

    auto sendto(IoDescriptor *fd, std::span<const std::byte> buffer, int flags, EndpointView endpoint) -> IoTask<size_t> override;
    auto recvfrom(IoDescriptor *fd, std::span<std::byte> buffer, int flags, MutableEndpointView endpoint) -> IoTask<size_t> override;

    auto poll(IoDescriptor *fd, uint32_t event) -> IoTask<uint32_t> override;
private:
    auto processCompletion() -> void;
    auto processCallback() -> void;
    auto allocSqe() -> ::io_uring_sqe *;

    ::io_uring mRing { };

    // For handling the fn, that user posted
    int mPipeSender = -1;
    int mPipeReceiver = -1;
    detail::UringCallbackEx mPipeCallback;
};

inline UringContext::UringContext(UringConfig conf) {
    if (auto ret = ::io_uring_queue_init(conf.entries, &mRing, conf.flags); ret != 0) {
        auto err = -ret;
        ILIAS_ERROR("Uring", "Failed to io_uring_queue_init({}, {}) => {}", conf.entries, conf.flags, SystemError(err));
        return;
    }
    int pipes[2];
    if (::pipe2(pipes, O_CLOEXEC | O_NONBLOCK) == -1) {
        ILIAS_ERROR("Uring", "Failed to pipe2", SystemError::fromErrno());
        return;
    }

    // Setup the pipe to process the callback
    mPipeSender = pipes[1];
    mPipeReceiver = pipes[0];
    mPipeCallback.onCallback = [](detail::UringCallback *callback, const ::io_uring_cqe &cqe) -> void {
        auto self = static_cast<UringContext*>(static_cast<detail::UringCallbackEx*>(callback)->ptr);
        ILIAS_ASSERT(&self->mPipeCallback == callback);
        self->processCallback();
    };
    mPipeCallback.ptr = this;

    // Add poll
    auto sqe = allocSqe();
    ::io_uring_prep_poll_multishot(sqe, mPipeReceiver, POLLIN);
    ::io_uring_sqe_set_data(sqe, &mPipeCallback);

    // Version
#if defined(IO_URING_VERSION_MAJOR) && defined(IO_URING_VERSION_MINOR)
    ILIAS_TRACE("Uring", "Using liburing {}.{}", IO_URING_VERSION_MAJOR, IO_URING_VERSION_MINOR);
#endif

}

inline UringContext::~UringContext() {
    ::io_uring_queue_exit(&mRing);
    ::close(mPipeSender);
    ::close(mPipeReceiver);
}

inline auto UringContext::processCompletion() -> void {
    ::io_uring_cqe *cqe = nullptr;
    if (::io_uring_wait_cqe(&mRing, &cqe) != 0 || cqe == nullptr) [[unlikely]] {
        ILIAS_ERROR("Uring", "io_uring_wait_cqe failed {}", SystemError(-errno));
        return;
    }
    auto entry = *cqe;
    auto ptr = ::io_uring_cqe_get_data(&entry);
    auto cb = static_cast<detail::UringCallback*>(ptr);
    ::io_uring_cqe_seen(&mRing, cqe);
    ILIAS_ASSERT_MSG(cb, "May missing io_uring_sqe_set_data ?");
    if (cb->onCallback) [[likely]] {
        cb->onCallback(cb, entry);
    }
    return;
}

inline auto UringContext::processCallback() -> void {
    detail::UringFunc func;
    while (::read(mPipeReceiver, &func, sizeof(func)) == sizeof(func)) {
        func.fn(func.args);
    }
}

inline auto UringContext::allocSqe() -> ::io_uring_sqe * {
    auto sqe = ::io_uring_get_sqe(&mRing);
    if (!sqe) {
        auto n = ::io_uring_submit(&mRing);
        sqe = ::io_uring_get_sqe(&mRing);
    }
    ILIAS_ASSERT(sqe);
    return sqe;
}

inline auto UringContext::post(void (*fn)(void *), void *args) -> void {
    detail::UringFunc func {
        .fn = fn,
        .args = args
    };
    auto n = ::write(mPipeSender, &func, sizeof(func));
    if (n != sizeof(func)) {
        ILIAS_ERROR("Uring", "Failed to post fn to ring {}", SystemError::fromErrno());
        ::abort();
    }
}

inline auto UringContext::run(CancellationToken &token) -> void {
    auto reg = token.register_([this]() {
        // Alloc the noop sqe, let it wakeup the ring
        auto sqe = allocSqe();
        ::io_uring_prep_nop(sqe);
        ::io_uring_sqe_set_data(sqe, detail::UringCallback::noop());
        ::io_uring_submit(&mRing);
    });
    while (!token.isCancellationRequested()) {
        ::io_uring_submit(&mRing); //< Submit any pending request
        processCompletion();
    }
}

inline auto UringContext::addDescriptor(fd_t fd, IoDescriptor::Type type) -> Result<IoDescriptor*> {
    auto nfd = std::make_unique<detail::UringDescriptor>();
    if (::fstat(fd, &nfd->stat) != 0) {
        return Unexpected(SystemError::fromErrno());
    }
    ILIAS_TRACE("Uring", "Adding fd {}", fd);

    nfd->fd = fd;
    return nfd.release();
}

inline auto UringContext::removeDescriptor(IoDescriptor *fd) -> Result<void> {
    auto nfd = static_cast<detail::UringDescriptor*>(fd);
    ILIAS_TRACE("Uring", "Removing fd {}", nfd->fd);

#if IO_URING_VERSION_MINOR > 2
    auto sqe = allocSqe();
    ::io_uring_prep_cancel_fd(sqe, nfd->fd, 0);
    ::io_uring_sqe_set_data(sqe, detail::UringCallback::noop());
    ::io_uring_submit(&mRing);
#endif

    delete nfd;
    return {};
}

inline auto UringContext::sleep(uint64_t ms) -> IoTask<void> {
    ::timespec ts { };
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    co_return co_await detail::UringTimeoutAwaiter {mRing, ts};
}

inline auto UringContext::read(IoDescriptor *fd, std::span<std::byte> buffer, std::optional<size_t> offset) -> IoTask<size_t> {
    auto nfd = static_cast<detail::UringDescriptor*>(fd);
    co_return co_await detail::UringReadAwaiter {mRing, nfd->fd, buffer, offset};
}

inline auto UringContext::write(IoDescriptor *fd, std::span<const std::byte> buffer, std::optional<size_t> offset) -> IoTask<size_t> {
    auto nfd = static_cast<detail::UringDescriptor*>(fd);
    co_return co_await detail::UringWriteAwaiter {mRing, nfd->fd, buffer, offset};
}

inline auto UringContext::accept(IoDescriptor *fd, MutableEndpointView endpoint) -> IoTask<socket_t> {
    auto nfd = static_cast<detail::UringDescriptor*>(fd);
    co_return co_await detail::UringAcceptAwaiter {mRing, nfd->fd, endpoint};
}

inline auto UringContext::connect(IoDescriptor *fd, EndpointView endpoint) -> IoTask<void> {
    auto nfd = static_cast<detail::UringDescriptor*>(fd);
    co_return co_await detail::UringConnectAwaiter {mRing, nfd->fd, endpoint};
}

inline auto UringContext::sendto(IoDescriptor *fd, std::span<const std::byte> buffer, int flags, EndpointView endpoint) -> IoTask<size_t> {
    auto nfd = static_cast<detail::UringDescriptor*>(fd);
    ::iovec vec {
        .iov_base = (void*) buffer.data(),
        .iov_len = buffer.size()
    };
    ::msghdr msg {
        .msg_name = (::sockaddr*) endpoint.data(),
        .msg_namelen = endpoint.length(),
        .msg_iov = &vec,
        .msg_iovlen = 1,
    };
    co_return co_await detail::UringSendmsgAwaiter {mRing, nfd->fd, msg, flags};
}

inline auto UringContext::recvfrom(IoDescriptor *fd, std::span<std::byte> buffer, int flags, MutableEndpointView endpoint) -> IoTask<size_t> {
    auto nfd = static_cast<detail::UringDescriptor*>(fd);
    ::iovec vec {
        .iov_base = buffer.data(),
        .iov_len = buffer.size()
    };
    ::msghdr msg {
        .msg_name = endpoint.data(),
        .msg_namelen = endpoint.bufsize(),
        .msg_iov = &vec,
        .msg_iovlen = 1,
    };
    co_return co_await detail::UringRecvmsgAwaiter {mRing, nfd->fd, msg, flags};
}

inline auto UringContext::poll(IoDescriptor *fd, uint32_t events) -> IoTask<uint32_t> {
    auto nfd = static_cast<detail::UringDescriptor*>(fd);
    co_return co_await detail::UringPollAwaiter {mRing, nfd->fd, events};
}

ILIAS_NS_END