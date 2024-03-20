#pragma once

#include "ilias_expected.hpp"
#include "ilias_backend.hpp"
#include "ilias_latch.hpp"
#include "ilias.hpp"
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <map>

#if defined(__cpp_lib_coroutine)
#include "ilias_co.hpp"
#endif

#if defined(__linux) 
#include <sys/epoll.h>
#include <array>
#endif

ILIAS_NS_BEGIN

enum PollEvent : int {
    In  = POLLIN,
    Out = POLLOUT,
    Err = POLLERR,
    Hup = POLLHUP,
    All = In | Out,
};

/**
 * @brief A Interface to get notifyed
 * 
 */
class IOWatcher {
public:
    virtual void onEvent(int event) = 0;
protected:
    IOWatcher() = default;
    ~IOWatcher() = default;
private:
    socket_t mFd = ILIAS_INVALID_SOCKET;
    int64_t  mPollfdIdx = -1; //< Location of it's pollfd
    int      mPollEvents = 0; //< Info of it's request events
friend class PollContext;
};

/**
 * @brief A callback like watcher
 * 
 */
class PollOperation final : public IOWatcher {
public:
    PollOperation() = default;
    ~PollOperation() = default;

    void onEvent(int revents) override { mCallback(revents); }
    void setCallback(Function<void(int revents)> &&callback) { mCallback = std::move(callback); }
private:
    Function<void(int revents)> mCallback;
};

/**
 * @brief A Context for add watcher 
 * 
 */
class PollContext final : public IOContext, private IOWatcher {
public:
    PollContext();
    PollContext(const PollContext&) = delete;
    ~PollContext();
    
    // Poll Watcher interface
    bool addWatcher(SocketView socket, IOWatcher* watcher, int events);
    bool modifyWatcher(IOWatcher* watcher, int events);
    bool removeWatcher(IOWatcher* watcher);
    IOWatcher *findWatcher(SocketView socket);

    // Async Interface
    bool asyncInitialize(SocketView socket) override;
    bool asyncCleanup(SocketView socket) override;
    bool asyncCancel(SocketView, void *operation) override;

    void *asyncRecv(SocketView socket, void *buffer, size_t n, int64_t timeout, RecvHandler &&cb) override;
    void *asyncSend(SocketView socket, const void *buffer, size_t n, int64_t timeout, SendHandler &&cb) override;
    void *asyncAccept(SocketView socket, int64_t timeout, AcceptHandler &&cb) override;
    void *asyncConnect(SocketView socket, const IPEndpoint &endpoint, int64_t timeout, ConnectHandler &&cb) override;

    void *asyncRecvfrom(SocketView socket, void *buffer, size_t n, int64_t timeout, RecvfromHandler &&cb) override;
    void *asyncSendto(SocketView socket, const void *buffer, size_t n, const IPEndpoint &endpoint, int64_t timeout, SendtoHandler &&cb) override;

    // Poll
    void *asyncPoll(SocketView socket, int revent, int64_t timeout, Function<void(int revents)> &&cb);
private:
    struct Fn {
        void (*fn)(void *);
        void *arg;    
    };

    void _run();
    void _notify(const ::pollfd &pfd);
    void _stop();
    void _dump();
    void _invoke(Fn);
    template <typename T>
    void _invoke4(T &&callable);
    void onEvent(int events) override;
    
    SockInitializer       mInitalizer;

    std::thread           mThread; //< Threads for network poll
    std::atomic_bool      mRunning {true}; //< Flag to stop the thread
    std::mutex            mMutex;
    std::map<socket_t, IOWatcher*> mWatchers;

#if defined(__linux)
    int mEpollfd = -1;
#else
    std::vector<::pollfd> mPollfds;
#endif

    Socket mEvent; //< Socket poll in workThread
    Socket mControl; //< Socket for sending message
};

