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
#include <ilias/buffer.hpp>
#include <ilias/log.hpp>

#include <unordered_map>
#include <algorithm>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <unistd.h>
#include <thread>
#include <deque>
#include <mutex>
#include <list>
#include <set>
#include <bit>


#if __has_include(<aio.h>)
#include <ilias/platform/detail/aio_core.hpp> //< For normal file
#endif

ILIAS_NS_BEGIN

namespace detail {


class EpollAwaiter;

/**
 * @brief The Epoll descriptor, if alloc is too frequently, maybe we can use memory pool
 *
 */
class EpollDescriptor final : public IoDescriptor {
public:
    int                fd         = -1;
    int                epollFd    = -1;
    IoDescriptor::Type type       = Unknown;
    bool               pollable   = false;

    // Poll Status
    std::list<EpollAwaiter *> awaiters;
    uint32_t                  events = 0; // Current all combined events
};

[[maybe_unused]]
inline std::string epollToString(uint32_t events) {
    std::string ret;
    if (events & EPOLLIN) {
        ret += ret.empty() ? "" : " | ";
        ret += "EPOLLIN";
    }
    if (events & EPOLLOUT) {
        ret += ret.empty() ? "" : " | ";
        ret += "EPOLLOUT";
    }
    if (events & EPOLLRDHUP) {
        ret += ret.empty() ? "" : " | ";
        ret += "EPOLLRDHUP";
    }
    if (events & EPOLLERR) {
        ret += ret.empty() ? "" : " | ";
        ret += "EPOLLERR";
    }
    if (events & EPOLLHUP) {
        ret += ret.empty() ? "" : " | ";
        ret += "EPOLLHUP";
    }
    if (events & EPOLLET) {
        ret += ret.empty() ? "" : " | ";
        ret += "EPOLLET";
    }
    if (events & EPOLLONESHOT) {
        ret += ret.empty() ? "" : " | ";
        ret += "EPOLLONESHOT";
    }
    if (events & EPOLLWAKEUP) {
        ret += ret.empty() ? "" : " | ";
        ret += "EPOLLWAKEUP";
    }
    if (events & EPOLLEXCLUSIVE) {
        ret += ret.empty() ? "" : " | ";
        ret += "EPOLLEXCLUSIVE";
    }
    if (ret.empty()) {
        ret = "None";
    }
    return ret;
}

/**
 * @brief poll for Epoll Awatier.
 *
 */
class EpollAwaiter {
public:
    EpollAwaiter(EpollDescriptor *fd, uint32_t events) : mFd(fd), mEvents(events) { }

    auto await_ready() -> bool;
    auto await_suspend(TaskView<> caller) -> void;
    auto await_resume() -> Result<uint32_t>;

    auto onNotify(Result<uint32_t> revents) -> void;
    auto events() const -> uint32_t;
    auto _trace(CoroHandle caller) -> void;
private:
    auto onCancel() -> void;

    EpollDescriptor                *mFd = nullptr;
    Result<uint32_t>                mResult; //< The result of the awaiter
    uint32_t                        mEvents  = 0; //< Events to wait for
    TaskView<>                      mCaller;
    CancellationToken::Registration mRegistration;
    std::list<EpollAwaiter *>::iterator mIt;
};

inline auto EpollAwaiter::await_ready() -> bool {
    mIt = mFd->awaiters.end(); // Mark it
    if ((mFd->events & mEvents) == mEvents) { // The registered events are contains current events, no need to register on epoll
        return false;
    }
    // Do register
    ::epoll_event modevent;
    modevent.data.ptr = mFd;
    modevent.events   = mEvents | mFd->events | EPOLLONESHOT;
    if (::epoll_ctl(mFd->epollFd, EPOLL_CTL_MOD, mFd->fd, &modevent) == -1) {
        mResult = Unexpected(SystemError::fromErrno());
        return true;
    }
    mFd->events |= mEvents;
    ILIAS_TRACE("Epoll", "Modify epoll event for fd: {}, events: {}", mFd->fd, epollToString(mFd->events | EPOLLONESHOT));
    return false;
}

inline auto EpollAwaiter::await_suspend(TaskView<> caller) -> void {
    mIt           = mFd->awaiters.insert(mFd->awaiters.end(), this);
    mCaller       = caller;
    mRegistration = caller.cancellationToken().register_([this]() { onCancel(); });
}

inline auto EpollAwaiter::await_resume() -> Result<uint32_t> {
    return mResult;
}

inline auto EpollAwaiter::onNotify(Result<uint32_t> revents) -> void {
    if (mIt == mFd->awaiters.end()) { // Already Got Event or Canceled
        return;
    }
    mIt = mFd->awaiters.end();
    mResult = revents;
    mCaller.schedule();
}

inline auto EpollAwaiter::events() const -> uint32_t {
    return mEvents;
}

inline auto EpollAwaiter::onCancel() -> void {
    if (mIt == mFd->awaiters.end()) { // Already Got Event or Canceled
        return;
    }
    mFd->awaiters.erase(mIt);
    mIt = mFd->awaiters.end();
    mResult = Unexpected(Error::Canceled);
    mCaller.schedule();
}

#if defined(ILIAS_TASK_TRACE)
inline auto EpollAwaiter::_trace(CoroHandle caller) -> void {
    caller.frame().msg = fmtlib::format("poll fd: {}, events: {}", mFd->fd, epollToString(mEvents));
}
#endif // defined(ILIAS_TASK_TRACE)

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
    ///> @brief Cancel all pending Io operations on a descriptor
    auto cancel(IoDescriptor *fd) -> Result<void> override;

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
    auto processCompletion(CancellationToken &) -> void;
    auto processEvents(std::span<const epoll_event> events) -> void;
    auto pollCallbacks() -> void;
    auto readTty(IoDescriptor *fd, ::std::span<::std::byte> buffer) -> IoTask<size_t>;

