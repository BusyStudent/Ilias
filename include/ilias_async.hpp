#pragma once

#include "ilias.hpp"
#include <thread>
#include <atomic>
#include <mutex>
#include <map>

#if __cpp_lib_coroutine
    #include "ilias_co.hpp"
#endif

ILIAS_NS_BEGIN

enum PollEvent : int {
    In  = POLLIN,
    Out = POLLOUT,
    Err = POLLERR,
    All = In | Out | Err
};

/**
 * @brief A Interface to get notifyed
 * 
 */
class IOWatcher {
public:
    virtual void onEvent(int event) = 0;
    virtual void onTimeout() = 0;
protected:
    constexpr IOWatcher() = default;
    constexpr ~IOWatcher() = default;
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

    void addWatcher(IOWatcher* watcher, int events = PollEvent::All);
    bool modifyWatcher(IOWatcher* watcher, int events = PollEvent::All);
    bool removeWatcher(IOWatcher* watcher);

#ifdef __cpp_lib_coroutine
    Task<ssize_t> asyncRecv(SocketView socket, void* buffer, size_t length, int flags = 0);
    Task<ssize_t> asyncSend(SocketView socket, const void* buffer, size_t length, int flags = 0);
#endif

private:
    void _run();
    void _notify(::pollfd &pfd);

    std::thread           mThread; //< Threads for network poll
    std::atomic_bool      mRunning {true}; //< Flag to stop the thread
    std::vector<::pollfd> mPollfds;
    std::map<socket_t, IOWatcher*> mWatchers;
    std::recursive_mutex           mMutex;

    Socket mEvent; //< Socket poll in workThread
    Socket mControl; //< Socket for sending message
};

// --- C
class TcpServer {
public:

private:
    IOContext *mContext;
};

class TcpClient {
public:

private:
    IOContext *mContext;
};

// --- IOContext Impl
inline IOContext::IOContext() {
    // Init socket
#ifndef __linux
    Socket server(AF_INET, SOCK_STREAM, 0);
    server.listen();
    mControl = Socket(AF_INET, SOCK_STREAM, 0);
    mControl.connect(server.localEndpoint());
    mEvent = server.accept().first;
#else

#endif
    // Add event socket
}
inline IOContext::~IOContext() {
    mRunning = false;
    mThread.join();
}
inline void IOContext::_run() {
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
                _notify(*iter);
            }
        }
    }
}
inline void IOContext::_notify(::pollfd &pfd) {
    auto iter = mWatchers.find(pfd.fd);
    if (iter == mWatchers.end()) {
        return;
    }
    auto &watcher = iter->second;
    watcher->onEvent(pfd.revents);
    pfd.revents = 0;
}

ILIAS_NS_END