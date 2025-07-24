#include <ilias/platform/epoll.hpp>
#include <ilias/runtime/token.hpp>
#include <ilias/io/system_error.hpp>
#include <ilias/io/fd_utils.hpp>
#include <ilias/io/error.hpp>
#include <ilias/net/endpoint.hpp>
#include <ilias/net/sockfd.hpp>

#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <list>

#if __has_include(<aio.h>)
    #include "aio_core.hpp"
#endif // __has_include(<aio.h>)


ILIAS_NS_BEGIN

namespace linux {

class EpollAwaiter;
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

class EpollAwaiter {
public:
    EpollAwaiter(EpollDescriptor *fd, uint32_t events) : mFd(fd), mEvents(events) { }

    auto await_ready() -> bool;
    auto await_suspend(runtime::CoroHandle caller) -> void;
    auto await_resume() -> IoResult<uint32_t>;

    auto onNotify(IoResult<uint32_t> revents) -> void;
    auto events() const -> uint32_t;
private:
    auto onStopRequested() -> void;

    EpollDescriptor                *mFd = nullptr;
    IoResult<uint32_t>              mResult; //< The result of the awaiter
    uint32_t                        mEvents  = 0; //< Events to wait for
    runtime::CoroHandle             mCaller;
    runtime::StopRegistration       mRegistration;
    std::list<EpollAwaiter *>::iterator mIt;
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

auto EpollAwaiter::await_ready() -> bool {
    mIt = mFd->awaiters.end(); // Mark it
    if ((mFd->events & mEvents) == mEvents) { // The registered events are contains current events, no need to register on epoll
        return false;
    }
    // Do register
    ::epoll_event modevent;
    modevent.data.ptr = mFd;
    modevent.events   = mEvents | mFd->events | EPOLLONESHOT;
    if (::epoll_ctl(mFd->epollFd, EPOLL_CTL_MOD, mFd->fd, &modevent) == -1) {
        mResult = Err(SystemError::fromErrno());
        return true;
    }
    mFd->events |= mEvents;
    ILIAS_TRACE("Epoll", "Modify epoll event for fd: {}, events: {}", mFd->fd, epollToString(mFd->events | EPOLLONESHOT));
    return false;
}

auto EpollAwaiter::await_suspend(runtime::CoroHandle caller) -> void {
    mIt           = mFd->awaiters.insert(mFd->awaiters.end(), this);
    mCaller       = caller;
    mRegistration = runtime::StopRegistration(caller.stopToken(), [this]() { onStopRequested(); });
}

auto EpollAwaiter::await_resume() -> IoResult<uint32_t> {
    return mResult;
}

auto EpollAwaiter::onNotify(IoResult<uint32_t> revents) -> void {
    if (mIt == mFd->awaiters.end()) { // Already Got Event or Stopped
        return;
    }
    mIt = mFd->awaiters.end();
    mResult = revents;
    mCaller.schedule();
}

auto EpollAwaiter::events() const -> uint32_t {
    return mEvents;
}

auto EpollAwaiter::onStopRequested() -> void {
    if (mIt == mFd->awaiters.end()) { // Already Got Event or Stopped
        return;
    }
    mFd->awaiters.erase(mIt);
    mIt = mFd->awaiters.end();
    mCaller.setStopped();
}

EpollContext::EpollContext() {
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

EpollContext::~EpollContext() {
    ::close(mEpollFd);
    ::close(mEventFd);
}

auto EpollContext::addDescriptor(fd_t fd, IoDescriptor::Type type) -> IoResult<IoDescriptor *> {
    if (fd < 0) {
        ILIAS_WARN("Epoll", "Invalid file descriptor {}", fd);
        return Err(IoError::InvalidArgument);
    }
    if (type == IoDescriptor::Unknown || type == IoDescriptor::Tty) { // If user give us a tty, it may redirect to something else, check it
        auto res = fd_utils::type(fd);
        if (!res) {
            ILIAS_WARN("Epoll", "Failed to get file descriptor type {}", res.error().message());
            return Err(res.error());
        }
        type = res.value();
    }

    auto nfd        = std::make_unique<EpollDescriptor>();
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
            return Err(SystemError::fromErrno());
        }    
    }
    int  flags = ::fcntl(fd, F_GETFL, 0);
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK | O_CLOEXEC) == -1) {
        ILIAS_WARN("Epoll", "Failed to set descriptor to non-blocking. error: {}", SystemError::fromErrno());
    }
    return nfd.release();
}