// --- PollContext Impl
inline PollContext::PollContext() {
    // Init socket
#ifndef __linux
    Socket server(AF_INET, SOCK_STREAM, 0);
    server.bind(IPEndpoint(IPAddress4::loopback(), 0));
    server.listen();
    mControl = Socket(AF_INET, SOCK_STREAM, 0);
    mControl.connect(server.localEndpoint());
    mEvent = server.accept().first;
#else
    int fds[2];
    ::socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    mEvent.reset(fds[0]);
    mControl.reset(fds[1]);
    mEpollfd = ::epoll_create1(0);
#endif
    mEvent.setBlocking(false);

    // Add event socket
    addWatcher(mEvent, this, PollEvent::In);

    // Start Work Thread
    mThread = std::thread(&PollContext::_run, this);
}
inline PollContext::~PollContext() {
    _stop();
    mThread.join();

#ifdef __linux
    ::close(mEpollfd);
#endif
}
inline void PollContext::_run() {
    // < Common Poll
#ifndef __linux
    std::vector<::pollfd> collected;
    while (mRunning) {
        auto n = ILIAS_POLL(mPollfds.data(), mPollfds.size(), -1);
        if (n < 0) {
            ::printf("[Ilias::PollContext] Poll Error %s\n", SockError::fromErrno().message().c_str());
        }
        // Because first socket are event soocket, for the array from [end to begin]
        for (auto iter = mPollfds.rbegin(); iter != mPollfds.rend(); iter++) {
            if (n == 0) {
                break;
            }
            if (iter->revents) {
                n--;
                collected.push_back(*iter);
                iter->revents = 0;
            }
        }
        // Dispatch
        for (const auto &v : collected) {
            _notify(v);
        }
        collected.clear();
    }
#else
    // Check EPoll Events as same as Poll
    static_assert(EPOLLIN == POLLIN);
    static_assert(EPOLLOUT == POLLOUT);
    static_assert(EPOLLERR == POLLERR);
    static_assert(EPOLLHUP == POLLHUP);

    std::array<::epoll_event, 1024> events;
    while (mRunning) {
        auto n = ::epoll_wait(mEpollfd, events.data(), events.size(), -1);
        if (n < 0) {
            ::printf("[Ilias::PollContext] Epoll Error %s\n", SockError::fromErrno().message().c_str());
        }
        for (int i = 0; i < n; i++) {
            auto watcher = static_cast<IOWatcher*>(events[i].data.ptr);
            if (!watcher) {
                continue;
            }
            watcher->onEvent(events[i].events);
        }
    }
#endif

}
inline void PollContext::_notify(const ::pollfd &pfd) {
    auto iter = mWatchers.find(pfd.fd);
    if (iter == mWatchers.end()) {
        return;
    }
    auto &watcher = iter->second;
    watcher->onEvent(pfd.revents);
}
inline void PollContext::_dump() {
#if !defined(NDEBUG)
    ::printf("[Ilias::PollContext] Dump Watchers\n");
    for (const auto &pair : mWatchers) {
        auto watcher = pair.second;
        if (watcher->mFd == mEvent.get()) {
            continue;
        }
        std::string events;
        if (watcher->mPollEvents & PollEvent::In) {
            events += "In ";
        }
        if (watcher->mPollEvents & PollEvent::Out) {
            events += "Out ";
        }
        ::printf(
            "[Ilias::PollContext] IOWatcher %p Socket %lu Event %s\n",
            watcher, 
            uintptr_t(watcher->mFd), 
            events.c_str()
        );
    }
#endif
}
inline bool PollContext::addWatcher(SocketView socket, IOWatcher *watcher, int events) {
    if (mThread.joinable()) {
        if (mThread.get_id() != std::this_thread::get_id()) {
            bool val = false;
            _invoke4([=, this, &val]() {
                val = addWatcher(socket, watcher, events);
            });
            return val;
        }
    }
    ILIAS_ASSERT(!(events & PollEvent::Err));
    ILIAS_ASSERT(!(events & PollEvent::Hup));

#ifndef __linux
    ::pollfd pfd;
    pfd.events = events;
    pfd.revents = 0;
    pfd.fd = socket.get();

    watcher->mFd = socket.get();
    watcher->mPollEvents = events;
    watcher->mPollfdIdx = mPollfds.size(); //< Store the location
    mWatchers.emplace(socket.get(), watcher);
    mPollfds.push_back(pfd);
#else
    ::epoll_event epevent {};
    epevent.events = events;
    epevent.data.ptr = watcher;

    if (::epoll_ctl(mEpollfd, EPOLL_CTL_ADD, socket.get(), &epevent) != 0) {
        // Error
        return false;
    }
    watcher->mFd = socket.get();
    watcher->mPollEvents = events;
    mWatchers.emplace(socket.get(), watcher);
#endif

    _dump();
    return true;
}
inline bool PollContext::modifyWatcher(IOWatcher *watcher, int events) {
    if (mThread.get_id() != std::this_thread::get_id()) {
        bool val = false;
        _invoke4([=, this, &val]() {
            val =  modifyWatcher(watcher, events);
        });
        return val;
    }

#ifndef __linux
    if (watcher->mPollfdIdx <= 0) {
        return false;
    }

    mPollfds[watcher->mPollfdIdx].events = events;
    mPollfds[watcher->mPollfdIdx].revents = 0;
#else
    ::epoll_event epevent {};
    epevent.events = events;
    epevent.data.ptr = watcher;

    if (::epoll_ctl(mEpollfd, EPOLL_CTL_MOD, watcher->mFd, &epevent) != 0) {
        // Error
        return false;
    }
#endif
    // Ok, do Common
    watcher->mPollEvents = events;

    _dump();
    return true;
}
inline bool PollContext::removeWatcher(IOWatcher *watcher) {
    if (mThread.get_id() != std::this_thread::get_id()) {
        bool val = false;
        _invoke4([=, this, &val]() {
            val =  removeWatcher(watcher);
        });
        return val;
    }

#ifndef __linux
    auto fd = watcher->mFd;
    auto idx = watcher->mPollfdIdx;
    auto iter = mWatchers.find(fd);
    if (iter == mWatchers.end()) {
        return false;
    }
    if (idx < 0) {
        return false;
    }
    if (idx + 1 == mPollfds.size()) {
        // Is end, just remove
        mPollfds.pop_back();
    }
    else {
        // Modify the prev end with current index
        mWatchers[mPollfds.back().fd]->mPollfdIdx = idx;
        // Swap with the end
        std::swap(mPollfds[idx], mPollfds.back());
        // Remove it
        mPollfds.pop_back();
    }
    mWatchers.erase(iter);

    watcher->mFd = ILIAS_INVALID_SOCKET;
    watcher->mPollfdIdx = -1;
    watcher->mPollEvents = 0;
#else
    if (::epoll_ctl(mEpollfd, EPOLL_CTL_DEL, watcher->mFd, nullptr) != 0) {
        // Fail to remove
        return false;
    }
    mWatchers.erase(watcher->mFd);
    watcher->mFd = ILIAS_INVALID_SOCKET;
    watcher->mPollEvents = 0;
#endif

    _dump();
    return true;
}
inline IOWatcher *PollContext::findWatcher(SocketView socket) {
    if (mThread.get_id() != std::this_thread::get_id()) {
        IOWatcher *watcher = nullptr;
        _invoke4([=, this, &watcher]() {
            watcher = findWatcher(socket);
        });
        return watcher;
    }
    auto fd = socket.get();
    auto iter = mWatchers.find(fd);
    if (iter == mWatchers.end()) {
        return nullptr;
    }
    return iter->second;
}
inline void PollContext::onEvent(int events) {
    if (!(events & PollEvent::In)) {
        return;
    }
    Fn fn;
    while (mEvent.recv(&fn, sizeof(Fn)) == sizeof(Fn)) {
        fn.fn(fn.arg);
    }
}
inline void PollContext::_stop() {
    Fn fn;
    fn.fn = [](void *arg) { static_cast<PollContext *>(arg)->mRunning = false; };
    fn.arg = this;
    _invoke(fn);
}
inline void PollContext::_invoke(Fn fn) {
    struct Args {
        Fn fn;
        std::latch latch {1};
    } args {fn};

    Fn helperFn;
    helperFn.fn = [](void *ptr) {
        auto arg = static_cast<Args *>(ptr);
        arg->fn.fn(arg->fn.arg);
        arg->latch.count_down();
    };
    helperFn.arg = &args;
    std::unique_lock<std::mutex> lock(mMutex);
    mControl.send(&helperFn, sizeof(Fn));
    lock.unlock();
    args.latch.wait();
}
template <typename Callable>
inline void PollContext::_invoke4(Callable &&callable) {
    Fn fn;
    fn.fn = [](void *callable) {
        auto c = static_cast<Callable *>(callable);
        (*c)();
    };
    fn.arg = &callable;
    _invoke(fn);
}

