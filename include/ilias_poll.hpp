#pragma once

#include "ilias_backend.hpp"
#include <sys/timerfd.h>
#include <sys/epoll.h>
#include <array>
#include <map>

ILIAS_NS_BEGIN

/**
 * @brief 
 * 
 */
class PollWatcher {
public:
    virtual auto onEvent(::uint32_t revent) -> void = 0;
};

/**
 * @brief EPoll Context for Polling
 * 
 */
class PollContext final : public IoContext {
public:
    PollContext();
    PollContext(const PollContext &) = delete;
    ~PollContext();

    // EventLoop
    auto run() -> void override;
    auto quit() -> void override;
    auto post(void (*)(void *), void *) -> void override;
    auto delTimer(uintptr_t timer) -> bool override;
    auto addTimer(int64_t ms, void (*fn)(void *), void *arg, int flags) -> uintptr_t override;

    // IoContext
    auto addSocket(SocketView fd) -> Result<void> override;
    auto removeSocket(SocketView fd) -> Result<void> override;
    
    auto send(SocketView fd, const void *buffer, size_t n) -> Task<size_t> override;
    auto recv(SocketView fd, void *buffer, size_t n) -> Task<size_t> override;
    auto connect(SocketView fd, const IPEndpoint &endpoint) -> Task<void> override;
    auto accept(SocketView fd) -> Task<std::pair<Socket, IPEndpoint> > override;
    auto sendto(SocketView fd, const void *buffer, size_t n, const IPEndpoint &endpoint) -> Task<size_t> override;
    auto recvfrom(SocketView fd, void *buffer, size_t n) -> Task<std::pair<size_t, IPEndpoint> > override;

    // 
    auto poll(int fd, uint32_t event) -> Task<uint32_t>;
private:
    // for Impl post
    struct PipeWatcher : PollWatcher {
        auto onEvent(uint32_t) -> void override;
        PollContext *self;
    } mPipeWatcher;

    // for Impl timer
    struct TimerWatcher : PollWatcher {
        auto onEvent(uint32_t) -> void override;
        PollContext *self;
    } mTimerWatcher;

    // for Impl post
    struct Fn {
        void (*fn)(void *);
        void  *args;
    };
    auto _show(::epoll_event event) -> void;

    int mEpollfd = -1;
    int mPipeRecv = -1;
    int mPipeSend = -1;
    int mTimerfd = -1;
    bool mQuit = false;
};

inline PollContext::PollContext() {
    int pipes[2];
    ::pipe2(pipes, O_NONBLOCK);
    mPipeRecv = pipes[0];
    mPipeSend = pipes[1];
    mEpollfd = ::epoll_create1(0);

    // Add the recv pipe to poll
    ::epoll_event event;
    event.events = EPOLLIN;
    event.data.ptr = &mPipeWatcher;
    mPipeWatcher.self = this;
    if (::epoll_ctl(mEpollfd, EPOLL_CTL_ADD, mPipeRecv, &event) == -1) {
        ::perror("epoll+ctl");
    }

    // Setup timerfd
    mTimerfd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    event.events = EPOLLIN | EPOLLOUT | EPOLLHUP;
    event.data.ptr = &mTimerWatcher;
    mTimerWatcher.self = this;
    if (::epoll_ctl(mEpollfd, EPOLL_CTL_ADD, mTimerfd, &event) == -1) {
        ::perror("epoll+ctl");
    }
}
inline PollContext::~PollContext() {
    ::close(mEpollfd);
    ::close(mPipeRecv);
    ::close(mPipeSend);
    ::close(mTimerfd);
}
inline auto PollContext::run() -> void {
    epoll_event events[128];
    while (!mQuit) {
        int n = ::epoll_wait(mEpollfd, events, 128, -1);
        if (n < 0) {
            return;
        }
        for (int i = 0; i < n; i++) {
            auto event = events[i];
            auto watcher = static_cast<PollWatcher*>(event.data.ptr);
            _show(event);
            if (!watcher) {
                continue;
            }
            watcher->onEvent(event.events);
        }
    }
    mQuit = false;
}
inline auto PollContext::post(void (*fn)(void *), void *args) -> void {
    Fn f {fn, args};
    auto n = ::write(mPipeSend, &f, sizeof(f));
    ILIAS_ASSERT(n == sizeof(f));
}
inline auto PollContext::quit() -> void {
    post([](void *self) {
        static_cast<PollContext*>(self)->mQuit = true;
    }, this);
}
inline auto PollContext::PipeWatcher::onEvent(::uint32_t revent) -> void {
    if (!(revent & EPOLLIN)) {
        return;
    }
    Fn fn;
    while (::read(self->mPipeRecv, &fn, sizeof(fn)) == sizeof(fn)) {
        fn.fn(fn.args);
    }
}
inline auto PollContext::TimerWatcher::onEvent(::uint32_t revent) -> void {
    int64_t timestamp = 0;
    while (::read(self->mTimerfd, &timestamp, sizeof(timestamp)) == sizeof(timestamp)) {
        // ETC...
    }
}
inline auto PollContext::_show(::epoll_event event) -> void {
    fprintf(stderr, "[Ilias] EPoll Event ");
    if (event.events & EPOLLIN) {
        fprintf(stderr, "EPOLLIN ");
    }
    if (event.events & EPOLLOUT) {
        fprintf(stderr, "EPOLLOUT ");
    }
    if (event.events & EPOLLERR) {
        fprintf(stderr, "EPOLLERR ");
    }
    if (event.events & EPOLLHUP) {
        fprintf(stderr, "EPOLLHUP ");
    }
    fprintf(stderr, "on watcher %p\n", event.data.ptr);
}