    ///> @brief The epoll file descriptor
    SockInitializer                                      mInit;
    int                                                  mEpollFd = -1;
    int                                                  mEventFd = -1; // For wakeup the epoll, there is some new callback in the queue
    detail::TimerService                                 mService;
    std::deque<Callback>                                 mCallbacks; // The callbacks in current thread, non mutex
    std::deque<Callback>                                 mPendingCallbacks; // The callbacks from another thread, protected by mMutex
    std::mutex                                           mMutex;
    std::thread::id                                      mThreadId { std::this_thread::get_id() };
};

inline EpollContext::EpollContext() {
    mEpollFd = ::epoll_create1(EPOLL_CLOEXEC);
    if (mEpollFd == -1) {
        ILIAS_WARN("Epoll", "Failed to create epoll file descriptor");
        ILIAS_ASSERT(false);
    }
    mEventFd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (mEventFd == -1) {
        ILIAS_WARN("Epoll", "Failed to create eventfd file descriptor");
        ILIAS_ASSERT(false);
    }
    epoll_event event;
    event.events = EPOLLIN;
    event.data.ptr = nullptr; // Special ptr, mark eventfd
    if (::epoll_ctl(mEpollFd, EPOLL_CTL_ADD, mEventFd, &event) == -1) {
        ILIAS_WARN("Epoll", "Failed to add eventfd to epoll");
        ILIAS_ASSERT(false);
    }
}

inline EpollContext::~EpollContext() {
    ::close(mEpollFd);
    ::close(mEventFd);
}

inline auto EpollContext::addDescriptor(fd_t fd, IoDescriptor::Type type) -> Result<IoDescriptor *> {
    if (fd < 0) {
        ILIAS_WARN("Epoll", "Invalid file descriptor {}", fd);
        return Unexpected(Error::InvalidArgument);
    }
    if (type == IoDescriptor::Unknown || type == IoDescriptor::Tty) { // If user give us a tty, it may redirect to something else, check it
        auto res = fd_utils::type(fd);
        if (!res) {
            ILIAS_WARN("Epoll", "Failed to get file descriptor type {}", res.error());
            return Unexpected(res.error());
        }
        type = res.value();
    }

    auto nfd        = std::make_unique<detail::EpollDescriptor>();
    nfd->fd         = fd;
    nfd->epollFd    = mEpollFd;
    nfd->type       = type;
    nfd->pollable   = false;

    ILIAS_TRACE("Epoll", "Created new fd descriptor: {}, type: {}", fd, type);

    if (type == IoDescriptor::Pipe || type == IoDescriptor::Tty || type == IoDescriptor::Socket) {
        nfd->pollable = true;
        epoll_event event;
        event.events = 0 | EPOLLONESHOT; // Just do simple register
        event.data.ptr = nfd.get();
        if (::epoll_ctl(mEpollFd, EPOLL_CTL_ADD, fd, &event) == -1) {
            ILIAS_ERROR("Epoll", "Failed to add fd {} to epoll: {}", fd, strerror(errno));
            return Unexpected(SystemError::fromErrno());
        }    
    }
    int  flags = ::fcntl(fd, F_GETFL, 0);
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK | O_CLOEXEC) == -1) {
        ILIAS_WARN("Epoll", "Failed to set descriptor to non-blocking. error: {}", SystemError::fromErrno());
    }
    return nfd.release();
}

