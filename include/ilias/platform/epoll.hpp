/**
 * @file epoll.hpp
 * @author llhsdmd (llhsdmd@gmail.com)
 * @brief Impl the epoll asyncio on the linux platform
 * @version 0.1
 * @date 2024-09-03
 *
 * @copyright Copyright (c) 2024
 *
 */

#pragma once

#include <sys/epoll.h>
#include <unordered_map>
#include <array>
#include <list>
#include <algorithm>
#include <sys/stat.h>
#include <unistd.h>

#include <ilias/platform/detail/epoll_event.hpp>
#include <ilias/cancellation_token.hpp>
#include <ilias/detail/timer.hpp>
#include <ilias/task/executor.hpp>
#include <ilias/task/task.hpp>
#include <ilias/net/endpoint.hpp>
#include <ilias/net/system.hpp>
#include <ilias/net/sockfd.hpp>
#include <ilias/io/context.hpp>
#include <ilias/log.hpp>

ILIAS_NS_BEGIN

namespace detail {

/**
 * @brief The IOCP descriptor, if alloc is too frequently, maybe we can use memory pool
 *
 */
class EpollDescriptor final : public IoDescriptor {
public:
    int                fd         = -1;
    IoDescriptor::Type type       = Unknown;
    bool               isPollable = false;

    // < For Socket data
    struct {
        int family   = 0;
        int stype    = 0;
        int protocol = 0;
    };

    ::std::unordered_map<uint32_t, ::std::list<detail::EpollEvent>> events;
};

} // namespace detail

class EpollContext final : public IoContext {
public:
    EpollContext();
    EpollContext(const EpollContext &) = delete;
    ~EpollContext();

    ///> @brief Add a new system descriptor to the context
    auto addDescriptor(fd_t fd, IoDescriptor::Type type) -> Result<IoDescriptor *> override;
    ///> @brief Remove a descriptor from the context
    auto removeDescriptor(IoDescriptor *fd) -> Result<void> override;

    ///> @brief Read from a descriptor
    auto read(IoDescriptor *fd, ::std::span<::std::byte> buffer, ::std::optional<size_t> offset)
        -> Task<size_t> override;
    ///> @brief Write to a descriptor
    auto write(IoDescriptor *fd, ::std::span<const ::std::byte> buffer, ::std::optional<size_t> offset)
        -> Task<size_t> override;

    ///> @brief Connect to a remote endpoint
    auto connect(IoDescriptor *fd, const IPEndpoint &endpoint) -> Task<void> override;
    ///> @brief Accept a connection
    auto accept(IoDescriptor *fd, IPEndpoint *remoteEndpoint) -> Task<socket_t> override;

    ///> @brief Send data to a remote endpoint
    auto sendto(IoDescriptor *fd, ::std::span<const ::std::byte> buffer, int flags, const IPEndpoint *endpoint)
        -> Task<size_t> override;
    ///> @brief Receive data from a remote endpoint
    auto recvfrom(IoDescriptor *fd, ::std::span<::std::byte> buffer, int flags, IPEndpoint *endpoint)
        -> Task<size_t> override;

    ///> @brief Poll a descriptor for events
    auto poll(IoDescriptor *fd, uint32_t event) -> Task<uint32_t> override;

    ///> @brief Post a callable to the executor
    auto post(void (*fn)(void *), void *args) -> void override;

    ///> @brief Enter and run the task in the executor, it will infinitely loop until the token is canceled
    auto run(CancellationToken &token) -> void override;

    ///> @brief Sleep for a specified amount of time
    auto sleep(uint64_t ms) -> Task<void> override;

private:
    struct PostTask {
        void (*fn)(void *);
        void *args;
    };
    auto processCompletion(int timeout) -> void;
    auto processTaskByPost() -> void;
    auto processEvents(int fd, uint32_t events) -> void;

    ///> @brief The epoll file descriptor
    SockInitializer                                      mInit;
    int                                                  mEpollFd = -1;
    detail::TimerService                                 mService {*this};
    int                                                  mTaskSneder   = -1;
    int                                                  mTaskReceiver = -1;
    ::std::array<epoll_event, 64>                        mEvents;
    ::std::unordered_map<int, detail::EpollDescriptor *> mDescriptors;
};