// Timer
inline auto PollContext::delTimer(uintptr_t timer) -> bool {
    return false;
}
inline auto PollContext::addTimer(int64_t ms, void (*fn)(void *), void *arg, int flags) -> uintptr_t {
    return 0;
}

// EPOLL socket here
inline auto PollContext::addSocket(SocketView sock) -> Result<void> {
    return sock.setBlocking(false);
}
inline auto PollContext::removeSocket(SocketView sock) -> Result<void> {
    return Result<void>();
}

inline auto PollContext::send(SocketView fd, const void *buffer, size_t n) -> Task<size_t> {
    while (true) {
        auto ret = fd.send(buffer, n);
        if (ret) {
            co_return ret;
        }
        if (ret.error() != Error::WouldBlock) {
            co_return ret;
        }
        auto pollret = co_await poll(fd.get(), EPOLLOUT);
        if (!pollret) {
            co_return Unexpected(pollret.error());
        }
    }
}
inline auto PollContext::recv(SocketView fd, void *buffer, size_t n) -> Task<size_t> {
    while (true) {
        auto ret = fd.recv(buffer, n);
        if (ret) {
            co_return ret;
        }
        if (ret.error() != Error::WouldBlock) {
            co_return ret;
        }
        auto pollret = co_await poll(fd.get(), EPOLLIN);
        if (!pollret) {
            co_return Unexpected(pollret.error());
        }
    }
}
inline auto PollContext::connect(SocketView fd, const IPEndpoint &endpoint) -> Task<void> {
    auto ret = fd.connect(endpoint);
    if (ret) {
        co_return ret;
    }
    if (ret.error() != Error::InProgress) {
        co_return ret;
    }
    auto pollret = co_await poll(fd.get(), EPOLLOUT);
    if (!pollret) {
        co_return Unexpected(pollret.error());
    }
    auto err = fd.error().value();
    if (!err.isOk()) {
        co_return Unexpected(err);
    }
    co_return Result<>();
}
inline auto PollContext::accept(SocketView fd) -> Task<std::pair<Socket, IPEndpoint> > {
    while (true) {
        auto ret = fd.accept<Socket>();
        if (ret) {
            co_return ret;
        }
        if (ret.error() != Error::WouldBlock) {
            co_return ret;
        }
        auto pollret = co_await poll(fd.get(), EPOLLIN);
        if (!pollret) {
            co_return Unexpected(pollret.error());
        }
    }
}
inline auto PollContext::sendto(SocketView fd, const void *buffer, size_t n, const IPEndpoint &endpoint) -> Task<size_t> {
    while (true) {
        auto ret = fd.sendto(buffer, n, 0, endpoint);
        if (ret) {
            co_return ret;
        }
        if (ret.error() != Error::WouldBlock) {
            co_return ret;
        }
        auto pollret = co_await poll(fd.get(), EPOLLOUT);
        if (!pollret) {
            co_return Unexpected(pollret.error());
        }
    }
}
inline auto PollContext::recvfrom(SocketView fd, void *buffer, size_t n) -> Task<std::pair<size_t, IPEndpoint> > {
    while (true) {
        IPEndpoint endpoint;
        auto ret = fd.recvfrom(buffer, n, 0, &endpoint);
        if (ret) {
            co_return std::make_pair(*ret, endpoint);
        }
        if (ret.error() != Error::WouldBlock) {
            co_return Unexpected(ret.error());
        }
        auto pollret = co_await poll(fd.get(), EPOLLIN);
        if (!pollret) {
            co_return Unexpected(pollret.error());
        }
    }
}

inline auto PollContext::poll(int fd, uint32_t events) -> Task<uint32_t> {
    struct PollAwaiter : PollWatcher {
        auto await_ready() -> bool { 
            event.data.ptr = this;
            if (::epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event) == -1) {
                perror("poll error");
                epollError = true;
                return true;
            }
            epollAdded = true;
            return false; //< Wating Epoll
        }
        auto await_suspend(std::coroutine_handle<> h) -> void {
            callerHandle = h;
        }
        auto await_resume() -> Result<uint32_t> {
            if (epollAdded) {
                ::epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, &event);
            }
            if (epollError) {
                return Unexpected(Error::fromErrno());
            }
            if (!notified) {
                return Unexpected(Error::Canceled); //< Use cancel
            }
            return revents;
        }
        auto onEvent(uint32_t r) -> void override {
            revents = r;
            notified = true;
            callerHandle.resume();
        }
        int fd = 0;
        int epollfd = 0;
        bool epollError = false; //< Does we got any error from add the fd
        bool epollAdded = false; //< Does the fd still in epoll ?
        bool notified = false; //< Does we has been notifyed ?
        uint32_t revents = 0; //< Received events
        std::coroutine_handle <> callerHandle;
        ::epoll_event event; //< Requested event
    };

    PollAwaiter awaiter;
    awaiter.fd = fd;
    awaiter.epollfd = mEpollfd;
    awaiter.event.events = events;
    co_return co_await awaiter;
}

ILIAS_NS_END