inline auto EpollContext::removeDescriptor(IoDescriptor *fd) -> Result<void> {
    auto nfd = static_cast<detail::EpollDescriptor *>(fd);
    cancel(nfd);
    if (nfd->pollable) {
        if (::epoll_ctl(mEpollFd, EPOLL_CTL_DEL, nfd->fd, nullptr) == -1) {
            ILIAS_ERROR("Epoll", "Failed to remove fd {} from epoll: {}", nfd->fd, SystemError::fromErrno());
        }
    }
    delete nfd;
    return {};
}

inline auto EpollContext::cancel(IoDescriptor *fd) -> Result<void> {
    auto nfd = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_ASSERT(nfd != nullptr);
    ILIAS_TRACE("Epoll", "Cancel fd {} all pending operations for {}", nfd->fd, nfd->awaiters.size());
    if (nfd->pollable) { // if descriptor is pollable, cancel all poll awaiter
        for (auto &awaiter : nfd->awaiters) {
            awaiter->onNotify(Unexpected(Error::Canceled));
        }
        nfd->awaiters.clear();
    }
    return {};
}


inline auto EpollContext::post(void (*fn)(void *), void *args) -> void {
    ILIAS_ASSERT(fn != nullptr);
    ILIAS_TRACE("Epoll", "Post callback {} with args {}", (void *)fn, args);
    Callback callback {fn, args};
    if (std::this_thread::get_id() == mThreadId) { // Same thread, just push to the queue
        mCallbacks.emplace_back(callback);
        return;
    }
    // Different thread, push to the queue and wakeup the epoll
    {
        std::lock_guard locker {mMutex};
        mPendingCallbacks.emplace_back(callback);
    }
    uint64_t data = 1; // Wakeup epoll
    if (::write(mEventFd, &data, sizeof(data)) != sizeof(data)) {
        // ? Why write failed?
        ILIAS_WARN("Epoll", "Failed to write to event fd: {}", SystemError::fromErrno());
    }
}

inline auto EpollContext::run(CancellationToken &token) -> void {
    while (!token.isCancellationRequested()) {
        mService.updateTimers();
        processCompletion(token);
    }
}

inline auto EpollContext::sleep(uint64_t ms) -> IoTask<void> {
    return mService.sleep(ms);
}

inline auto EpollContext::processCompletion(CancellationToken &token) -> void {
    while (!mCallbacks.empty()) { // Process all callbacks in the current thread queue
        auto cb = mCallbacks.front();
        mCallbacks.pop_front();
        cb.fn(cb.args);
        mService.updateTimers(); // Update timers after each callback, TODO: Make an better way
    }
    // No callbacks available and non exit requested, process epoll events
    if (token.isCancellationRequested()) {
        return;
    }
    // Time to wait
    std::array<epoll_event, 64> events;
    std::span view { events };
    int timeout = -1; // Wait forever        
    if (auto nextTimepoint = mService.nextTimepoint(); nextTimepoint) {
        auto diffRaw = *nextTimepoint - ::std::chrono::steady_clock::now();
        auto diffMs  = ::std::chrono::duration_cast<::std::chrono::milliseconds>(diffRaw).count();
        timeout = ::std::clamp<int64_t>(diffMs, 0, ::std::numeric_limits<int>::max() - 1);
    }
    if (auto res = ::epoll_wait(mEpollFd, view.data(), view.size(), timeout); res > 0) { // Got any events
        processEvents(view.subspan(0, res));
    }
}

inline auto EpollContext::pollCallbacks() -> void {
    std::lock_guard locker {mMutex};
    ILIAS_TRACE("Epoll", "Polling {} callbacks from different thread queue", mPendingCallbacks.size());
    mCallbacks.insert(mCallbacks.end(), mPendingCallbacks.begin(), mPendingCallbacks.end());
    mPendingCallbacks.clear();
    uint64_t data = 0; // Reset wakeup flag
    if (::read(mEventFd, &data, sizeof(data)) != sizeof(data)) {
        // ? Why read failed?
        ILIAS_WARN("Epoll", "Failed to read from event fd: {}", SystemError::fromErrno());
    }
}

