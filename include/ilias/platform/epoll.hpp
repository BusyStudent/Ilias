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
#include <list>
#include <algorithm>
#include <sys/stat.h>
#include <unistd.h>
#include <set>
#include <deque>

#include <ilias/platform/detail/epoll_event.hpp>
#include <ilias/cancellation_token.hpp>
#include <ilias/detail/timer.hpp>
#include <ilias/task/executor.hpp>
#include <ilias/task/task.hpp>
#include <ilias/net/endpoint.hpp>
#include <ilias/net/system.hpp>
#include <ilias/net/sockfd.hpp>
#include <ilias/net/msg.hpp>
#include <ilias/io/fd_utils.hpp>
#include <ilias/io/context.hpp>
#include <ilias/log.hpp>
#include <ilias/buffer.hpp>

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

    // For Socket data
    struct {
        int family   = 0;
        int stype    = 0;
        int protocol = 0;
    };

    struct EventReference {
        uint32_t              events;
        detail::EpollAwaiter *awaiter = nullptr;
    };

    ::std::unordered_map<uint32_t, ::std::list<detail::EpollAwaiter *>> awaiterMap;

    auto makeEventReference(uint32_t events, detail::EpollAwaiter *awaiter) -> EventReference;
    auto countEvents(uint32_t events) const -> size_t;
    auto removeEvent(const EventReference &ref) -> void;
    auto removeEvents(uint32_t events) -> void;
    auto events() const -> uint32_t;
};

inline auto EpollDescriptor::makeEventReference(uint32_t events, detail::EpollAwaiter *awaiter) -> EventReference {
    auto it = awaiterMap.find(events);
    if (it == awaiterMap.end()) {
        it = awaiterMap.emplace(events, ::std::list<detail::EpollAwaiter *>()).first;
    }

    auto iter = it->second.emplace(it->second.end(), awaiter);
    return {events, awaiter};
}

inline auto EpollDescriptor::countEvents(uint32_t events) const -> size_t {
    auto it = awaiterMap.find(events);
    if (it != awaiterMap.end()) {
        return it->second.size();
    }
    else {
        return 0;
    }
}

inline auto EpollDescriptor::removeEvent(const EventReference &ref) -> void {
    auto it = awaiterMap.find(ref.events);
    if (it != awaiterMap.end()) {
        it->second.remove(ref.awaiter);
    }
    if (it->second.empty()) {
        awaiterMap.erase(it);
    }
}

inline auto EpollDescriptor::removeEvents(uint32_t events) -> void {
    awaiterMap.erase(events);
}

inline auto EpollDescriptor::events() const -> uint32_t {
    uint32_t events = 0;
    for (const auto &[key, value] : awaiterMap) {
        events |= key;
    }
    return events;
}

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
        -> IoTask<size_t> override;
    ///> @brief Write to a descriptor
    auto write(IoDescriptor *fd, ::std::span<const ::std::byte> buffer, ::std::optional<size_t> offset)
        -> IoTask<size_t> override;

    ///> @brief Connect to a remote endpoint
    auto connect(IoDescriptor *fd, EndpointView endpoint) -> IoTask<void> override;
    ///> @brief Accept a connection
    auto accept(IoDescriptor *fd, MutableEndpointView remoteEndpoint) -> IoTask<socket_t> override;

    ///> @brief Send data to a remote endpoint
    auto sendto(IoDescriptor *fd, ::std::span<const ::std::byte> buffer, int flags, EndpointView endpoint)
        -> IoTask<size_t> override;
    ///> @brief Receive data from a remote endpoint
    auto recvfrom(IoDescriptor *fd, ::std::span<::std::byte> buffer, int flags, MutableEndpointView endpoint)
        -> IoTask<size_t> override;

    ///> @brief Send a message to a descriptor
    auto sendmsg(IoDescriptor *fd, const MsgHdr &msg, int flags) -> IoTask<size_t> override;
    ///> @brief Receive a message from a descriptor
    auto recvmsg(IoDescriptor *fd, MsgHdr &msg, int flags) -> IoTask<size_t> override;    

    ///> @brief Poll a descriptor for events
    auto poll(IoDescriptor *fd, uint32_t event) -> IoTask<uint32_t> override;

    ///> @brief Post a callable to the executor
    auto post(void (*fn)(void *), void *args) -> void override;

    ///> @brief Enter and run the task in the executor, it will infinitely loop until the token is canceled
    auto run(CancellationToken &token) -> void override;

    ///> @brief Sleep for a specified amount of time
    auto sleep(uint64_t ms) -> IoTask<void> override;

