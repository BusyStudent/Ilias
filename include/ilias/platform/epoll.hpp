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

#if __has_include(<aio.h>)
    #include <ilias/platform/detail/aio_core.hpp> //< For normal file
#endif

ILIAS_NS_BEGIN

namespace detail {

/**
 * @brief The Epoll descriptor, if alloc is too frequently, maybe we can use memory pool
 *
 */
class EpollDescriptor final : public IoDescriptor {
public:
    int                fd         = -1;
    IoDescriptor::Type type       = Unknown;
    bool               isPollable = false;
    bool               isRemoved  = false;

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
    auto readTty(IoDescriptor *fd, ::std::span<::std::byte> buffer) -> Task<size_t>;

    ///> @brief The epoll file descriptor
    SockInitializer                                      mInit;
    int                                                  mEpollFd = -1;
    detail::TimerService                                 mService;
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
        mTaskReceiver = fds[0];
        mTaskSneder   = fds[1];
        fcntl(mTaskSneder, F_SETFL, O_NONBLOCK);
        fcntl(mTaskReceiver, F_SETFL, O_NONBLOCK);
        epoll_event event = {};
        event.events      = EPOLLIN;
        event.data.fd     = mTaskReceiver;
        auto ret          = ::epoll_ctl(mEpollFd, EPOLL_CTL_ADD, mTaskReceiver, &event);
        if (ret == -1) {
            ILIAS_ERROR("Epoll", "Failed to add receiver to epoll. error: {}", errno);
            ILIAS_ASSERT(false);
        }
    }
    ILIAS_TRACE("Epoll", "Epoll context created, fd({}), make pipe({}, {})", mEpollFd, mTaskReceiver, mTaskSneder);
}

inline EpollContext::~EpollContext() {
    if (mEpollFd != -1) {
        ILIAS_TRACE("Epoll", "Epoll context destroyed, fd({})", mEpollFd);
        close(mEpollFd);
    }
    else {
        ILIAS_WARN("Epoll", "Epoll context destroyed, but fd is -1");
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
                ILIAS_WARN("Epoll", "Failed to fstat file descriptor. error: {}", SystemError::fromErrno());
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
        socklen_t len = sizeof(nfd->family);
        if (getsockopt(fd, SOL_SOCKET, SO_DOMAIN, &nfd->family, &len) == -1) {
            ILIAS_WARN("Epoll", "Failed to get socket domain. error: {}", SystemError::fromErrno());
        }
        len = sizeof(nfd->stype);
        if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &nfd->stype, &len) == -1) {
            ILIAS_WARN("Epoll", "Failed to get socket type. error: {}", SystemError::fromErrno());
        }
        len = sizeof(nfd->protocol);
        if (getsockopt(fd, SOL_SOCKET, SO_PROTOCOL, &nfd->protocol, &len) == -1) {
            ILIAS_WARN("Epoll", "Failed to get socket protocol. error: {}", SystemError::fromErrno());
        }
    }
    ILIAS_TRACE("Epoll", "Created new fd descriptor: {}, type: {}", fd, (int)type);

    if (type == IoDescriptor::Pipe || type == IoDescriptor::Tty) {
        nfd->isPollable = true;
    }
    int  flags = fcntl(fd, F_GETFL, 0);
    auto ret   = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (ret == -1) {
        ILIAS_WARN("Epoll", "Failed to set descriptor to non-blocking. error: {}", SystemError::fromErrno());
    }
    mDescriptors.insert(std::make_pair(fd, nfd.get()));
    return nfd.release();
}

