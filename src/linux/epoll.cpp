#include <ilias/platform/detail/blocking.hpp>
#include <ilias/platform/epoll.hpp>
#include <ilias/detail/intrusive.hpp>
#include <ilias/runtime/token.hpp>
#include <ilias/io/system_error.hpp>
#include <ilias/io/fd_utils.hpp>
#include <ilias/io/error.hpp>
#include <ilias/net/endpoint.hpp>
#include <ilias/net/msghdr.hpp>
#include <ilias/net/sockfd.hpp>

#include <sys/timerfd.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <list>


ILIAS_NS_BEGIN

namespace os_linux {
namespace {

constexpr uintptr_t KIND_EVENT_FD = 0;
constexpr uintptr_t KIND_TIMER_FD = 1;

// MARK: EpollAwaiter
class EpollDescriptor;
class EpollAwaiter final : public intrusive::ListNode<EpollAwaiter> {
public:
    EpollAwaiter(EpollDescriptor *fd, uint32_t events) : mFd(fd), mEvents(events) {}

    auto await_ready() -> bool;
    auto await_suspend(runtime::CoroHandle caller) -> void;
    auto await_resume() -> IoResult<uint32_t>;

    auto onNotify(IoResult<uint32_t> revents) -> void;
    auto events() const -> uint32_t;
private:
    auto onStopRequested() -> void;

    EpollDescriptor          *mFd = nullptr;
    IoResult<uint32_t>        mResult; //< The result of the awaiter
    uint32_t                  mEvents  = 0; //< Events to wait for
    runtime::CoroHandle       mCaller;
    runtime::StopRegistration mRegistration;
};

class EpollDescriptor final : public IoDescriptor {
public:
    int                fd         = -1;
    IoDescriptor::Type type       = Unknown;
    int                epollFd    = -1;
    bool               pollable   = false;

