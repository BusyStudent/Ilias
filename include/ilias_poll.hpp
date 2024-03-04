#pragma once

#include "ilias_expected.hpp"
#include "ilias_latch.hpp"
#include "ilias.hpp"
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <map>

#if defined(__cpp_lib_coroutine)
#include "ilias_co.hpp"
#endif

ILIAS_NS_BEGIN

// --- Function
#if defined(__cpp_lib_move_only_function)
template <typename ...Args>
using Function = std::move_only_function<Args...>;
#else
template <typename ...Args>
using Function = std::function<Args...>;
#endif

enum PollEvent : int {
    Timeout = -1,
    None = 0,
    In  = POLLIN,
    Out = POLLOUT,
    Err = POLLERR,
    Hup = POLLHUP,
    All = In | Out
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

    void onEvent(int revents) override { mCallback(revents); mCallback = nullptr; }
    void setCallback(Function<void(int revents)> &&callback) { mCallback = std::move(callback); }
private:
    Function<void(int revents)> mCallback;
};

/**
 * @brief A Context for add watcher 
 * 
 */
class PollContext final : private IOWatcher {
public:
    PollContext();
    PollContext(const PollContext&) = delete;
    ~PollContext();

    // Poll Watcher interface
    void addWatcher(SocketView socket, IOWatcher* watcher, int events);
    bool modifyWatcher(IOWatcher* watcher, int events);
    bool removeWatcher(IOWatcher* watcher);
    IOWatcher *findWatcher(SocketView socket);

    // Async Interface
    bool asyncInitialize(SocketView socket);
    bool asyncCleanup(SocketView socket);
    bool asyncCancel(SocketView, void *operation);

    void *asyncRecv(SocketView socket, void *buffer, size_t n, Function<void(Expected<size_t, SockError> &&)> &&cb);
    void *asyncSend(SocketView socket, const void *buffer, size_t n, Function<void(Expected<size_t, SockError> &&)> &&cb);
    void *asyncAccept(SocketView socket, Function<void(Expected<std::pair<Socket, IPEndpoint> , SockError> &&)> &&cb);
    void *asyncConnect(SocketView socket, const IPEndpoint &endpoint, Function<void(Expected<void, SockError> &&)> &&cb);

    // Poll
    void *asyncPoll(SocketView socket, int revent, Function<void(int revents)> &&cb);
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
    std::vector<::pollfd> mPollfds;
    std::mutex            mMutex;
    std::map<socket_t, IOWatcher*> mWatchers;

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
}
inline void PollContext::_run() {
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
#ifndef NDEBUG
    ::printf("[Ilias::PollContext] Dump Watchers\n");
    for (const auto &pfd : mPollfds) {
        if (pfd.fd == mEvent.get()) {
            continue;
        }
        std::string events;
        if (pfd.events & PollEvent::In) {
            events += "In ";
        }
        if (pfd.events & PollEvent::Out) {
            events += "Out ";
        }
        ::printf(
            "[Ilias::PollContext] IOWatcher %p Socket %lu Event %s\n",
            mWatchers[pfd.fd], 
            uintptr_t(pfd.fd), 
            events.c_str()
        );
    }
#endif
}
inline void PollContext::addWatcher(SocketView socket, IOWatcher *watcher, int events) {
    if (mThread.joinable()) {
        if (mThread.get_id() != std::this_thread::get_id()) {
            return _invoke4([=, this]() {
                addWatcher(socket, watcher, events);
            });
        }
    }
    ILIAS_ASSERT(!(events & PollEvent::Err));
    ILIAS_ASSERT(!(events & PollEvent::Hup));

    ::pollfd pfd;
    pfd.events = events;
    pfd.revents = 0;
    pfd.fd = socket.get();

    watcher->mFd = socket.get();
    watcher->mPollfdIdx = mPollfds.size(); //< Store the location
    mWatchers.emplace(socket.get(), watcher);
    mPollfds.push_back(pfd);

    _dump();
}
inline bool PollContext::modifyWatcher(IOWatcher *watcher, int events) {
    if (mThread.get_id() != std::this_thread::get_id()) {
        bool val = false;
        _invoke4([=, this, &val]() {
            val =  modifyWatcher(watcher, events);
        });
        return val;
    }
    if (watcher->mPollfdIdx <= 0) {
        return false;
    }

    mPollfds[watcher->mPollfdIdx].events = events;
    mPollfds[watcher->mPollfdIdx].revents = 0;

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
    addWatcher(socket, new PollOperation, PollEvent::None);
    return true;
}
inline bool PollContext::asyncCleanup(SocketView socket) {
    if (!socket.isValid()) {
        return false;
    }
    auto watcher = findWatcher(socket);
    if (!watcher) {
        return false;
    }
    return removeWatcher(watcher);
}
inline bool PollContext::asyncCancel(SocketView socket, void *op) {
    PollOperation *operation = static_cast<PollOperation*>(op);
    if (!operation) {
        operation = static_cast<PollOperation*>(findWatcher(socket));
    }
    if (!operation) {
        return false;
    }
    return modifyWatcher(operation, PollEvent::None); //< Let it poll nothing
}
inline void *PollContext::asyncPoll(SocketView socket, int revent, Function<void(int revents)> &&cb) {
    auto operation = static_cast<PollOperation*>(findWatcher(socket));
    if (!operation) {
        return 0;
    }
    operation->setCallback([this, operation, b = std::move(cb)](int revents) mutable {
        // Let it watch nothing
        modifyWatcher(operation, PollEvent::None);
        // Invoke User callback
        b(revents);
    });
    modifyWatcher(operation, revent);
    return operation;
}
inline void *PollContext::asyncSend(SocketView socket, const void *buffer, size_t n, Function<void(Expected<size_t, SockError> &&)> &&callback) {
    auto bytes = socket.send(buffer, n);
    if (bytes >= 0) {
        // Ok, call callback
        callback(bytes);
        return nullptr;
    }
    auto err = SockError::fromErrno();
    if (!err.isInProgress() && err.isWouldBlock()) {
        // Not async operation, return error by callback
        callback(Unexpected(err));
        return nullptr;
    }

    // Ok, we need to wait for the socket to be writable
    return asyncPoll(socket, PollEvent::Out, [cb = std::move(callback), socket, buffer, n](int revents) mutable {
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
inline void *PollContext::asyncRecv(SocketView socket, void *buffer, size_t n, Function<void(Expected<size_t, SockError> &&)> &&callback) {
    auto bytes = socket.recv(buffer, n);
    if (bytes >= 0) {
        // Ok, call callback
        callback(bytes);
        return nullptr;
    }
    auto err = SockError::fromErrno();
    if (!err.isInProgress() && err.isWouldBlock()) {
        // Not async operation, return error by callback
        callback(Unexpected(err));
        return nullptr;
    }
    // Ok, wait readable
    return asyncPoll(socket, PollEvent::Out, [cb = std::move(callback), socket, buffer, n](int revents) mutable {
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


using IOContext = PollContext;

ILIAS_NS_END