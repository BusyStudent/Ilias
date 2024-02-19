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
protected:
    constexpr IOWatcher() = default;
    constexpr ~IOWatcher() = default;
};

/**
 * @brief A Context for add watcher 
 * 
 */
class IOContext {
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

    std::thread           mThread; //< Threads for network poll
    std::atomic<bool>     mRunning {true}; //< Flag to stop the thread
    std::vector<::pollfd> mPollfds;
    std::map<socket_t, IOWatcher*> mWatchers;
    std::recursive_mutex           mMutex;
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
inline IOContext::IOContext() : mThread(&IOContext::_run, this) {
    while (mRunning) {
        auto ret = ILIAS_POLL(mPollfds.data(), mPollfds.size(), -1);
    }
}
inline IOContext::~IOContext() {
    mThread.join();
}

ILIAS_NS_END