private:
    struct Callback {
        void (*fn)(void *);
        void *args;
    };
    auto processCompletion(int timeout) -> void;
    auto processCallbacks() -> void;
    auto processEvents(int fd, uint32_t events) -> void;
    auto readTty(IoDescriptor *fd, ::std::span<::std::byte> buffer) -> IoTask<size_t>;

    ///> @brief The epoll file descriptor
    SockInitializer                                      mInit;
    int                                                  mEpollFd = -1;
    detail::TimerService                                 mService;
    int                                                  mTaskSender     = -1;
    int                                                  mTaskReceiver   = -1;
    int                                                  mCurrentEvent   = 0;
    int                                                  mPollEventCount = 0;
    ::std::unordered_map<int, detail::EpollDescriptor *> mDescriptors;
    constexpr static const int                           mMaxEvents = 64;
    std::array<epoll_event, mMaxEvents>                  mEvents;
};

inline EpollContext::EpollContext() {
    mEpollFd = epoll_create1(O_CLOEXEC);
    if (mEpollFd == -1) {
        ILIAS_WARN("Epoll", "Failed to create epoll file descriptor");
        ILIAS_ASSERT(false);
    }
    int fds[2];
    if (::pipe2(fds, O_CLOEXEC | O_NONBLOCK) == -1) {
        ILIAS_WARN("Epoll", "Failed to create pipe socket");
        ILIAS_ASSERT(false);
    }
    else {
        mTaskReceiver = fds[0];
        mTaskSender   = fds[1];
        epoll_event event = {};
        event.events      = EPOLLIN;
        event.data.fd     = mTaskReceiver;
        auto ret          = ::epoll_ctl(mEpollFd, EPOLL_CTL_ADD, mTaskReceiver, &event);
        if (ret == -1) {
            ILIAS_ERROR("Epoll", "Failed to add receiver to epoll. error: {}", errno);
            ILIAS_ASSERT(false);
        }
    }
    ILIAS_TRACE("Epoll", "Epoll context created, fd({}), make pipe({}, {})", mEpollFd, mTaskReceiver, mTaskSender);
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
    if (fd < 0) {
        ILIAS_WARN("Epoll", "Invalid file descriptor {}", fd);
        return Unexpected(Error::InvalidArgument);
    }
    if (type == IoDescriptor::Unknown) {
        auto res = fd_utils::type(fd);
        if (!res) {
            ILIAS_WARN("Epoll", "Failed to get file descriptor type {}", res.error());
            return Unexpected(res.error());
        }
        type = res.value();
    }

    auto nfd        = std::make_unique<detail::EpollDescriptor>();
    nfd->fd         = fd;
    nfd->type       = type;
    nfd->isPollable = false;

    if (type == IoDescriptor::Socket) {
        nfd->isPollable = true;
        // 获取socket的协议族
        socklen_t len = sizeof(nfd->family);
        if (::getsockopt(fd, SOL_SOCKET, SO_DOMAIN, &nfd->family, &len) == -1) {
            ILIAS_WARN("Epoll", "Failed to get socket domain. error: {}", SystemError::fromErrno());
        }
        len = sizeof(nfd->stype);
        if (::getsockopt(fd, SOL_SOCKET, SO_TYPE, &nfd->stype, &len) == -1) {
            ILIAS_WARN("Epoll", "Failed to get socket type. error: {}", SystemError::fromErrno());
        }
        len = sizeof(nfd->protocol);
        if (::getsockopt(fd, SOL_SOCKET, SO_PROTOCOL, &nfd->protocol, &len) == -1) {
            ILIAS_WARN("Epoll", "Failed to get socket protocol. error: {}", SystemError::fromErrno());
        }
    }
    ILIAS_TRACE("Epoll", "Created new fd descriptor: {}, type: {}", fd, type);

    if (type == IoDescriptor::Pipe || type == IoDescriptor::Tty) {
        nfd->isPollable = true;
    }
    int  flags = ::fcntl(fd, F_GETFL, 0);
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK | O_CLOEXEC) == -1) {
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
        auto &epollevents = descriptor->awaiterMap;
        for (auto &[event, epolleventawaiters] : epollevents) {
            for (auto &epolleventawaiter : epolleventawaiters) {
                detail::EpollAwaiter::onCancel(epolleventawaiter);
            }
        }
    }
    mDescriptors.erase(descriptor->fd);
    auto epollret = ::epoll_ctl(mEpollFd, EPOLL_CTL_DEL, descriptor->fd, nullptr); // remove from epoll control
    if (epollret != 0 && errno != ENOENT) {
        ILIAS_WARN("Epoll", "Failed to remove fd {} from epoll: {}", descriptor->fd, strerror(errno));
    }
    descriptor->isRemoved = true; // make removed flag, ask poll this descriptor is removed.
    if (descriptor->awaiterMap.size() == 0) {
        delete descriptor;
    }
    else {
        post(+[](void *descriptor) { delete static_cast<detail::EpollDescriptor *>(descriptor); }, descriptor);
    }
    if (ret != 0) {
        return Unexpected(SystemError(EALREADY));
    }
    return {};
}