auto EpollContext::removeDescriptor(IoDescriptor *fd) -> IoResult<void> {
    auto nfd = static_cast<EpollDescriptor *>(fd);
    cancel(nfd);
    if (nfd->pollable) {
        if (::epoll_ctl(mEpollFd, EPOLL_CTL_DEL, nfd->fd, nullptr) == -1) {
            ILIAS_ERROR("Epoll", "Failed to remove fd {} from epoll: {}", nfd->fd, SystemError::fromErrno());
        }
    }
    delete nfd;
    return {};
}

auto EpollContext::cancel(IoDescriptor *fd) -> IoResult<void> {
    auto nfd = static_cast<EpollDescriptor *>(fd);
    ILIAS_ASSERT(nfd != nullptr);
    ILIAS_TRACE("Epoll", "Cancel fd {} all pending operations for {}", nfd->fd, nfd->awaiters.size());
    if (nfd->pollable) { // if descriptor is pollable, cancel all poll awaiter
        for (auto &awaiter : nfd->awaiters) {
            awaiter->onNotify(Err(SystemError::Canceled));
        }
        nfd->awaiters.clear();
    }
    return {};
}


auto EpollContext::post(void (*fn)(void *), void *args) -> void {
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

auto EpollContext::run(runtime::StopToken token) -> void {
    while (!token.stop_requested()) {
        mService.updateTimers();
        processCompletion(token);
    }
}

auto EpollContext::sleep(uint64_t ms) -> Task<void> {
    co_return co_await mService.sleep(ms);
}

auto EpollContext::processCompletion(runtime::StopToken &token) -> void {
    while (!mCallbacks.empty()) { // Process all callbacks in the current thread queue
        auto cb = mCallbacks.front();
        mCallbacks.pop_front();
        cb.fn(cb.args);
        mService.updateTimers(); // Update timers after each callback, TODO: Make an better way
    }
    // No callbacks available and non exit requested, process epoll events
    if (token.stop_requested()) {
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

auto EpollContext::pollCallbacks() -> void {
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

auto EpollContext::processEvents(std::span<const epoll_event> eventsArray) -> void {
    for (const auto &item : eventsArray) {
        auto events = item.events;
        auto ptr = item.data.ptr;
        if (ptr == nullptr) { // From the event fd, wakeup epoll and poll callbacks
            pollCallbacks();
            continue;
        }

        // Normal descriptor, dispatch to the awaiters
        auto nfd = static_cast<EpollDescriptor *>(ptr);
        ILIAS_TRACE("Epoll", "Got epoll event for fd: {}, events: {}", nfd->fd, epollToString(events));
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

        // Re-arm the descriptor events
        epoll_event modevent;
        modevent.events = nfd->events | EPOLLONESHOT; // Just one shot
        modevent.data.ptr = nfd;
        if (::epoll_ctl(mEpollFd, EPOLL_CTL_MOD, nfd->fd, &modevent) == -1) {
            // Notify all pending awaiters
            ILIAS_WARN("Epoll", "Failed to modify fd {} epoll mode: {}", nfd->fd, SystemError::fromErrno());
            nfd->events = 0;
            auto error = SystemError::fromErrno();
            for (auto &awaiter : nfd->awaiters) {
                awaiter->onNotify(Err(error));
            }
            nfd->awaiters.clear();
        }
        else {
            ILIAS_TRACE("Epoll", "Modify epoll event for fd: {}, events: {}", nfd->fd, epollToString(nfd->events | EPOLLONESHOT));            
        }
    }
}

auto EpollContext::read(IoDescriptor *fd, MutableBuffer buffer, ::std::optional<size_t> offset)
    -> IoTask<size_t> {
    auto nfd = static_cast<EpollDescriptor *>(fd);
    ILIAS_ASSERT(nfd != nullptr);
    if (!nfd->pollable) {
        // Not supported operation when aio unavailable
#if !__has_include(<aio.h>)
        co_return Err(Error::OperationNotSupported);
#endif

    }
    ILIAS_ASSERT(nfd->type != IoDescriptor::Unknown);
    if (nfd->type == IoDescriptor::Tty) {
        if (auto res = co_await poll(nfd, EPOLLIN); !res) {
            co_return Err(res.error());
        }
        if (auto res = ::read(nfd->fd, buffer.data(), buffer.size()); res >= 0) {
            co_return res;
        }
        co_return Err(SystemError::fromErrno());
    }
    while (true) {
#if __has_include(<aio.h>)
        if (!nfd->pollable) { // Use POSIX AIO handle it
            co_return co_await posix::AioReadAwaiter {nfd->fd, buffer, offset};
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
            co_return Err(SystemError::fromErrno());
        }
        auto pollRet = co_await poll(nfd, EPOLLIN);
        if (!pollRet && pollRet.error() != SystemError(EINTR) && pollRet.error() != SystemError(EAGAIN)) {
            co_return Err(pollRet.error());
        }
    }
}

auto EpollContext::write(IoDescriptor *fd, Buffer buffer, ::std::optional<size_t> offset)
    -> IoTask<size_t> {
    auto nfd = static_cast<EpollDescriptor *>(fd);
    ILIAS_TRACE("Epoll", "start write {} bytes on fd {}", buffer.size(), nfd->fd);
    ILIAS_ASSERT(nfd != nullptr);
    if (!nfd->pollable) {
        // Not supported operation when aio unavailable
#if !__has_include(<aio.h>)
        co_return Err(Error::OperationNotSupported);
#endif

    }
    ILIAS_ASSERT(nfd->type != IoDescriptor::Unknown);
    while (true) {
#if __has_include(<aio.h>)
        if (!nfd->pollable) { // Use POSIX AIO handle it
            co_return co_await posix::AioWriteAwaiter {nfd->fd, buffer, offset};
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
            co_return Err(SystemError::fromErrno());
        }
        auto pollRet = co_await poll(nfd, EPOLLOUT);
        if (!pollRet && pollRet.error() != SystemError(EINTR) && pollRet.error() != SystemError(EAGAIN)) {
            co_return Err(pollRet.error());
        }
    }
}

auto EpollContext::connect(IoDescriptor *fd, EndpointView endpoint) -> IoTask<void> {
    auto nfd = static_cast<EpollDescriptor *>(fd);
    ILIAS_ASSERT(nfd != nullptr);
    ILIAS_ASSERT(nfd->type == IoDescriptor::Socket);
    ILIAS_TRACE("Epoll", "Start connect to {} on fd {}", endpoint, nfd->fd);
    if (::connect(nfd->fd, endpoint.data(), endpoint.length()) == 0) {
        ILIAS_TRACE("Epoll", "{} connect to {} successful", nfd->fd, endpoint);
        co_return {};
    }
    else if (errno != EINPROGRESS && errno != EAGAIN) {
        ILIAS_TRACE("Epoll", "{} connect to {} failed with {}", nfd->fd, endpoint, SystemError::fromErrno());
        co_return Err(SystemError::fromErrno());
    }
    if (auto pollRet = co_await poll(nfd, EPOLLOUT); !pollRet) {
        co_return Err(pollRet.error());
    }
    int       sockErr    = 0;
    socklen_t sockErrLen = sizeof(sockErr);
    if (::getsockopt(nfd->fd, SOL_SOCKET, SO_ERROR, &sockErr, &sockErrLen) == -1) {
        co_return Err(SystemError::fromErrno());
    }
    if (sockErr != 0) {
        ILIAS_TRACE("Epoll", "{} connect to {} failed with {}", nfd->fd, endpoint, SystemError(sockErr));
        co_return Err(SystemError(sockErr));
    }
    ILIAS_TRACE("Epoll", "{} connect to {} successful", nfd->fd, endpoint);
    co_return {};
}

auto EpollContext::accept(IoDescriptor *fd, MutableEndpointView remoteEndpoint) -> IoTask<socket_t> {
    auto nfd = static_cast<EpollDescriptor *>(fd);
    ILIAS_ASSERT(nfd != nullptr);
    ILIAS_ASSERT(nfd->type == IoDescriptor::Socket);
    ILIAS_TRACE("Epoll", "Start accept on fd {}", nfd->fd);
    auto socket = SocketView(nfd->fd);
    while (true) {
        if (auto ret = socket.accept<socket_t>(remoteEndpoint); ret) {
            co_return ret;
        }
        else if (ret.error() != SystemError(EAGAIN) && ret.error() != SystemError(EWOULDBLOCK)) {
            co_return Err(SystemError::fromErrno());
        }
        if (auto pollRet = co_await poll(fd, EPOLLIN); !pollRet) {
            co_return Err(pollRet.error());
        }
    }
}

auto EpollContext::sendto(IoDescriptor *fd, Buffer buffer, int flags,
                                 EndpointView endpoint) -> IoTask<size_t> {
    auto nfd = static_cast<EpollDescriptor *>(fd);
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
            co_return Err(pollRet.error());
        }
    }
}

auto EpollContext::recvfrom(IoDescriptor *fd, MutableBuffer buffer, int flags,
                                   MutableEndpointView endpoint) -> IoTask<size_t> {
    auto nfd = static_cast<EpollDescriptor *>(fd);
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
            co_return Err(pollRet.error());
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
auto EpollContext::poll(IoDescriptor *fd, uint32_t events) -> IoTask<uint32_t> {
    auto nfd = static_cast<EpollDescriptor *>(fd);
    ILIAS_ASSERT(nfd != nullptr);
    if (!nfd->pollable) {
        co_return Err(IoError::OperationNotSupported);
    }
    co_return co_await EpollAwaiter {nfd, events};
}

} // namespace linux

ILIAS_NS_END