inline auto EpollContext::processEvents(std::span<const epoll_event> eventsArray) -> void {
    for (const auto &item : eventsArray) {
        auto events = item.events;
        auto ptr = item.data.ptr;
        if (ptr == nullptr) { // From the event fd, wakeup epoll and poll callbacks
            pollCallbacks();
            continue;
        }

        // Normal descriptor, dispatch to the awaiters
        auto nfd = static_cast<detail::EpollDescriptor *>(ptr);
        ILIAS_TRACE("Epoll", "Got epoll event for fd: {}, events: {}", nfd->fd, detail::epollToString(events));
        uint32_t newEvents = 0; // New interested events
        for (auto it = nfd->awaiters.begin(); it != nfd->awaiters.end();) {
            auto awaiter = *it;
            bool isInterested = awaiter->events() & events;
            bool isErrorOrHup = events & EPOLLERR || events & EPOLLHUP;
            bool shouldNotify = isInterested || isErrorOrHup; // Notify if interested or error or hangup

            if (shouldNotify) {
                awaiter->onNotify(events);
                it = nfd->awaiters.erase(it);    
            }
            else {
                newEvents |= awaiter->events(); // Collect the new interested events
                ++it;
            }
        }

        // Update the events we still interested
        nfd->events = newEvents;
        if (nfd->events == 0) { // No more interested events
            ILIAS_ASSERT(nfd->awaiters.empty()); // No more interested events, no more awaiters
            ILIAS_TRACE("Epoll", "Fd {} no more interested events", nfd->fd);
            continue; // Because oneshot, we don't need to modify the epoll event, just do nothing
        }

        // Modify the descriptor events
        epoll_event modevent;
        modevent.events = nfd->events | EPOLLONESHOT; // Just one shot
        modevent.data.ptr = nfd;
        if (::epoll_ctl(mEpollFd, EPOLL_CTL_MOD, nfd->fd, &modevent) == -1) {
            // Notify all pending awaiters
            ILIAS_WARN("Epoll", "Failed to modify fd {} epoll mode: {}", nfd->fd, SystemError::fromErrno());
            nfd->events = 0;
            auto error = SystemError::fromErrno();
            for (auto &awaiter : nfd->awaiters) {
                awaiter->onNotify(Unexpected(error));
            }
            nfd->awaiters.clear();
        }
        else {
            ILIAS_TRACE("Epoll", "Modify epoll event for fd: {}, events: {}", nfd->fd, detail::epollToString(nfd->events | EPOLLONESHOT));            
        }
    }
}

inline auto EpollContext::read(IoDescriptor *fd, ::std::span<::std::byte> buffer, ::std::optional<size_t> offset)
    -> IoTask<size_t> {
    auto nfd = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_ASSERT(nfd != nullptr);
    if (!nfd->pollable) {
        // Not supported operation when aio unavailable
#if !__has_include(<aio.h>)
        co_return Unexpected(Error::OperationNotSupported);
#endif

    }
    ILIAS_ASSERT(nfd->type != IoDescriptor::Unknown);
    if (nfd->type == IoDescriptor::Tty) {
        co_return co_await readTty(nfd, buffer);
    }
    while (true) {
#if __has_include(<aio.h>)
        if (!nfd->pollable) { // Use POSIX AIO handle it
            co_return co_await detail::AioReadAwaiter {nfd->fd, buffer, offset};
        }
#endif
        int ret = 0;
        if (offset.has_value()) {
            ret = ::pread(nfd->fd, buffer.data(), buffer.size(), offset.value_or(0));
        }
        else {
            ret = ::read(nfd->fd, buffer.data(), buffer.size());
        }
        if (ret >= 0) {
            co_return ret;
        }
        else if (auto err = errno; err != EINTR && err != EAGAIN && err != EWOULDBLOCK) {
            co_return Unexpected(SystemError::fromErrno());
        }
        auto pollRet = co_await poll(nfd, EPOLLIN);
        if (!pollRet && pollRet.error() != SystemError(EINTR) && pollRet.error() != SystemError(EAGAIN)) {
            co_return Unexpected(pollRet.error());
        }
    }
    co_return Unexpected(Error::Unknown);
}