inline auto EpollContext::read(IoDescriptor *fd, ::std::span<::std::byte> buffer, ::std::optional<size_t> offset)
    -> IoTask<size_t> {
    auto descriptor = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_ASSERT(descriptor != nullptr);
    if (!descriptor->isPollable) {
        // Not supported operation when aio unavailable
#if !__has_include(<aio.h>)
        co_return Unexpected(Error::OperationNotSupported);
#endif

    }
    ILIAS_ASSERT(descriptor->type != detail::EpollDescriptor::Unknown);
    if (descriptor->type == detail::EpollDescriptor::Tty) {
        co_return co_await readTty(descriptor, buffer);
    }
    while (true) {
#if __has_include(<aio.h>)
        if (!descriptor->isPollable) { // Use POSIX AIO handle it
            co_return co_await detail::AioReadAwaiter {descriptor->fd, buffer, offset};
        }
#endif
        int ret = 0;
        if (offset.has_value()) {
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
    -> IoTask<size_t> {
    auto descriptor = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_TRACE("Epoll", "start write {} bytes on fd {}", buffer.size(), descriptor->fd);
    ILIAS_ASSERT(descriptor != nullptr);
    if (!descriptor->isPollable) {
        // Not supported operation when aio unavailable
#if !__has_include(<aio.h>)
        co_return Unexpected(Error::OperationNotSupported);
#endif

    }
    ILIAS_ASSERT(descriptor->type != detail::EpollDescriptor::Unknown);
    while (true) {
#if __has_include(<aio.h>)
        if (!descriptor->isPollable) { // Use POSIX AIO handle it
            co_return co_await detail::AioWriteAwaiter {descriptor->fd, buffer, offset};
        }
#endif
        int ret = 0;
        if (offset.has_value()) {
            ILIAS_ASSERT(descriptor->type == detail::EpollDescriptor::File);
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

inline auto EpollContext::connect(IoDescriptor *fd, EndpointView endpoint) -> IoTask<void> {
    auto descriptor = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_ASSERT(descriptor != nullptr);
    ILIAS_ASSERT(descriptor->type == IoDescriptor::Socket);
    ILIAS_TRACE("Epoll", "start connect to {} on fd {}", endpoint, descriptor->fd);
    auto ret = ::connect(descriptor->fd, endpoint.data(), endpoint.length());
    if (ret == 0) {
        ILIAS_TRACE("Epoll", "{} connect to {} successful", descriptor->fd, endpoint);
        co_return {};
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
    co_return {};
}

inline auto EpollContext::accept(IoDescriptor *fd, MutableEndpointView remoteEndpoint) -> IoTask<socket_t> {
    auto descriptor = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_TRACE("Epoll", "start accept on fd {}", descriptor->fd);
    ILIAS_ASSERT(descriptor != nullptr);
    ILIAS_ASSERT(descriptor->type == IoDescriptor::Socket);
    ILIAS_ASSERT(descriptor->fd != -1);
    auto socket = SocketView(descriptor->fd);
    while (true) {
        auto ret = socket.accept<socket_t>(remoteEndpoint);
        if (ret.has_value()) {
            co_return ret;
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
                                 EndpointView endpoint) -> IoTask<size_t> {
    auto descriptor = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_TRACE("Epoll", "start sendto on fd {}", descriptor->fd);
    ILIAS_ASSERT(descriptor != nullptr);
    ILIAS_ASSERT(descriptor->type == IoDescriptor::Socket);
    ILIAS_ASSERT(descriptor->fd != -1);
    SocketView socket(descriptor->fd);
    while (true) {
        Result<size_t> ret;
        if (endpoint) {
            ret = socket.sendto(buffer, flags | MSG_DONTWAIT, endpoint);
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

inline auto EpollContext::recvfrom(IoDescriptor *fd, ::std::span<::std::byte> buffer, int flags,
                                   MutableEndpointView endpoint) -> IoTask<size_t> {
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

inline auto EpollContext::sendmsg(IoDescriptor *fd, const MsgHdr &msg, int flags) -> IoTask<size_t> {
    auto descriptor = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_ASSERT(descriptor != nullptr);
    while (true) {
        auto ret = ::sendmsg(descriptor->fd, &msg, flags | MSG_DONTWAIT);
        if (ret >= 0) {
            co_return ret;
        }
        if (auto err = errno; err != EINTR && err != EAGAIN && err != EWOULDBLOCK) {
            co_return Unexpected(SystemError(err));
        }
        auto pollRet = co_await poll(descriptor, EPOLLOUT);
        if (!pollRet && pollRet.error() != SystemError(EINTR) && pollRet.error() != SystemError(EAGAIN)) {
            co_return Unexpected(pollRet.error());
        }
    }
}

inline auto EpollContext::recvmsg(IoDescriptor *fd, MsgHdr &msg, int flags) -> IoTask<size_t> {
    auto descriptor = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_ASSERT(descriptor != nullptr);
    while (true) {
        auto ret = ::recvmsg(descriptor->fd, &msg, flags | MSG_DONTWAIT);
        if (ret >= 0) {
            co_return ret;
        }
        if (auto err = errno; err != EINTR && err != EAGAIN && err != EWOULDBLOCK) {
            co_return Unexpected(SystemError(err));
        }
        auto pollRet = co_await poll(descriptor, EPOLLIN);
        if (!pollRet && pollRet.error() != SystemError(EINTR) && pollRet.error() != SystemError(EAGAIN)) {
            co_return Unexpected(pollRet.error());
        }
    }
}

// ----------------------------------------------------------------------------------------------------------------------
/**
 * @brief wait a event for a descriptor
 * All events supported by epoll are suspended via this function.
 * if the descriptor is no event, the function will add the fd in the epoll, and remove it when all the event in this
 * descript is triggered. please not make a fd to construct multiple descriptor
 * @param fd
 * @param event
 * @return IoTask<uint32_t>
 */
inline auto EpollContext::poll(IoDescriptor *fd, uint32_t event) -> IoTask<uint32_t> {
    auto descriptor = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_ASSERT(descriptor != nullptr);
    ILIAS_TRACE("Epoll", "fd {} poll events {}", descriptor->fd, detail::toString(event));
    // make a epoll event struct and get the event reference
    auto awaiter   = detail::EpollAwaiter(descriptor->fd, mEpollFd, event);
    auto eventsref = descriptor->makeEventReference(event, &awaiter);
    // if this is the first event, add the fd to the epoll
    if (descriptor->awaiterMap.size() == 1 && descriptor->countEvents(event) == 1) {
        epoll_event event = {0};
        event.events      = EPOLLRDHUP;
        auto ret          = ::epoll_ctl(mEpollFd, EPOLL_CTL_ADD, descriptor->fd, &event);
        if (ret != 0) {
            co_return Unexpected<Error>(SystemError::fromErrno());
        }
    }
    // Build epoll listener event tags based on existing events
    uint32_t listenings_events = descriptor->events();
    // Wait for events
    awaiter.setEvents(listenings_events);
    auto ret = co_await awaiter;
    ILIAS_TRACE("Epoll", "awaiter finished, return {}",
                ret.has_value() ? detail::toString(ret.value()) : ret.error().message());
    if (descriptor->isRemoved) {
        co_return ret; // descriptor has been removed, this descriptor while deleted by next event.
    }
    // remove event from descriptor's event queue
    descriptor->removeEvent(eventsref);
    // remove fd from epoll if no more events are listening
    if (descriptor->awaiterMap.empty()) {
        auto ret = ::epoll_ctl(mEpollFd, EPOLL_CTL_DEL, descriptor->fd, nullptr);
        if (ret != 0) {
            ILIAS_WARN("Epoll", "Failed to remove fd {} from epoll: {}", descriptor->fd, strerror(errno));
        }
    }
    co_return ret;
}

inline auto EpollContext::post(void (*fn)(void *), void *args) -> void {
    ILIAS_ASSERT(fn != nullptr);
    ILIAS_TRACE("Epoll", "Post callback {} with args {}", (void *)fn, args);
    Callback callback {fn, args};
    // FIXME: this is a block call, if buffer is full and block main thread will be blocked
    auto ret = ::write(mTaskSender, &callback, sizeof(callback));
    if (ret != sizeof(callback)) {
        ILIAS_WARN("Epoll", "Failed to post callback to epoll. error: {}", SystemError::fromErrno());
        ILIAS_ASSERT(false);
    }
}

inline auto EpollContext::run(CancellationToken &token) -> void {
    int timeout = -1; // Wait forever
    while (!token.isCancellationRequested()) {
        auto nextTimepoint = mService.nextTimepoint();
        if (nextTimepoint) {
            auto diffRaw = *nextTimepoint - ::std::chrono::steady_clock::now();
            auto diffMs  = ::std::chrono::duration_cast<::std::chrono::milliseconds>(diffRaw).count();
            timeout      = ::std::clamp<int64_t>(diffMs, 0, ::std::numeric_limits<int>::max() - 1);
        }
        mService.updateTimers();
        processCompletion(timeout);
    }
}

inline auto EpollContext::sleep(uint64_t ms) -> IoTask<void> {
    co_return co_await mService.sleep(ms);
}

inline auto EpollContext::processCompletion(int timeout) -> void {
    if (mPollEventCount <= mCurrentEvent) {
        mCurrentEvent = 0;
        memset(mEvents.data(), 0, sizeof(epoll_event) * mEvents.size());
        mPollEventCount = ::epoll_wait(mEpollFd, mEvents.data(), mEvents.size(), timeout);
        if (mPollEventCount < 0 && errno != EINTR) {
            ILIAS_WARN("Epoll", "Failed to wait for events. error: {}", errno);
            ILIAS_ASSERT(false);
        }
    }
    else {
        while (mPollEventCount > mCurrentEvent) {
            auto event = mEvents[mCurrentEvent];
            mCurrentEvent++;
            if (event.data.fd == mTaskReceiver) {
                processCallbacks();
            }
            else {
                processEvents(event.data.fd, event.events);
            }
        }
    }
}

inline auto EpollContext::processCallbacks() -> void {
    while (true) {
        Callback callback;
        auto     readLen = ::read(mTaskReceiver, &callback, sizeof(callback));
        if (readLen != sizeof(callback)) {
            break;
        }
        ILIAS_TRACE("Epoll", "Run callback {} {}", (void *)callback.fn, (void *)callback.args);
        callback.fn(callback.args);
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
    if (descriptorItem->second->isRemoved) {
        return;
    }
    std::set<detail::EpollAwaiter *> awaiters;
    if ((events & EPOLLERR) || (events & EPOLLHUP)) {
        // 出现错误，唤起所有正在等待的任务
        auto &epollAwaiterMap = descriptorItem->second->awaiterMap;
        for (auto &[event, epollAwaiters] : epollAwaiterMap) {
            for (auto &epollAwaiter : epollAwaiters) {
                awaiters.insert(epollAwaiter);
            }
        }
    }
    if (events & EPOLLIN) {
        auto &epollAwaiterMap = descriptorItem->second->awaiterMap;
        auto  epollawaiters   = epollAwaiterMap.find(EPOLLIN);
        if (epollawaiters != epollAwaiterMap.end() && epollawaiters->second.size() > 0) {
            awaiters.insert(*epollawaiters->second.begin());
        }
    }
    if (events & EPOLLOUT) {
        auto &epollAwaiterMap = descriptorItem->second->awaiterMap;
        auto  epollAwaiters   = epollAwaiterMap.find(EPOLLOUT);
        if (epollAwaiters != epollAwaiterMap.end() && epollAwaiters->second.size() > 0) {
            awaiters.insert(*epollAwaiters->second.begin());
        }
    }
    if (awaiters.size() == 0) {
        ILIAS_ERROR("Epoll", "events {} has no fd wait.", events);
    }
    for (auto awaiter : awaiters) {
        detail::EpollAwaiter::onCompletion(events, awaiter);
    }
}

inline auto EpollContext::readTty(IoDescriptor *fd, ::std::span<::std::byte> buffer) -> IoTask<size_t> {
    auto descriptor = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_ASSERT(descriptor != nullptr);
    ILIAS_ASSERT(descriptor->type == detail::EpollDescriptor::Tty);
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