inline auto EpollContext::removeDescriptor(IoDescriptor *fd) -> Result<void> {
    auto descriptor = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_ASSERT(descriptor != nullptr);
    int ret = 0;
    if (descriptor->isPollable) { // if descriptor is pollable, cancel all pollable events
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
    mDescriptors.erase(descriptor->fd);
    auto epollret = ::epoll_ctl(mEpollFd, EPOLL_CTL_DEL, descriptor->fd, nullptr); // remove from epoll control
    if (epollret != 0 && errno != ENOENT) {
        ILIAS_WARN("Epoll", "Failed to remove fd {} from epoll: {}", descriptor->fd, strerror(errno));
    }
    descriptor->isRemoved = true; // make removed flag, ask poll this descriptor is removed.
    if (descriptor->events.size() == 0) {
        delete descriptor;
    }
    else {
        post(
            +[](void *descriptor) { delete static_cast<detail::EpollDescriptor *>(descriptor); }, descriptor);
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
    if (descriptor->type == detail::EpollDescriptor::Tty) {
        co_return co_await readTty(descriptor, buffer);
    }
    while (true) {
        int ret = 0;
        if (offset.has_value()) {
            ILIAS_ASSERT(descriptor->type == detail::EpollDescriptor::File);
#if __has_include(<aio.h>)
            co_return co_await detail::AioReadAwaiter(descriptor->fd, buffer, offset);
#else
            ret = ::pread(descriptor->fd, buffer.data(), buffer.size(), offset.value_or(0));
#endif
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
    ILIAS_TRACE("Epoll", "start write {} bytes on fd {}", buffer.size(), descriptor->fd);
    ILIAS_ASSERT(descriptor != nullptr);
    if (!descriptor->isPollable) {
        // not supported operation
        co_return Unexpected(Error::OperationNotSupported);
    }
    ILIAS_ASSERT(descriptor->type != detail::EpollDescriptor::Unknown);
    while (true) {
        int ret = 0;
        if (offset.has_value()) {
            ILIAS_ASSERT(descriptor->type == detail::EpollDescriptor::File);
#if __has_include(<aio.h>)
            co_return co_await detail::AioWriteAwaiter(descriptor->fd, buffer, offset);
#else
            ret = ::pwrite(descriptor->fd, buffer.data(), buffer.size(), offset.value_or(0));
#endif
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
    ILIAS_TRACE("Epoll", "start connect to {} on fd {}", endpoint, descriptor->fd);
    auto ret = ::connect(descriptor->fd, &endpoint.cast<sockaddr>(), endpoint.length());
    if (ret == 0) {
        ILIAS_TRACE("Epoll", "{} connect to {} successful", descriptor->fd, endpoint);
        co_return Result<void>();
    }
    else if (errno != EINPROGRESS && errno != EAGAIN) {
        ILIAS_TRACE("Epoll", "{} connect to {} failed with {}", descriptor->fd, endpoint, SystemError::fromErrno());
        co_return Unexpected(SystemError::fromErrno());
    }
    auto pollRet = co_await poll(descriptor, EPOLLOUT);
    ILIAS_TRACE("Epoll", "{} connect {} {}", descriptor->fd, endpoint,
                pollRet.has_value() ? std::string("successful") : pollRet.error().message());
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
    ILIAS_TRACE("Epoll", "start accept on fd {}", descriptor->fd);
    ILIAS_ASSERT(descriptor != nullptr);
    ILIAS_ASSERT(descriptor->type == IoDescriptor::Socket);
    ILIAS_ASSERT(descriptor->fd != -1);
    auto socket = SocketView(descriptor->fd);
    while (true) {
        auto ret = socket.accept<socket_t>();
        if (ret.has_value()) {
            if (remoteEndpoint != nullptr) {
                *remoteEndpoint = ret.value().second;
            }
            co_return ret.value().first;
        }
        if (ret.error() != SystemError(EAGAIN) && ret.error() != SystemError(EWOULDBLOCK)) {
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
    ILIAS_TRACE("Epoll", "start sendto on fd {}", descriptor->fd);
    ILIAS_ASSERT(descriptor != nullptr);
    ILIAS_ASSERT(descriptor->type == IoDescriptor::Socket);
    ILIAS_ASSERT(descriptor->fd != -1);
    SocketView socket(descriptor->fd);
    while (true) {
        Result<size_t> ret;
        if (endpoint != nullptr) {
            ret = socket.sendto(buffer, flags | MSG_DONTWAIT, *endpoint);
        }
        else {
            ret = socket.send(buffer, flags | MSG_DONTWAIT);
        }
        if (ret.has_value()) {
            co_return ret;
        }
        else if (ret.error() != SystemError(EINTR) && ret.error() != SystemError(EAGAIN) &&
                 ret.error() != SystemError(EWOULDBLOCK)) {
            co_return ret;
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
    ILIAS_TRACE("Epoll", "start recvfrom on fd {}", descriptor->fd);
    ILIAS_ASSERT(descriptor != nullptr);
    ILIAS_ASSERT(descriptor->type == IoDescriptor::Socket);
    ILIAS_ASSERT(descriptor->fd != -1);
    SocketView socket(descriptor->fd);
    while (true) {
        Result<size_t> ret;
        ret = socket.recvfrom(buffer, flags | MSG_DONTWAIT, endpoint);
        if (ret.has_value()) {
            co_return ret;
        }
        else if (ret.error() != SystemError(EINTR) && ret.error() != SystemError(EAGAIN) &&
                 ret.error() != SystemError(EWOULDBLOCK)) {
            co_return ret;
        }
        auto pollRet = co_await poll(descriptor, EPOLLIN);
        if (!pollRet && pollRet.error() != SystemError(EINTR) && pollRet.error() != SystemError(EAGAIN)) {
            co_return Unexpected(pollRet.error());
        }
    }
    co_return Unexpected(Error::Unknown);
}
// ----------------------------------------------------------------------------------------------------------------------
/**
 * @brief wait a event for a descriptor
 * All events supported by epoll are suspended via this function.
 * if the descriptor is no event, the function will add the fd in the epoll, and remove it when all the event in this
 * descript is triggered. please not make a fd to construct multiple descriptor
 * @param fd
 * @param event
 * @return Task<uint32_t>
 */
inline auto EpollContext::poll(IoDescriptor *fd, uint32_t event) -> Task<uint32_t> {
    auto descriptor = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_ASSERT(descriptor != nullptr);
    ILIAS_TRACE("Epoll", "fd {} poll events {}", descriptor->fd, detail::toString(event));
    // Build event queues based on different events
    ::std::unordered_map<uint32_t, ::std::list<detail::EpollEvent>>::iterator eventsIt = descriptor->events.find(event);
    if (eventsIt == descriptor->events.end()) {
        eventsIt = descriptor->events.emplace_hint(descriptor->events.begin(), event, std::list<detail::EpollEvent> {});
    }
    // Add event to descriptor's event queue
    auto &epollevent   = eventsIt->second.emplace_back(detail::EpollEvent {descriptor->fd, mEpollFd});
    auto  epolleventIt = --eventsIt->second.end();
    // if this is the first event, add the fd to the epoll
    if (descriptor->events.size() == 1 && eventsIt->second.size() == 1) {
        epoll_event event = {0};
        event.events      = EPOLLRDHUP;
        auto ret          = ::epoll_ctl(mEpollFd, EPOLL_CTL_ADD, descriptor->fd, &event);
        if (ret != 0) {
            co_return Unexpected<Error>(SystemError::fromErrno());
        }
    }
    // Build epoll listener event tags based on existing events
    uint32_t listenings_events = 0;
    for (auto &[key, value] : descriptor->events) {
        listenings_events |= key;
    }
    // Wait for events
    auto ret = co_await detail::EpollAwaiter(epollevent, listenings_events);
    ILIAS_TRACE("Epoll", "awaiter finished, return {}",
                ret.has_value() ? detail::toString(ret.value()) : ret.error().message());
    if (descriptor->isRemoved) {
        co_return ret; // descriptor has been removed, this descriptor while deleted by next event.
    }
    // remove event from descriptor's event queue
    eventsIt->second.erase(epolleventIt);
    if (eventsIt->second.empty()) {
        descriptor->events.erase(eventsIt);
    }
    // remove fd from epoll if no more events are listening
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
    ILIAS_TRACE("Epoll", "post task<{}> whit args<{}>", (void *)fn, args);
    PostTask task {fn, args};
    char     buf[sizeof(PostTask)] = {0};
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
        ILIAS_TRACE("Epoll", "run post task<{}> whit args<{}>", (void *)task.fn, (void *)task.args);
        task.fn(task.args);
    }
}

inline auto EpollContext::processEvents(int fd, uint32_t events) -> void {
    ILIAS_TRACE("Epoll", "Process events for fd: {}, events: {}", fd, detail::toString(events));
    auto descriptorItem = mDescriptors.find(fd);
    if (descriptorItem == mDescriptors.end()) {
        ILIAS_ERROR("Epoll", "Descriptor not found: {}", fd);
        epoll_event event;
        if (fd != -1)
            ::epoll_ctl(mEpollFd, EPOLL_CTL_DEL, fd, &event);
        return;
    }
    if ((events & EPOLLERR) || (events & EPOLLHUP)) {
        auto descriptorItem = mDescriptors.find(fd);
        if (descriptorItem == mDescriptors.end()) {
            return;
        }
        auto               &epollevents = descriptorItem->second->events;
        std::vector<void *> datas;
        for (auto &[event, epolleventawaiters] : epollevents) {
            for (auto &epolleventawaiter : epolleventawaiters) {
                datas.push_back(epolleventawaiter.data);
            }
        }
        for (auto data : datas) {
            detail::EpollAwaiter::onCompletion(events, data);
        }
    }
    if (events & EPOLLIN) {
        auto descriptorItem = mDescriptors.find(fd);
        if (descriptorItem == mDescriptors.end()) {
            return;
        }
        auto &epollevents        = descriptorItem->second->events;
        auto  epolleventawaiters = epollevents.find(EPOLLIN);
        if (epolleventawaiters != epollevents.end()) {
            if (epolleventawaiters->second.size() > 0) {
                detail::EpollAwaiter::onCompletion(events, epolleventawaiters->second.begin()->data);
            }
        }
    }
    if (events & EPOLLOUT) {
        auto descriptorItem = mDescriptors.find(fd);
        if (descriptorItem == mDescriptors.end()) {
            return;
        }
        auto &epollevents        = descriptorItem->second->events;
        auto  epolleventawaiters = epollevents.find(EPOLLOUT);
        if (epolleventawaiters != epollevents.end()) {
            if (epolleventawaiters->second.size() > 0) {
                detail::EpollAwaiter::onCompletion(events, epolleventawaiters->second.begin()->data);
            }
        }
    }
}

inline auto EpollContext::readTty(IoDescriptor *fd, ::std::span<::std::byte> buffer) -> Task<size_t> {
    auto descriptor = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_ASSERT(descriptor != nullptr);
    ILIAS_ASSERT(descriptor->type == detail::EpollDescriptor::Tty);
    if (!descriptor->isPollable) {
        // not supported operation
        co_return Unexpected(Error::OperationNotSupported);
    }
    ILIAS_ASSERT(descriptor->type != detail::EpollDescriptor::Unknown);
    while (true) {
        auto pollRet = co_await poll(descriptor, EPOLLIN);
        if (!pollRet && pollRet.error() != SystemError(EINTR) && pollRet.error() != SystemError(EAGAIN)) {
            co_return Unexpected(pollRet.error());
        }
        int ret = 0;
        ret     = ::read(descriptor->fd, buffer.data(), buffer.size());
        if (ret >= 0) {
            co_return ret;
        }
        else if (ret < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            co_return Unexpected(SystemError::fromErrno());
        }
    }
    co_return Unexpected(Error::Unknown);
}

ILIAS_NS_END