inline auto EpollContext::write(IoDescriptor *fd, ::std::span<const ::std::byte> buffer, ::std::optional<size_t> offset)
    -> IoTask<size_t> {
    auto nfd = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_TRACE("Epoll", "start write {} bytes on fd {}", buffer.size(), nfd->fd);
    ILIAS_ASSERT(nfd != nullptr);
    if (!nfd->pollable) {
        // Not supported operation when aio unavailable
#if !__has_include(<aio.h>)
        co_return Unexpected(Error::OperationNotSupported);
#endif

    }
    ILIAS_ASSERT(nfd->type != IoDescriptor::Unknown);
    while (true) {
#if __has_include(<aio.h>)
        if (!nfd->pollable) { // Use POSIX AIO handle it
            co_return co_await detail::AioWriteAwaiter {nfd->fd, buffer, offset};
        }
#endif
        int ret = 0;
        if (offset.has_value()) {
            ILIAS_ASSERT(nfd->type == IoDescriptor::File);
            ret = ::pwrite(nfd->fd, buffer.data(), buffer.size(), offset.value_or(0));
        }
        else {
            ret = ::write(nfd->fd, buffer.data(), buffer.size());
        }
        if (ret >= 0) {
            co_return ret;
        }
        else if (auto err = errno; err != EINTR && err != EAGAIN && err != EWOULDBLOCK) {
            co_return Unexpected(SystemError::fromErrno());
        }
        auto pollRet = co_await poll(nfd, EPOLLOUT);
        if (!pollRet && pollRet.error() != SystemError(EINTR) && pollRet.error() != SystemError(EAGAIN)) {
            co_return Unexpected(pollRet.error());
        }
    }
    co_return Unexpected(Error::Unknown);
}

inline auto EpollContext::connect(IoDescriptor *fd, EndpointView endpoint) -> IoTask<void> {
    auto nfd = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_ASSERT(nfd != nullptr);
    ILIAS_ASSERT(nfd->type == IoDescriptor::Socket);
    ILIAS_TRACE("Epoll", "Start connect to {} on fd {}", endpoint, nfd->fd);
    if (::connect(nfd->fd, endpoint.data(), endpoint.length()) == 0) {
        ILIAS_TRACE("Epoll", "{} connect to {} successful", nfd->fd, endpoint);
        co_return {};
    }
    else if (errno != EINPROGRESS && errno != EAGAIN) {
        ILIAS_TRACE("Epoll", "{} connect to {} failed with {}", nfd->fd, endpoint, SystemError::fromErrno());
        co_return Unexpected(SystemError::fromErrno());
    }
    if (auto pollRet = co_await poll(nfd, EPOLLOUT); !pollRet) {
        co_return Unexpected(pollRet.error());
    }
    int       sockErr    = 0;
    socklen_t sockErrLen = sizeof(sockErr);
    if (::getsockopt(nfd->fd, SOL_SOCKET, SO_ERROR, &sockErr, &sockErrLen) == -1) {
        co_return Unexpected(SystemError::fromErrno());
    }
    if (sockErr != 0) {
        ILIAS_TRACE("Epoll", "{} connect to {} failed with {}", nfd->fd, endpoint, SystemError(sockErr));
        co_return Unexpected(SystemError(sockErr));
    }
    ILIAS_TRACE("Epoll", "{} connect to {} successful", nfd->fd, endpoint);
    co_return {};
}

inline auto EpollContext::accept(IoDescriptor *fd, MutableEndpointView remoteEndpoint) -> IoTask<socket_t> {
    auto nfd = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_ASSERT(nfd != nullptr);
    ILIAS_ASSERT(nfd->type == IoDescriptor::Socket);
    ILIAS_TRACE("Epoll", "Start accept on fd {}", nfd->fd);
    auto socket = SocketView(nfd->fd);
    while (true) {
        if (auto ret = socket.accept<socket_t>(remoteEndpoint); ret) {
            co_return ret;
        }
        else if (ret.error() != SystemError(EAGAIN) && ret.error() != SystemError(EWOULDBLOCK)) {
            co_return Unexpected(SystemError::fromErrno());
        }
        if (auto pollRet = co_await poll(fd, EPOLLIN); !pollRet) {
            co_return Unexpected(pollRet.error());
        }
    }
    co_return Unexpected(Error::Unknown);
}