// Async Interface
inline bool PollContext::asyncInitialize(SocketView socket) {
    if (!socket.isValid()) {
        return false;
    }
    if (!socket.setBlocking(false)) {
        return false;
    }
    return true;
}
inline bool PollContext::asyncCleanup(SocketView socket) {
    if (!socket.isValid()) {
        return false;
    }
    return asyncCancel(socket, nullptr); //< Cancel all operation
}
inline bool PollContext::asyncCancel(SocketView socket, void *op) {
    PollOperation *operation = static_cast<PollOperation*>(op);
    if (!operation) {
        operation = static_cast<PollOperation*>(findWatcher(socket));
    }
    if (!operation) {
        return false;
    }
    auto v = removeWatcher(operation);
    delete operation;
    return v;
}
inline void *PollContext::asyncPoll(SocketView socket, int revent, int64_t timeout, Function<void(int revents)> &&cb) {
    auto operation = new PollOperation;
    if (!operation) {
        return 0;
    }
    if (timeout > 0) {
        // TODO Timeout
        ::printf("TODO: Timeout\n");
    }
    operation->setCallback([this, operation, b = std::move(cb)](int revents) mutable {
        // Remove it
        removeWatcher(operation);
        // Invoke User callback
        b(revents);
        delete operation;
    });
    addWatcher(socket, operation, revent);
    return operation;
}
inline void *PollContext::asyncSend(SocketView socket, const void *buffer, size_t n, int64_t timeout, SendHandler &&callback) {
    auto bytes = socket.send(buffer, n);
    if (bytes >= 0) {
        // Ok, call callback
        callback(bytes);
        return nullptr;
    }
    auto err = SockError::fromErrno();
    if (!err.isInProgress() && !err.isWouldBlock()) {
        // Not async operation, return error by callback
        callback(Unexpected(err));
        return nullptr;
    }

    // Ok, we need to wait for the socket to be writable
    return asyncPoll(socket, PollEvent::Out, timeout, [cb = std::move(callback), socket, buffer, n](int revents) mutable {
        if (revents & PollEvent::Out) {
            // Ok, call callback
            auto bytes = socket.send(buffer, n);
            if (bytes >= 0) {
                cb(bytes);
                return;
            }
            // Error, call callback
            cb(Unexpected(SockError::fromErrno()));
        }
        if (revents & PollEvent::Err) {
            // Error, call callback
            cb(Unexpected(SockError::fromErrno()));
        }
    });
}
inline void *PollContext::asyncRecv(SocketView socket, void *buffer, size_t n, int64_t timeout, RecvHandler &&callback) {
    auto bytes = socket.recv(buffer, n);
    if (bytes >= 0) {
        // Ok, call callback
        callback(bytes);
        return nullptr;
    }
    auto err = SockError::fromErrno();
    if (!err.isInProgress() && !err.isWouldBlock()) {
        // Not async operation, return error by callback
        callback(Unexpected(err));
        return nullptr;
    }
    // Ok, wait readable
    return asyncPoll(socket, PollEvent::In, timeout, [cb = std::move(callback), socket, buffer, n](int revents) mutable {
        if (revents & PollEvent::In) {
            auto bytes = socket.recv(buffer, n);
            if (bytes >= 0) {
                cb(bytes);
                return;
            }
            // Error, call callback
            cb(Unexpected(SockError::fromErrno()));
        }
        if (revents & PollEvent::Err) {
            // Error, call callback
            cb(Unexpected(SockError::fromErrno()));
        }
    });
}
inline void *PollContext::asyncAccept(SocketView socket, int64_t timeout, AcceptHandler &&callback) {
    auto pair = socket.accept<Socket>();
    if (pair.first.isValid()) {
        // Got value
        callback(std::move(pair));
        return nullptr;
    }
    auto err = SockError::fromErrno();
    if (!err.isInProgress() && !err.isWouldBlock()) {
        callback(Unexpected(err));
        return nullptr;
    }
    return asyncPoll(socket, PollEvent::In, timeout, [cb = std::move(callback), socket](int revents) mutable {
        if (revents & PollEvent::In) {
            auto pair = socket.accept<Socket>();
            if (pair.first.isValid()) {
                // Got value
                cb(std::move(pair));
                return;
            }
        }
        if (revents & PollEvent::Err) {
            // Error, call callback
            cb(Unexpected(SockError::fromErrno()));
        }
    });
}
inline void *PollContext::asyncConnect(SocketView socket, const IPEndpoint &endpoint, int64_t timeout, ConnectHandler &&callback) {
    if (socket.connect(endpoint)) {
        callback(Expected<void, SockError>());
        return nullptr;
    }
    auto err = SockError::fromErrno();
    if (!err.isInProgress() && !err.isWouldBlock()) {
        callback(Unexpected(err));
        return nullptr;
    }
    // Ok, we need to wait for the socket to be writable
    return asyncPoll(socket, PollEvent::Out, timeout, [cb = std::move(callback), socket, endpoint](int revents) mutable {
        auto err = socket.error();
        if (err.isOk() && (revents & PollEvent::Out)) {
            // Ok
            cb(Expected<void, SockError>());
            return;
        }
        // Error, call callback
        cb(Unexpected(err));
    });
}
inline void *PollContext::asyncRecvfrom(SocketView socket, void *buffer, size_t n, int64_t timeout, RecvfromHandler &&callback) {
    IPEndpoint endpoint;
    auto bytes = socket.recvfrom(buffer, n, 0, &endpoint);
    if (bytes >= 0) {
        callback(std::make_pair(size_t(bytes), endpoint));
        return nullptr;
    }
    auto err = SockError::fromErrno();
    if (!err.isInProgress() && !err.isWouldBlock()) {
        callback(Unexpected(err));
        return nullptr;
    }
    // Ok, we need to wait for the socket to be readable
    return asyncPoll(socket, PollEvent::In, timeout, [cb = std::move(callback), socket, buffer, n](int revents) mutable {
        if (revents & PollEvent::In) {
            IPEndpoint endpoint;
            auto bytes = socket.recvfrom(buffer, n, 0, &endpoint);
            if (bytes >= 0) {
                cb(std::make_pair(size_t(bytes), endpoint));
                return;
            }
        }
        // Error, call callback
        cb(Unexpected(SockError::fromErrno()));
    });
}
inline void *PollContext::asyncSendto(SocketView socket, const void *buffer, size_t n, const IPEndpoint &endpoint, int64_t timeout, SendtoHandler &&callback) {
    auto bytes = socket.sendto(buffer, n, 0, &endpoint);
    if (bytes >= 0) {
        callback(bytes);
        return nullptr;
    }
    auto err = SockError::fromErrno();
    if (!err.isInProgress() && !err.isWouldBlock()) {
        callback(Unexpected(err));
        return nullptr;
    }
    // Ok, we need to wait for the socket to be writable
    return asyncPoll(socket, PollEvent::Out, timeout, [cb = std::move(callback), socket, buffer, n, ep = endpoint](int revents) mutable {
        if (revents & PollEvent::Out) {
            auto bytes = socket.sendto(buffer, n, 0, &ep);
            if (bytes >= 0) {
                cb(bytes);
                return;
            }
        }
        // Error, call callback
        cb(Unexpected(SockError::fromErrno()));
    });
}

#if !defined(_WIN32)
using NativeIOContext = PollContext;
#endif

ILIAS_NS_END