#pragma once

#include "ilias_async.hpp"
#include "ilias.hpp"
#include "ilias_expected.hpp"

#include <Windows.h>
#include <WinSock2.h>
#include <MSWSock.h>
#include <atomic>

#pragma comment(lib, "mswsock.lib")

ILIAS_NS_BEGIN

enum IOCPEvent : int {
    Timeout = -1,
    None = 0,
    Read = 1 << 0,
    Write = 1 << 1,
    Accept = 1 << 2,
    Connect = 1 << 3,
    Error = 1 << 4
};

struct IOCPOverlapped {
    OVERLAPPED overlap;
    int event;
    WSABUF wsaBuf;
    DWORD byteTrans;
    socket_t sockfd;
    socket_t peerfd;
    DWORD dwflag;
    std::atomic_bool isCompleted;
    std::function<void(expected<std::pair<Socket, IPEndpoint>, SockError>)> AcceptCallback;
    std::function<void(expected<void, SockError>)> ConnectCallback;
    std::function<void(expected<size_t, SockError>)> RecvSendCallback;
};

/**
 * @brief A Context for add watcher 
 * 
 */
class IOCPContext {
public:
    IOCPContext();
    IOCPContext(const IOCPContext&) = delete;
    ~IOCPContext();

    bool asyncInitalize(SocketView socket);
    bool asyncCleanup(SocketView sock);

    bool asyncCancel(SocketView socket, void *operation);
    void *asyncConnect(SocketView socket,
                            const IPEndpoint &ep, 
                            std::function<void(expected<void, SockError>)> &&callback);
    void *asyncAccept(SocketView socket,
                        std::function<void(expected<std::pair<Socket, IPEndpoint>, SockError>)> &&callback);
    void *asyncRecv(SocketView socket,
                    void *buf,
                    size_t n,
                    std::function<void(expected<size_t, SockError>)> &&callback);
    void *asyncSend(SocketView socket,
                    const void *buf,
                    size_t n,
                    std::function<void(expected<size_t, SockError>)> &&callback);
private:
    struct Fn {
        void (*fn)(void *);
        void *arg;
    };

    void _run();
    void _stop();
    void _dump();
    void _invoke(Fn);
    template <typename T>
    void _invoke4(T &&callable);
    void onEvent(IOCPOverlapped *overlapped);
    
    SockInitializer       mInitalizer;
    LPFN_GETACCEPTEXSOCKADDRS mFnGetAcceptExSocketAddress = nullptr;
    LPFN_ACCEPTEX mFnAcceptEx = nullptr;
    LPFN_CONNECTEX mFnConnectEx = nullptr;

    std::thread           mThread; //< Threads for network poll
    std::atomic_bool      mRunning {true}; //< Flag to stop the thread
    std::mutex            mMutex;
    std::map<socket_t, std::unique_ptr<IOCPOverlapped>> mIOCPOverlappedMap;
    HANDLE mIocpFd = INVALID_HANDLE_VALUE; //< iocp fd
    const int mNumberOfCurrentThreads = 16;

    Socket mEvent; //< Socket poll in workThread
    Socket mControl; //< Socket for sending message
};

using IOContext = IOCPContext;

ILIAS_NS_END