    // Poll Status
    intrusive::List<EpollAwaiter> awaiters;
    uint32_t                      events = 0; // Current all combined events
};

[[maybe_unused]]
auto epollToString(uint32_t events) -> std::string {
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
    mFd->awaiters.push_back(*this);
    mCaller = caller;
    mRegistration.register_<&EpollAwaiter::onStopRequested>(caller.stopToken(), this);
}

auto EpollAwaiter::await_resume() -> IoResult<uint32_t> {
    return mResult;
}

auto EpollAwaiter::onNotify(IoResult<uint32_t> revents) -> void {
    mResult = revents;
    mCaller.schedule();
}

auto EpollAwaiter::events() const -> uint32_t {
    return mEvents;
}

auto EpollAwaiter::onStopRequested() -> void {
    if (!isLinked()) { // Already Got Event or Stopped
        return;
    }
    unlink();
    mCaller.setStopped();
}

auto epollCreate() -> FileDescriptor {
    auto fd = ::epoll_create1(EPOLL_CLOEXEC);
    if (fd == -1) {
        ILIAS_ERROR("Epoll", "Failed to create epoll file descriptor");
        ILIAS_THROW(std::system_error(SystemError::fromErrno(), "epoll_create1"));
    }
    return FileDescriptor{fd};
}

auto eventfdCreate() -> FileDescriptor {
    auto fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (fd == -1) {
        ILIAS_ERROR("Epoll", "Failed to create eventfd file descriptor");
        ILIAS_THROW(std::system_error(SystemError::fromErrno(), "eventfd"));
    }
    return FileDescriptor{fd};
}

auto timerfdCreate() -> FileDescriptor {
    auto fd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (fd == -1) {
        ILIAS_ERROR("Epoll", "Failed to create timerfd file descriptor");
        ILIAS_THROW(std::system_error(SystemError::fromErrno(), "timerfd_create"));
    }
    return FileDescriptor{fd};
}

} // namespace

EpollContext::EpollContext() : 
    mEpollFd(epollCreate()),
    mEventFd(eventfdCreate()),
    mTimerFd(timerfdCreate())
{
    // Bind eventfd
    ::epoll_event event;
    event.events = EPOLLIN;
    event.data.ptr = reinterpret_cast<void*>(KIND_EVENT_FD); // Special ptr, mark eventfd
    if (::epoll_ctl(mEpollFd.get(), EPOLL_CTL_ADD, mEventFd.get(), &event) == -1) {
        ILIAS_ERROR("Epoll", "Failed to add eventfd to epoll");
        ILIAS_THROW(std::system_error(SystemError::fromErrno(), "epoll_ctl"));
    }
    
    // Bind timerfd
    event.events = EPOLLIN;
    event.data.ptr = reinterpret_cast<void*>(KIND_TIMER_FD); // Special ptr, mark timerfd
    if (::epoll_ctl(mEpollFd.get(), EPOLL_CTL_ADD, mTimerFd.get(), &event) == -1) {
        ILIAS_ERROR("Epoll", "Failed to add timerfd to epoll");
        ILIAS_THROW(std::system_error(SystemError::fromErrno(), "epoll_ctl"));
    }

    // Bind the service
    mService.setCallback([this](auto timepoint) {
        using namespace std::chrono;

        ::itimerspec timerval {};
        if (timepoint) { // If the next timepoint is set, set the timer, other wise, disable it
            auto now = steady_clock::now();
            auto diff = duration_cast<nanoseconds>(*timepoint - now);
            if (diff.count() < 0) {
                diff = nanoseconds{1};
            }
            // Just one shot
            timerval.it_interval.tv_sec  = 0;
            timerval.it_interval.tv_nsec = 0;
            timerval.it_value.tv_sec  = diff.count() / 1000000000;
            timerval.it_value.tv_nsec = diff.count() % 1000000000;
        }
        if (::timerfd_settime(mTimerFd.get(), 0, &timerval, nullptr) == -1) {
            ILIAS_WARN("Epoll", "Failed to set timerfd time: {}", SystemError::fromErrno());
        }
        ILIAS_TRACE("Epoll", "Update timerfd time");
    });
}

EpollContext::~EpollContext() {

}

auto EpollContext::addDescriptor(fd_t fd, IoDescriptor::Type type) -> IoResult<IoDescriptor *> {
    if (fd < 0) {
        ILIAS_WARN("Epoll", "Invalid file descriptor {}", fd);
        return Err(IoError::InvalidArgument);
    }
    if (type == IoDescriptor::Unknown || type == IoDescriptor::Tty) { // If user give us a tty, it may redirect to something else, check it
        ILIAS_TRY(type, fd_utils::type(fd));
    }

    auto nfd        = std::make_unique<EpollDescriptor>();
    nfd->fd      = fd;
    nfd->type    = type;
    nfd->epollFd = mEpollFd.get();

    // Check is pollable
    if (type == IoDescriptor::Pipe || type == IoDescriptor::Tty || type == IoDescriptor::Socket || type == IoDescriptor::Pollable) {
        nfd->pollable = true;
        epoll_event event;
        event.events = 0 | EPOLLONESHOT; // Just do simple register
        event.data.ptr = nfd.get();
        if (::epoll_ctl(mEpollFd.get(), EPOLL_CTL_ADD, fd, &event) == -1) {
            ILIAS_ERROR("Epoll", "Failed to add fd {} to epoll: {}", fd, strerror(errno));
            return Err(SystemError::fromErrno());
        }    
    }
    if (::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) | O_NONBLOCK | O_CLOEXEC) == -1) {
        ILIAS_WARN("Epoll", "Failed to set descriptor to non-blocking & clo-exec. error: {}", SystemError::fromErrno());
    }
    ILIAS_TRACE("Epoll", "Created new fd descriptor: {}, type: {}", fd, type);
    return nfd.release();
}

auto EpollContext::removeDescriptor(IoDescriptor *fd) -> IoResult<void> {
    auto nfd = static_cast<EpollDescriptor *>(fd);
    if (nfd->pollable) {
        if (::epoll_ctl(mEpollFd.get(), EPOLL_CTL_DEL, nfd->fd, nullptr) == -1) {
            ILIAS_ERROR("Epoll", "Failed to remove fd {} from epoll: {}", nfd->fd, SystemError::fromErrno());
        }
    }
    delete nfd;
    return {};
}