inline EpollContext::EpollContext() {
    mEpollFd = epoll_create1(0);
    if (mEpollFd == -1) {
        ILIAS_WARN("Epoll", "Failed to create epoll file descriptor");
        ILIAS_ASSERT(false);
    }
    int fds[2];
    if (::pipe(fds) == -1) {
        ILIAS_WARN("Epoll", "Failed to create pipe socket");
        ILIAS_ASSERT(false);
    }
    else {
        mTaskSneder   = fds[0];
        mTaskReceiver = fds[1];
        fcntl(mTaskSneder, F_SETFL, O_NONBLOCK);
        fcntl(mTaskReceiver, F_SETFL, O_NONBLOCK);
        addDescriptor(mTaskReceiver, IoDescriptor::Socket);
        epoll_event event = {};
        event.events      = EPOLLIN;
        event.data.fd     = mTaskReceiver;
        auto ret          = ::epoll_ctl(mEpollFd, EPOLL_CTL_ADD, mTaskReceiver, &event);
        if (ret == -1) {
            ILIAS_ERROR("Epoll", "Failed to add receiver to epoll. error: {}", errno);
            ILIAS_ASSERT(false);
        }
    }
}

inline EpollContext::~EpollContext() {
    if (mEpollFd != -1) {
        close(mEpollFd);
    }
}

inline auto EpollContext::addDescriptor(fd_t fd, IoDescriptor::Type type) -> Result<IoDescriptor *> {
    if (fd == -1) {
        ILIAS_WARN("Epoll", "Invalid file descriptor");
        return Unexpected(Error::InvalidArgument);
    }
    if (type == IoDescriptor::Unknown) {
        struct stat statbuf;
        if (fd == fileno(stdin) || fd == fileno(stdout) || fd == fileno(stderr)) {
            type = IoDescriptor::Tty;
        }
        else {
            if (fstat(fd, &statbuf) == -1) {
                ILIAS_WARN("Epoll", "Failed to fstat file descriptor. error: {}", errno);
                return Unexpected(Error::InvalidArgument);
            }
            if (S_ISFIFO(statbuf.st_mode)) {
                type = IoDescriptor::Pipe;
            }
            else if (S_ISSOCK(statbuf.st_mode)) {
                type = IoDescriptor::Socket;
            }
            else if (S_ISREG(statbuf.st_mode)) {
                type = IoDescriptor::File;
            }
            else {
                ILIAS_WARN("Epoll", "Unknown file descriptor type");
                return Unexpected(Error::InvalidArgument);
            }
        }
    }

    auto nfd        = std::make_unique<detail::EpollDescriptor>();
    nfd->fd         = fd;
    nfd->type       = type;
    nfd->isPollable = false;

    if (type == IoDescriptor::Socket) {
        nfd->isPollable = true;
        // 获取socket的协议族
        socklen_t len;
        if (getsockopt(fd, SOL_SOCKET, SO_DOMAIN, &nfd->family, &len) == -1) {
            ILIAS_WARN("Epoll", "Failed to get socket domain. error: {}", errno);
        }
        if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &nfd->stype, &len) == -1) {
            ILIAS_WARN("Epoll", "Failed to get socket type. error: {}", errno);
        }
        if (getsockopt(fd, SOL_SOCKET, SO_PROTOCOL, &nfd->protocol, &len) == -1) {
            ILIAS_WARN("Epoll", "Failed to get socket protocol. error: {}", errno);
        }
    }

    if (type == IoDescriptor::Pipe) {
        nfd->isPollable = true;
    }

    fcntl(fd, F_SETFD, O_NONBLOCK);

    return nfd.release();
}

inline auto EpollContext::removeDescriptor(IoDescriptor *fd) -> Result<void> {
    auto descriptor = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_ASSERT(descriptor != nullptr);
    int ret = 0;
    if (descriptor->isPollable) {
        auto &epollevents = descriptor->events;
        for (auto &[event, epolleventawaiters] : epollevents) {
            for (auto &epolleventawaiter : epolleventawaiters) {
                if (epolleventawaiter.isResumed) {
                    ret++;
                }
                else {
                    detail::EpollAwaiter::onCancel(epolleventawaiter.data);
                }
            }
        }
    }
    if (ret != 0) {
        return Unexpected<Error>(SystemError(EALREADY));
    }
    return Result<void>();
}

