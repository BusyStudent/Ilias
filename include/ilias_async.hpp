#pragma once

#include "ilias_latch.hpp"
#include "ilias.hpp"
#include <thread>
#include <atomic>
#include <mutex>
#include <map>

#if defined(__cpp_lib_coroutine)
    #include "ilias_co.hpp"
#endif

ILIAS_NS_BEGIN

enum PollEvent : int {
    Timeout = -1,
    None = 0,
    In  = POLLIN,
    Out = POLLOUT,
    Err = POLLERR,
    Hup = POLLHUP,
    All = In | Out
};

template <typename T, typename Err = SockError>
class Expected {
public:
    explicit Expected(T &&v) : mType(Value) {
        new(&mStorage.value) T(std::move(v));
    }
    explicit Expected(Err &&e) : mType(Error) {
        new(&mStorage.err) Err(std::move(e));
    }
    ~Expected() {
        if (mType == Value) {
            mStorage.value.~T();
        }
        else if (mType == Error) {
            mStorage.err.~Err();
        }
    }
    T value() && {
        ILIAS_ASSERT(mType == Value);
        return std::move(mStorage.value);
    }
    Err error() && {
        ILIAS_ASSERT(mType == Error);
        return std::move(mStorage.err);
    }
    bool has_value() const {
        return mType == Value;
    }
private:
    union Storage {
        T value;
        Err err;
    } mStorage;
    enum {
        Value = 0,
        Error = 1
    } mType;
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
friend class IOContext;
};

/**
 * @brief A Context for add watcher 
 * 
 */
class IOContext : private IOWatcher {
public:
    IOContext();
    IOContext(const IOContext&) = delete;
    ~IOContext();

    void addWatcher(SocketView socket, IOWatcher* watcher, int events);
    bool modifyWatcher(IOWatcher* watcher, int events);
    bool removeWatcher(IOWatcher* watcher);
    IOWatcher *findWatcher(SocketView socket);

#ifdef __cpp_lib_coroutine
    Task<int>     asyncPoll(SocketView socket, int events);
    Task<ssize_t> asyncRecv(SocketView socket, void* buffer, size_t length, int flags = 0);
    Task<ssize_t> asyncSend(SocketView socket, const void* buffer, size_t length, int flags = 0);
    Task<bool>    asyncConnect(SocketView socket, const IPEndpoint& endpoint);
    template <typename T>
    Task<std::pair<T, IPEndpoint> > asyncAccept(const T &socket);
#endif

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

class AsyncSocket {
public:
    AsyncSocket(IOContext &ctxt, socket_t sockfd);
    AsyncSocket(const AsyncSocket &) = delete;
    ~AsyncSocket();
private:
    IOContext *mContext = nullptr;
    Socket     mSocket;
};

class TcpSocket : public AsyncSocket {

};
class UdpSocket : public AsyncSocket {

};


// --- IOContext Impl
inline IOContext::IOContext() {
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
    mThread = std::thread(&IOContext::_run, this);
}
inline IOContext::~IOContext() {
    _stop();
    mThread.join();
}
inline void IOContext::_run() {
    std::vector<::pollfd> collected;
    while (mRunning) {
        auto n = ILIAS_POLL(mPollfds.data(), mPollfds.size(), -1);
        if (n < 0) {
            ::printf("[Ilias::IOContext] Poll Error %s\n", SockError::fromErrno().message().c_str());
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
inline void IOContext::_notify(const ::pollfd &pfd) {
    auto iter = mWatchers.find(pfd.fd);
    if (iter == mWatchers.end()) {
        return;
    }
    auto &watcher = iter->second;
    watcher->onEvent(pfd.revents);
}
inline void IOContext::_dump() {
#ifndef NDEBUG
    ::printf("[Ilias::IOContext] Dump Watchers\n");
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
            "[Ilias::IOContext] IOWatcher %p Socket %lu Event %s\n",
            mWatchers[pfd.fd], 
            uintptr_t(pfd.fd), 
            events.c_str()
        );
    }
#endif
}
inline void IOContext::addWatcher(SocketView socket, IOWatcher *watcher, int events) {
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
inline bool IOContext::modifyWatcher(IOWatcher *watcher, int events) {
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
inline bool IOContext::removeWatcher(IOWatcher *watcher) {
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
inline IOWatcher *IOContext::findWatcher(SocketView socket) {
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
inline void IOContext::onEvent(int events) {
    if (!(events & PollEvent::In)) {
        return;
    }
    Fn fn;
    while (mEvent.recv(&fn, sizeof(Fn)) == sizeof(Fn)) {
        fn.fn(fn.arg);
    }
}
inline void IOContext::_stop() {
    Fn fn;
    fn.fn = [](void *arg) { static_cast<IOContext *>(arg)->mRunning = false; };
    fn.arg = this;
    _invoke(fn);
}
inline void IOContext::_invoke(Fn fn) {
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
inline void IOContext::_invoke4(Callable &&callable) {
    Fn fn;
    fn.fn = [](void *callable) {
        auto c = static_cast<Callable *>(callable);
        (*c)();
    };
    fn.arg = &callable;
    _invoke(fn);
}

#ifdef __cpp_lib_coroutine
// Coroutine 
inline Task<int> IOContext::asyncPoll(SocketView socket, int events) {
    if (events == 0 || !socket.isValid()) {
        co_return 0;
    }
    struct Watcher : IOWatcher {
        void onEvent(int revents) override {
            resume(std::move(revents));
            ctxt->removeWatcher(this);
            delete this;
        }
        IOContext *ctxt;
        CallbackAwaitable<int>::ResumeFunc resume;
    };
    int revents = co_await CallbackAwaitable<int>(
        [=, this](CallbackAwaitable<int>::ResumeFunc &&func) {
            auto watcher = new Watcher();
            watcher->ctxt = this;
            watcher->resume = std::move(func);
            addWatcher(socket, watcher, events);
        }
    );
    co_return revents;
}
inline Task<ssize_t> IOContext::asyncRecv(SocketView socket, void *buf, size_t count, int flags) {
    ssize_t n = socket.recv(buf, count, flags);
    if (n == -1) {
        auto err = SockError::fromErrno();
        if (!err.isWouldBlock()) {
            co_return n; //< Just Error
        }
        auto event = co_await asyncPoll(socket, PollEvent::In);
        if (event & PollEvent::In) {
            co_return socket.recv(buf, count, flags);
        }
    }
    co_return n;
}
inline Task<ssize_t> IOContext::asyncSend(SocketView socket, const void *buf, size_t count, int flags) {
    ssize_t n = socket.send(buf, count, flags);
    if (n == -1) {
        auto err = SockError::fromErrno();
        if (!err.isWouldBlock()) {
            co_return n; //< Just Error
        }
        auto event = co_await asyncPoll(socket, PollEvent::Out);
        if (event & PollEvent::Out) {
            co_return socket.send(buf, count, flags);
        }
    }
    co_return n;
}
inline Task<bool> IOContext::asyncConnect(SocketView socket, const IPEndpoint &endpoint) {
    bool connected = socket.connect(endpoint);
    if (!connected) {
        auto err = SockError::fromErrno();
        if (!err.isInProgress() && !err.isWouldBlock()) {
            co_return connected; //< Just Error
        }
        auto event = co_await asyncPoll(socket, PollEvent::Out);
        if (event & (PollEvent::Out | PollEvent::Err)) {
            co_return true;
        }
    }
    co_return connected;
}

#endif

ILIAS_NS_END