auto EpollContext::post(void (*fn)(void *), void *args) -> void {
    ILIAS_TRACE("Epoll", "Post callback {} with args {}", reinterpret_cast<void*>(fn), args);
    ILIAS_ASSERT(fn, "Can't post nullptr callback");

    std::pair callback {fn, args};
    if (runtime::Executor::currentThread() == this) { // Same thread, just push to the queue
        mCallbacks.emplace_back(callback);
        return;
    }

    // Different thread, push to the queue and wakeup the epoll
    bool wakeup = false;
    {
        std::lock_guard locker {mMutex};
        mPendingCallbacks.emplace_back(callback);
        wakeup = !mWakePending.exchange(true, std::memory_order::relaxed); // There is no wakeup pending, need to set the eventfd
    }
    
    if (wakeup) {
        uint64_t data = 1; // Wakeup epoll
        if (::write(mEventFd.get(), &data, sizeof(data)) != sizeof(data)) {
            // Why write failed?
            ILIAS_WARN("Epoll", "Failed to write to event fd: {}", SystemError::fromErrno());
        }
    }
}

auto EpollContext::run(runtime::StopToken token) -> void {
    auto running = true;
    auto cb = runtime::StopCallback(token, [&, this]() {
        schedule([&]() { running = false; });
    });
    while (running) {
        mService.updateTimers();
        processCompletion(running);
    }
}

auto EpollContext::sleep(std::chrono::nanoseconds ns) -> Task<void> {
    co_return co_await mService.sleep(ns);
}

inline
auto EpollContext::processCompletion(bool &running) -> void {
    while (!mCallbacks.empty()) { // Process all callbacks in the current thread queue
        auto cb = mCallbacks.front();
        mCallbacks.pop_front();
        cb.first(cb.second);
        mService.updateTimers(); // Update timers after each callback, TODO: Make an better way
    }
    // No callbacks available and non exit requested, process epoll events
    if (!running) {
        return;
    }

    // Time to wait
    std::array<::epoll_event, 64> events;
    std::span view {events};
    // Wait forever until we got any events (callbacks, io, timer)
    if (auto res = ::epoll_wait(mEpollFd.get(), view.data(), view.size(), -1); res > 0) { // Got any events
        for (auto event : view.subspan(0, res)) {
            processEvent(event);
        }
    }
}

inline
auto EpollContext::pollCallbacks() -> void {
    std::lock_guard locker {mMutex};
    ILIAS_TRACE("Epoll", "Polling {} callbacks from different thread queue", mPendingCallbacks.size());
    if (mCallbacks.empty()) { // Use swap to make it faster
        mCallbacks.swap(mPendingCallbacks);
    }
    else {
        mCallbacks.insert(mCallbacks.end(), mPendingCallbacks.begin(), mPendingCallbacks.end());
        mPendingCallbacks.clear();
    }

    // Reset wakeup flag
    uint64_t data = 0; 
    mWakePending.store(false, std::memory_order::relaxed); // Consume it
    if (::read(mEventFd.get(), &data, sizeof(data)) != sizeof(data)) {
        // Why read failed?
        ILIAS_WARN("Epoll", "Failed to read from event fd: {}", SystemError::fromErrno());
    }
}

inline
auto EpollContext::processTimer() -> void {
    uint64_t expiredCount = 0;
    ILIAS_TRACE("Epoll", "Process timer fd");
    while (::read(mTimerFd.get(), &expiredCount, sizeof(expiredCount)) == sizeof(uint64_t)) {
        mService.updateTimers();
    }
}