inline auto EpollContext::read(IoDescriptor *fd, ::std::span<::std::byte> buffer, ::std::optional<size_t> offset)
    -> Task<size_t> {
    auto descriptor = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_ASSERT(descriptor != nullptr);
    if (!descriptor->isPollable) {
        // not supported operation
        co_return Unexpected(Error::OperationNotSupported);
    }
    ILIAS_ASSERT(descriptor->type != detail::EpollDescriptor::Unknown);
    while (true) {
        int ret = 0;
        if (descriptor->type == detail::EpollDescriptor::Socket) {
            ret = ::recv(descriptor->fd, buffer.data(), buffer.size(), MSG_DONTWAIT);
        }
        else if (descriptor->type == detail::EpollDescriptor::File) {
            ret = ::pread(descriptor->fd, buffer.data(), buffer.size(), offset.value_or(0));
        }
        else {
            ret = ::read(descriptor->fd, buffer.data(), buffer.size());
        }
        if (ret >= 0) {
            co_return ret;
        }
        else if (ret < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            co_return Unexpected(SystemError::fromErrno());
        }
        auto pollRet = co_await poll(descriptor, EPOLLIN);
        if (!pollRet && pollRet.error() != SystemError(EINTR) && pollRet.error() != SystemError(EAGAIN)) {
            co_return Unexpected(pollRet.error());
        }
    }
    co_return Unexpected(Error::Unknown);
}

inline auto EpollContext::write(IoDescriptor *fd, ::std::span<const ::std::byte> buffer, ::std::optional<size_t> offset)
    -> Task<size_t> {
    auto descriptor = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_ASSERT(descriptor != nullptr);
    if (!descriptor->isPollable) {
        // not supported operation
        co_return Unexpected(Error::OperationNotSupported);
    }
    ILIAS_ASSERT(descriptor->type != detail::EpollDescriptor::Unknown);
    while (true) {
        int ret = 0;
        if (descriptor->type == detail::EpollDescriptor::Socket) {
            ret = ::send(descriptor->fd, buffer.data(), buffer.size(), MSG_DONTWAIT);
        }
        else if (descriptor->type == detail::EpollDescriptor::File) {
            ret = ::pwrite(descriptor->fd, buffer.data(), buffer.size(), offset.value_or(0));
        }
        else {
            ret = ::write(descriptor->fd, buffer.data(), buffer.size());
        }
        if (ret >= 0) {
            co_return ret;
        }
        else if (ret < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            co_return Unexpected(SystemError::fromErrno());
        }
        auto pollRet = co_await poll(descriptor, EPOLLOUT);
        if (!pollRet && pollRet.error() != SystemError(EINTR) && pollRet.error() != SystemError(EAGAIN)) {
            co_return Unexpected(pollRet.error());
        }
    }
    co_return Unexpected(Error::Unknown);
}

inline auto EpollContext::connect(IoDescriptor *fd, const IPEndpoint &endpoint) -> Task<void> {
    auto descriptor = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_ASSERT(descriptor != nullptr);
    ILIAS_ASSERT(descriptor->type == IoDescriptor::Socket);
    ILIAS_ASSERT(descriptor->fd != -1);
    auto ret = ::connect(descriptor->fd, endpoint.address().cast<sockaddr *>(), endpoint.address().length());
    if (ret == 0) {
        co_return Result<void>();
    }
    else if (errno != EINPROGRESS && errno != EAGAIN) {
        co_return Unexpected(SystemError::fromErrno());
    }
    auto pollRet = co_await poll(descriptor, EPOLLOUT);
    if (!pollRet) {
        co_return Unexpected(pollRet.error());
    }
    int       sockErr    = 0;
    socklen_t sockErrLen = sizeof(sockErr);
    if (::getsockopt(descriptor->fd, SOL_SOCKET, SO_ERROR, &sockErr, &sockErrLen) == -1) {
        co_return Unexpected(SystemError::fromErrno());
    }
    if (sockErr != 0) {
        co_return Unexpected(SystemError(sockErr));
    }
    co_return Result<void>();
}

