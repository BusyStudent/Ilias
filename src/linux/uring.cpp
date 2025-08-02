#include <ilias/defines.hpp>

#if defined(ILIAS_USE_IO_URING)
#include <ilias/platform/uring.hpp>
#include <ilias/task/when_any.hpp>
#include <ilias/task/task.hpp>
#include <ilias/sync/event.hpp>
#include <sys/eventfd.h>
#include <sys/utsname.h>
#include "uring_core.hpp"
#include "uring_ops.hpp"

ILIAS_NS_BEGIN

namespace linux {

class UringDescriptor final : public IoDescriptor {
public:
    int           fd;   
    struct ::stat stat; //< The file stat
    Event         cancel; //< The cancel event
};

UringContext::UringContext(UringConfig conf) {
    if (auto ret = ::io_uring_queue_init(conf.entries, &mRing, conf.flags); ret != 0) {
        auto err = -ret;
        ILIAS_ERROR("Uring", "Failed to io_uring_queue_init({}, {}) => {}", conf.entries, conf.flags, SystemError(err));
        ILIAS_THROW(std::system_error(err, std::system_category()));
    }
    mEventFd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (mEventFd == -1) {
        ILIAS_THROW(std::system_error(errno, std::system_category()));
    }

    // Setup the pipe to process the callback
    auto sqe = allocSqe();
    ::io_uring_prep_poll_multishot(sqe, mEventFd, POLLIN);
    ::io_uring_sqe_set_data(sqe, nullptr);

    // Version
#if defined(IO_URING_VERSION_MAJOR) && defined(IO_URING_VERSION_MINOR)
    ILIAS_TRACE("Uring", "Using liburing {}.{}", IO_URING_VERSION_MAJOR, IO_URING_VERSION_MINOR);
#endif

    // Detect kernel
    struct utsname buf;
    if (::uname(&buf) != 0) {
        return;
    }
    int major = 0, minor = 0, patch = 0;
    if (::sscanf(buf.release, "%d.%d.%d", &major, &minor, &patch) < 2) {
        return;
    }
    ILIAS_TRACE("Uring", "Kernel version {}.{}.{}", major, minor, patch);
    mFeatures.cancelFd = major > 5 || (major == 5 && minor >= 19); // At linux 5.19, io_uring support cancel_fd
}

UringContext::~UringContext() {
    ::io_uring_queue_exit(&mRing);
    ::close(mEventFd);
}

auto UringContext::processCompletion() -> void {
    ::io_uring_cqe *cqe = nullptr;
    if (::io_uring_wait_cqe(&mRing, &cqe) != 0 || cqe == nullptr) [[unlikely]] {
        ILIAS_ERROR("Uring", "io_uring_wait_cqe failed {}", SystemError(-errno));
        return;
    }
    auto entry = *cqe;
    auto ptr = ::io_uring_cqe_get_data(&entry);
    auto cb = static_cast<UringCallback*>(ptr);
    ::io_uring_cqe_seen(&mRing, cqe);
    if (cb) [[likely]] { // Normal completion
        cb->onCallback(cb, entry);
    }
    else { // Completion from the eventfd
        std::lock_guard locker {mMutex};
        mCallbacks.insert(mCallbacks.end(), mPendingCallbacks.begin(), mPendingCallbacks.end());
        mPendingCallbacks.clear();
        uint64_t data = 0; // Reset wakeup flag
        if (::read(mEventFd, &data, sizeof(data)) != sizeof(data)) {
            // ? Why read failed?
            ILIAS_WARN("Uring", "Failed to read from event fd: {}", SystemError::fromErrno());
        }
    }
}

auto UringContext::allocSqe() -> ::io_uring_sqe * {
    auto sqe = ::io_uring_get_sqe(&mRing);
    if (!sqe) {
        auto n = ::io_uring_submit(&mRing);
        sqe = ::io_uring_get_sqe(&mRing);
    }
    ILIAS_ASSERT(sqe);
    return sqe;
}

auto UringContext::post(void (*fn)(void *), void *args) -> void {
    auto cb = std::pair(fn, args);
    if (std::this_thread::get_id() == mThreadId) { // Same thread, just push to the queue
        mCallbacks.emplace_back(cb);
        return;
    }
    // Different thread, push to the queue and wakeup the io uring
    {
        std::lock_guard locker {mMutex};
        mPendingCallbacks.emplace_back(cb);
    }
    uint64_t data = 1; // Wakeup
    if (::write(mEventFd, &data, sizeof(data)) != sizeof(data)) {
        // ? Why write failed?
        ILIAS_WARN("Epoll", "Failed to write to event fd: {}", SystemError::fromErrno());
    }
}

auto UringContext::run(runtime::StopToken token) -> void {
    auto reg = runtime::StopCallback(token, [this]() {
        // Alloc the noop sqe, let it wakeup the ring
        auto sqe = allocSqe();
        ::io_uring_prep_nop(sqe);
        ::io_uring_sqe_set_data(sqe, UringCallback::noop());
        ::io_uring_submit(&mRing);
    });
    while (!token.stop_requested()) {
        // Prcoess the callback queue
        while (!mCallbacks.empty()) {
            auto cb = mCallbacks.front();
            mCallbacks.pop_front();
            cb.first(cb.second);
        }
        ::io_uring_submit(&mRing); // Submit any pending requests
        if (!token.stop_requested()) {
            processCompletion();
        }
    }
}

auto UringContext::addDescriptor(fd_t fd, IoDescriptor::Type type) -> IoResult<IoDescriptor*> {
    auto nfd = std::make_unique<UringDescriptor>();
    if (::fstat(fd, &nfd->stat) != 0) {
        return Err(SystemError::fromErrno());
    }
    ILIAS_TRACE("Uring", "Adding fd {}", fd);

    nfd->fd = fd;
    return nfd.release();
}

auto UringContext::removeDescriptor(IoDescriptor *fd) -> IoResult<void> {
    auto nfd = static_cast<UringDescriptor*>(fd);
    ILIAS_TRACE("Uring", "Removing fd {}", nfd->fd);
    auto _ = cancel(nfd);
    delete nfd;
    return {};
}

auto UringContext::cancel(IoDescriptor *fd) -> IoResult<void> {
    auto nfd = static_cast<UringDescriptor*>(fd);
    ILIAS_TRACE("Uring", "Cancelling fd {}", nfd->fd);

#if IO_URING_VERSION_MINOR > 2
    if (mFeatures.cancelFd) { // Allow cancelling fd
        auto sqe = allocSqe();
        ::io_uring_prep_cancel_fd(sqe, nfd->fd, 0);
        ::io_uring_sqe_set_data(sqe, UringCallback::noop());
        ::io_uring_submit(&mRing);    
    }
#endif
    
    return {};
}

auto UringContext::sleep(uint64_t ms) -> Task<void> {
    ::__kernel_timespec ts { };
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    co_return co_await UringTimeoutAwaiter {mRing, ts};
}

auto UringContext::read(IoDescriptor *fd, MutableBuffer buffer, std::optional<size_t> offset) -> IoTask<size_t> {
    auto nfd = static_cast<UringDescriptor*>(fd);
    co_return co_await UringReadAwaiter {mRing, nfd->fd, buffer, offset};
}

auto UringContext::write(IoDescriptor *fd, Buffer buffer, std::optional<size_t> offset) -> IoTask<size_t> {
    auto nfd = static_cast<UringDescriptor*>(fd);
    co_return co_await UringWriteAwaiter {mRing, nfd->fd, buffer, offset};
}

auto UringContext::accept(IoDescriptor *fd, MutableEndpointView endpoint) -> IoTask<socket_t> {
    auto nfd = static_cast<UringDescriptor*>(fd);
    co_return co_await UringAcceptAwaiter {mRing, nfd->fd, endpoint};
}

auto UringContext::connect(IoDescriptor *fd, EndpointView endpoint) -> IoTask<void> {
    auto nfd = static_cast<UringDescriptor*>(fd);
    co_return co_await UringConnectAwaiter {mRing, nfd->fd, endpoint};
}

auto UringContext::sendto(IoDescriptor *fd, Buffer buffer, int flags, EndpointView endpoint) -> IoTask<size_t> {
    auto nfd = static_cast<UringDescriptor*>(fd);
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
    co_return co_await UringSendmsgAwaiter {mRing, nfd->fd, msg, flags};
}

auto UringContext::recvfrom(IoDescriptor *fd, MutableBuffer buffer, int flags, MutableEndpointView endpoint) -> IoTask<size_t> {
    auto nfd = static_cast<UringDescriptor*>(fd);
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
    co_return co_await UringRecvmsgAwaiter {mRing, nfd->fd, msg, flags};
}

auto UringContext::poll(IoDescriptor *fd, uint32_t events) -> IoTask<uint32_t> {
    auto nfd = static_cast<UringDescriptor*>(fd);
    co_return co_await UringPollAwaiter {mRing, nfd->fd, events};
}

} // namespace linux

ILIAS_NS_END

#endif // ILIAS_USE_URING