// MARK: Process Events
inline
auto EpollContext::processEvent(const ::epoll_event item) -> void {
    auto events = item.events;
    auto ptr = item.data.ptr;
    if (ptr == reinterpret_cast<void*>(KIND_EVENT_FD)) { // From the event fd, wakeup epoll and poll callbacks
        pollCallbacks();
        return;
    }
    if (ptr == reinterpret_cast<void*>(KIND_TIMER_FD)) { // From the timer fd, update timers
        processTimer();
        return;
    }

    // Normal descriptor, dispatch to the awaiters
    auto nfd = static_cast<EpollDescriptor *>(ptr);
    ILIAS_TRACE("Epoll", "Got epoll event for fd: {}, events: {}", nfd->fd, epollToString(events));
    uint32_t newEvents = 0; // New interested events
    for (auto it = nfd->awaiters.begin(); it != nfd->awaiters.end();) {
        auto &awaiter = *it;
        bool isInterested = awaiter.events() & events;
        bool isErrorOrHup = events & EPOLLERR || events & EPOLLHUP;
        bool shouldNotify = isInterested || isErrorOrHup; // Notify if interested or error or hangup

        if (shouldNotify) {
            awaiter.onNotify(events);
            it = nfd->awaiters.erase(it);    
        }
        else {
            newEvents |= awaiter.events(); // Collect the new interested events
            ++it;
        }
    }

    // Update the events we still interested
    nfd->events = newEvents;
    if (nfd->events == 0) { // No more interested events
        ILIAS_ASSERT(nfd->awaiters.empty()); // No more interested events, no more awaiters
        ILIAS_TRACE("Epoll", "Fd {} no more interested events", nfd->fd);
        return; // Because oneshot, we don't need to modify the epoll event, just do nothing
    }

    // Re-arm the descriptor events
    ::epoll_event modevent;
    modevent.events = nfd->events | EPOLLONESHOT; // Just one shot
    modevent.data.ptr = nfd;
    if (::epoll_ctl(mEpollFd.get(), EPOLL_CTL_MOD, nfd->fd, &modevent) == -1) {
        // Notify all pending awaiters, error happened
        ILIAS_WARN("Epoll", "Failed to modify fd {} epoll mode: {}", nfd->fd, SystemError::fromErrno());
        nfd->events = 0;
        auto error = SystemError::fromErrno();
        for (auto iter = nfd->awaiters.begin(); iter != nfd->awaiters.end(); ) {
            iter->onNotify(Err(error));
            iter = nfd->awaiters.erase(iter);
        }
        return;
    }
    ILIAS_TRACE("Epoll", "Modify epoll event for fd: {}, events: {}", nfd->fd, epollToString(nfd->events | EPOLLONESHOT));
}

// MARK: Io
auto EpollContext::read(IoDescriptor *fd, MutableBuffer buffer, ::std::optional<size_t> offset) -> IoTask<size_t> {
    auto nfd = static_cast<EpollDescriptor *>(fd);
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
        if (!nfd->pollable) { // Use thread pool handle it
            co_return co_await runtime::threadpool::read(nfd->fd, buffer, offset);
        }
        ::ssize_t ret = 0;
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
        ILIAS_CO_TRYV(co_await poll(nfd, EPOLLIN));
    }
}