inline auto EpollContext::accept(IoDescriptor *fd, IPEndpoint *remoteEndpoint) -> Task<socket_t> {
    auto descriptor = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_ASSERT(descriptor != nullptr);
    ILIAS_ASSERT(descriptor->type == IoDescriptor::Socket);
    ILIAS_ASSERT(descriptor->fd != -1);
    while (true) {
        sockaddr_storage storage;
        socklen_t        storageLen = sizeof(storage);
        auto             ret        = ::accept(descriptor->fd, reinterpret_cast<sockaddr *>(&storage), &storageLen);
        if (ret == 0) {
            if (remoteEndpoint != nullptr) {
                *remoteEndpoint->fromRaw(&storage, storageLen);
            }
            co_return ret;
        }
        if (ret == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
            co_return Unexpected(SystemError::fromErrno());
        }
        auto pollRet = co_await poll(fd, EPOLLIN);
        if (!pollRet) {
            co_return Unexpected(pollRet.error());
        }
    }
    co_return Unexpected(Error::Unknown);
}

inline auto EpollContext::sendto(IoDescriptor *fd, ::std::span<const ::std::byte> buffer, int flags,
                                 const IPEndpoint *endpoint) -> Task<size_t> {
    auto descriptor = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_ASSERT(descriptor != nullptr);
    ILIAS_ASSERT(descriptor->type == IoDescriptor::Socket);
    ILIAS_ASSERT(descriptor->fd != -1);
    while (true) {
        int ret = -1;
        if (endpoint != nullptr) {
            ret = ::sendto(descriptor->fd, buffer.data(), buffer.size(), flags, endpoint->cast<sockaddr *>(),
                           endpoint->length());
        }
        else {
            ret = ::send(descriptor->fd, buffer.data(), buffer.size(), MSG_DONTWAIT);
        }
        if (ret >= 0) {
            co_return ret;
        }
        else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            co_return Unexpected(SystemError::fromErrno());
        }
        auto pollRet = co_await poll(descriptor, EPOLLOUT);
        if (!pollRet && pollRet.error() != SystemError(EINTR) && pollRet.error() != SystemError(EAGAIN)) {
            co_return Unexpected(pollRet.error());
        }
    }
    co_return Unexpected(Error::Unknown);
}

inline auto EpollContext::recvfrom(IoDescriptor *fd, ::std::span<::std::byte> buffer, int flags, IPEndpoint *endpoint)
    -> Task<size_t> {
    auto descriptor = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_ASSERT(descriptor != nullptr);
    ILIAS_ASSERT(descriptor->type == IoDescriptor::Socket);
    ILIAS_ASSERT(descriptor->fd != -1);
    while (true) {
        int ret = -1;
        if (endpoint != nullptr) {
            sockaddr_storage storage;
            socklen_t        len = sizeof(storage);
            ret                  = ::recvfrom(descriptor->fd, buffer.data(), buffer.size(), flags,
                                              reinterpret_cast<sockaddr *>(&storage), &len);
            if (ret >= 0) {
                endpoint->fromRaw(&storage, len);
            }
        }
        else {
            ret = ::recv(descriptor->fd, buffer.data(), buffer.size(), MSG_DONTWAIT);
        }
        if (ret >= 0) {
            co_return ret;
        }
        else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            co_return Unexpected(SystemError::fromErrno());
        }
        auto pollRet = co_await poll(descriptor, EPOLLOUT);
        if (!pollRet && pollRet.error() != SystemError(EINTR) && pollRet.error() != SystemError(EAGAIN)) {
            co_return Unexpected(pollRet.error());
        }
    }
    co_return Unexpected(Error::Unknown);
}

inline auto EpollContext::poll(IoDescriptor *fd, uint32_t event) -> Task<uint32_t> {
    auto descriptor = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_ASSERT(descriptor != nullptr);
    ::std::unordered_map<uint32_t, ::std::list<detail::EpollEvent>>::iterator eventsIt = descriptor->events.find(event);
    if (eventsIt == descriptor->events.end()) {
        eventsIt = descriptor->events.emplace_hint(descriptor->events.begin(), descriptor->fd,
                                                   std::list<detail::EpollEvent> {});
    }
    auto &epollevent   = eventsIt->second.emplace_back(detail::EpollEvent {descriptor->fd, mEpollFd, event});
    auto  epolleventIt = eventsIt->second.end();
    if (descriptor->events.size() > 1) {
        epoll_event event = {0};
        event.events      = EPOLLRDHUP;
        auto ret          = ::epoll_ctl(mEpollFd, EPOLL_CTL_ADD, descriptor->fd, &event);
        if (ret != 0) {
            co_return Unexpected<Error>(SystemError::fromErrno());
        }
    }
    auto ret = co_await detail::EpollAwaiter(epollevent);
    eventsIt->second.erase(epolleventIt);
    if (eventsIt->second.empty()) {
        descriptor->events.erase(eventsIt);
    }
    if (descriptor->events.empty()) {
        auto ret = ::epoll_ctl(mEpollFd, EPOLL_CTL_DEL, descriptor->fd, nullptr);
        if (ret != 0) {
            ILIAS_WARN("Epoll", "Failed to remove fd {} from epoll: {}", descriptor->fd, strerror(errno));
        }
    }
    co_return ret;
}