inline auto EpollContext::sendto(IoDescriptor *fd, ::std::span<const ::std::byte> buffer, int flags,
                                 EndpointView endpoint) -> IoTask<size_t> {
    auto nfd = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_ASSERT(nfd != nullptr);
    ILIAS_ASSERT(nfd->type == IoDescriptor::Socket);
    ILIAS_TRACE("Epoll", "Start sendto on fd {}", nfd->fd);
    SocketView socket(nfd->fd);
    while (true) {
        if (auto ret = socket.sendto(buffer, flags | MSG_DONTWAIT | MSG_NOSIGNAL, endpoint); ret) {
            co_return ret;
        }
        else if (ret.error() != SystemError(EINTR) && ret.error() != SystemError(EAGAIN) &&
                 ret.error() != SystemError(EWOULDBLOCK)) {
            co_return ret;
        }
        if (auto pollRet = co_await poll(nfd, EPOLLOUT); !pollRet) {
            co_return Unexpected(pollRet.error());
        }
    }
    co_return Unexpected(Error::Unknown);
}

inline auto EpollContext::recvfrom(IoDescriptor *fd, ::std::span<::std::byte> buffer, int flags,
                                   MutableEndpointView endpoint) -> IoTask<size_t> {
    auto nfd = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_ASSERT(nfd != nullptr);
    ILIAS_ASSERT(nfd->type == IoDescriptor::Socket);
    ILIAS_TRACE("Epoll", "Start recvfrom on fd {}", nfd->fd);
    SocketView socket(nfd->fd);
    while (true) {
        if (auto ret = socket.recvfrom(buffer, flags | MSG_DONTWAIT | MSG_NOSIGNAL, endpoint); ret) {
            co_return ret;
        }
        else if (ret.error() != SystemError(EINTR) && ret.error() != SystemError(EAGAIN) &&
                 ret.error() != SystemError(EWOULDBLOCK)) {
            co_return ret;
        }
        if (auto pollRet = co_await poll(nfd, EPOLLIN); !pollRet) {
            co_return Unexpected(pollRet.error());
        }
    }
    co_return Unexpected(Error::Unknown);
}

inline auto EpollContext::sendmsg(IoDescriptor *fd, const MsgHdr &msg, int flags) -> IoTask<size_t> {
    auto nfd = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_ASSERT(nfd != nullptr);
    while (true) {
        if (auto ret = ::sendmsg(nfd->fd, &msg, flags | MSG_DONTWAIT | MSG_NOSIGNAL); ret >= 0) {
            co_return ret;
        }
        if (auto err = errno; err != EINTR && err != EAGAIN && err != EWOULDBLOCK) {
            co_return Unexpected(SystemError(err));
        }
        if (auto pollRet = co_await poll(nfd, EPOLLOUT); !pollRet) {
            co_return Unexpected(pollRet.error());
        }
    }
}

inline auto EpollContext::recvmsg(IoDescriptor *fd, MsgHdr &msg, int flags) -> IoTask<size_t> {
    auto nfd = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_ASSERT(nfd != nullptr);
    while (true) {
        if (auto ret = ::recvmsg(nfd->fd, &msg, flags | MSG_DONTWAIT | MSG_NOSIGNAL); ret >= 0) {
            co_return ret;
        }
        if (auto err = errno; err != EINTR && err != EAGAIN && err != EWOULDBLOCK) {
            co_return Unexpected(SystemError(err));
        }
        if (auto pollRet = co_await poll(nfd, EPOLLIN); !pollRet) {
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
 * @param events
 * @return IoTask<uint32_t>
 */
inline auto EpollContext::poll(IoDescriptor *fd, uint32_t events) -> IoTask<uint32_t> {
    auto nfd = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_ASSERT(nfd != nullptr);
    if (!nfd->pollable) {
        co_return Unexpected(Error::OperationNotSupported);
    }
    co_return co_await detail::EpollAwaiter {nfd, events};
}

inline auto EpollContext::readTty(IoDescriptor *fd, ::std::span<::std::byte> buffer) -> IoTask<size_t> {
    auto nfd = static_cast<detail::EpollDescriptor *>(fd);
    ILIAS_ASSERT(nfd != nullptr);
    ILIAS_ASSERT(nfd->type == IoDescriptor::Tty);
    while (true) {
        if (auto ret = co_await poll(nfd, EPOLLIN); !ret) {
            co_return Unexpected(ret.error());
        }
        
        if (auto ret = ::read(nfd->fd, buffer.data(), buffer.size()); ret >= 0) {
            co_return ret;
        }
        else if (auto err = errno; err != EINTR && err != EAGAIN && err != EWOULDBLOCK) {
            co_return Unexpected(SystemError::fromErrno());
        }
    }
}

ILIAS_NS_END