auto EpollContext::write(IoDescriptor *fd, Buffer buffer, ::std::optional<size_t> offset) -> IoTask<size_t> {
    auto nfd = static_cast<EpollDescriptor *>(fd);
    while (true) {
        if (!nfd->pollable) { // Use thread pool handle it
            co_return co_await runtime::threadpool::write(nfd->fd, buffer, offset);
        }
        ::ssize_t ret = 0;
        if (offset) {
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
        ILIAS_CO_TRYV(co_await poll(nfd, EPOLLOUT));
    }
}

auto EpollContext::connect(IoDescriptor *fd, EndpointView endpoint) -> IoTask<void> {
    auto nfd = static_cast<EpollDescriptor *>(fd);
    SocketView socket{nfd->fd};
    if (auto res = socket.connect(endpoint); res) { // Immediate connected
        co_return {};
    }
    else if (auto err = res.error(); err != SystemError::InProgress && err != SystemError::WouldBlock) { // Failed
        co_return Err(SystemError::fromErrno());
    }
    ILIAS_CO_TRYV(co_await poll(nfd, EPOLLOUT));
    
    // Completed, take the error code
    ILIAS_CO_TRY(auto err, socket.error());
    if (!err.isOk()) {
        co_return Err(err);
    }
    ILIAS_TRACE("Epoll", "{} connect to {} successful", nfd->fd, endpoint);
    co_return {};
}

auto EpollContext::accept(IoDescriptor *fd, MutableEndpointView remoteEndpoint) -> IoTask<socket_t> {
    auto nfd = static_cast<EpollDescriptor *>(fd);
    SocketView socket{nfd->fd};
    while (true) {
        if (auto ret = socket.accept<socket_t>(remoteEndpoint); ret) {
            co_return ret;
        }
        else if (auto err = ret.error(); err == SystemError(EINTR)) {
            continue; // Retry
        }
        else if (err != SystemError::InProgress && err != SystemError::WouldBlock) {
            co_return Err(SystemError::fromErrno());
        }
        ILIAS_CO_TRYV(co_await poll(nfd, EPOLLIN));
    }
}

auto EpollContext::sendto(IoDescriptor *fd, Buffer buffer, int flags, EndpointView endpoint) -> IoTask<size_t> {
    auto nfd = static_cast<EpollDescriptor *>(fd);
    SocketView socket{nfd->fd};
    while (true) {
        if (auto ret = socket.sendto(buffer, flags | MSG_DONTWAIT | MSG_NOSIGNAL, endpoint); ret) {
            co_return ret;
        }
        else if (auto err = ret.error(); err == SystemError(EINTR)) {
            continue; // Retry
        }
        else if (err != SystemError::InProgress && err != SystemError::WouldBlock) {
            co_return ret;
        }
        ILIAS_CO_TRYV(co_await poll(nfd, EPOLLOUT));
    }
}

auto EpollContext::recvfrom(IoDescriptor *fd, MutableBuffer buffer, int flags, MutableEndpointView endpoint) -> IoTask<size_t> {
    auto nfd = static_cast<EpollDescriptor *>(fd);
    SocketView socket(nfd->fd);
    while (true) {
        if (auto ret = socket.recvfrom(buffer, flags | MSG_DONTWAIT | MSG_NOSIGNAL, endpoint); ret) {
            co_return ret;
        }
        else if (auto err = ret.error(); err == SystemError(EINTR)) {
            continue; // Retry
        }
        else if (err != SystemError::InProgress && err != SystemError::WouldBlock) {
            co_return ret;
        }
        ILIAS_CO_TRYV(co_await poll(nfd, EPOLLIN));
    }
}

auto EpollContext::sendmsg(IoDescriptor *fd, const MsgHdr &msg, int flags) -> IoTask<size_t> {
    auto nfd = static_cast<EpollDescriptor *>(fd);
    while (true) {
        if (auto ret = ::sendmsg(nfd->fd, &msg, flags | MSG_DONTWAIT | MSG_NOSIGNAL); ret > 0) {
            co_return ret;
        }
        else if (auto err = errno; ret == -1 && (err != EINTR && err != EAGAIN && err != EWOULDBLOCK)) {
            co_return Err(SystemError(err));
        }
        ILIAS_CO_TRYV(co_await poll(nfd, EPOLLOUT));
    }
}

auto EpollContext::recvmsg(IoDescriptor *fd, MutableMsgHdr &msg, int flags) -> IoTask<size_t> {
    auto nfd = static_cast<EpollDescriptor *>(fd);
    while (true) {
        if (auto ret = ::recvmsg(nfd->fd, &msg, flags | MSG_DONTWAIT | MSG_NOSIGNAL); ret > 0) {
            co_return ret;
        }
        else if (auto err = errno; ret == -1 && (err != EINTR && err != EAGAIN && err != EWOULDBLOCK)) {
            co_return Err(SystemError::fromErrno());
        }
        ILIAS_CO_TRYV(co_await poll(nfd, EPOLLIN));
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

} // namespace os_linux

ILIAS_NS_END