inline auto EpollContext::post(void (*fn)(void *), void *args) -> void {
    ILIAS_ASSERT(fn != nullptr);
    PostTask task {fn, args};
    char    *buf[sizeof(PostTask)] = {0};
    memcpy(buf, &task, sizeof(task));
    // FIXME: this is a block call, if buffer is full and block main thread will be blocked
    auto ret = ::write(mTaskSneder, buf, sizeof(buf));
    if (ret != sizeof(buf)) {
        ILIAS_WARN("Epoll", "Failed to post task to epoll. error: {}", errno);
        ILIAS_ASSERT(false);
    }
}

inline auto EpollContext::run(CancellationToken &token) -> void {
    int timeout = ::std::numeric_limits<int>::max();
    while (!token.isCancelled()) {
        auto nextTimepoint = mService.nextTimepoint();
        if (nextTimepoint) {
            auto diffRaw = *nextTimepoint - ::std::chrono::steady_clock::now();
            auto diffMs  = ::std::chrono::duration_cast<::std::chrono::milliseconds>(diffRaw).count();
            timeout      = ::std::clamp<int>(diffMs, 0, ::std::numeric_limits<int>::max() - 1);
        }
        mService.updateTimers();
        processCompletion(timeout);
    }
}

inline auto EpollContext::sleep(uint64_t ms) -> Task<void> {
    co_return co_await mService.sleep(ms);
}

inline auto EpollContext::processCompletion(int timeout) -> void {
    auto ret = ::epoll_wait(mEpollFd, mEvents.data(), mEvents.size(), timeout);
    if (ret < 0 && errno != EINTR) {
        ILIAS_WARN("Epoll", "Failed to wait for events. error: {}", errno);
        ILIAS_ASSERT(false);
    }
    for (int i = 0; i < ret; ++i) {
        if (mEvents[i].data.fd == mTaskReceiver) {
            processTaskByPost();
        }
        else {
            processEvents(mEvents[i].data.fd, mEvents[i].events);
        }
    }
}

inline auto EpollContext::processTaskByPost() -> void {
    while (true) {
        PostTask task;
        auto     readLen = ::read(mTaskReceiver, &task, sizeof(task));
        if (readLen != sizeof(task)) {
            break;
        }
        ILIAS_TRACE("Epoll", "Call callback function ({}, {})", (void *)task.fn, (void *)task.args);
        task.fn(task.args);
    }
}

inline auto EpollContext::processEvents(int fd, uint32_t events) -> void {
    auto descriptorItem = mDescriptors.find(fd);
    if (descriptorItem == mDescriptors.end()) {
        ILIAS_ERROR("Epoll", "Descriptor not found: {}", fd);
        epoll_event event;
        if (fd != -1)
            ::epoll_ctl(mEpollFd, EPOLL_CTL_DEL, fd, &event);
        return;
    }
    auto &epollevents = descriptorItem->second->events;
    if ((events & EPOLLERR) || (events & EPOLLHUP)) {
        for (auto &[event, epolleventawaiters] : epollevents) {
            for (auto &epolleventawaiter : epolleventawaiters) {
                detail::EpollAwaiter::onCompletion(events, epolleventawaiter.data);
            }
        }
        if (events & EPOLLIN) {
            auto epolleventawaiters = epollevents.find(EPOLLIN);
            if (epolleventawaiters != epollevents.end()) {
                if (epolleventawaiters->second.size() > 0) {
                    detail::EpollAwaiter::onCompletion(events, epolleventawaiters->second.begin()->data);
                }
            }
        }
    }
    if (events & EPOLLOUT) {
        auto epolleventawaiters = epollevents.find(EPOLLOUT);
        if (epolleventawaiters != epollevents.end()) {
            if (epolleventawaiters->second.size() > 0) {
                detail::EpollAwaiter::onCompletion(events, epolleventawaiters->second.begin()->data);
            }
        }
    }
}

ILIAS_NS_END