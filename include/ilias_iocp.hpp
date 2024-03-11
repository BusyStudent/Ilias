#pragma once

#include "ilias.hpp"
#include "ilias_expected.hpp"
#include "ilias_backend.hpp"

#include <Windows.h>
#include <WinSock2.h>
#include <MSWSock.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <map>

#pragma comment(lib, "mswsock.lib")

ILIAS_NS_BEGIN

enum IOCPEvent : int {
    Read     = 1 << 0,
    ReadFrom = 1 << 1,
    Write    = 1 << 2,
    Accept   = 1 << 3,
    Connect  = 1 << 4,
    Error    = 1 << 5
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

    uint8_t addressBuffer[sizeof(::sockaddr_storage) + 16]; //< Internal Used address buffer
    int addressLength; //< address length (used by recvfrom)

    AcceptHandler AcceptCallback;
    ConnectHandler ConnectCallback;
    RecvfromHandler RecvfromCallback;
    Function<void(Expected<size_t, SockError>)> RecvSendCallback;
};

/**
 * @brief A Context for add watcher 
 * 
 */
class IOCPContext final : public IOContext {
public:
    IOCPContext();
    IOCPContext(const IOCPContext&) = delete;
    ~IOCPContext();

    bool asyncInitialize(SocketView socket) override;
    bool asyncCleanup(SocketView sock) override;

    bool asyncCancel(SocketView socket, void *operation) override;
    void *asyncConnect(SocketView socket,
                            const IPEndpoint &ep, 
                            ConnectHandler &&callback) override;
    void *asyncAccept(SocketView socket,
                        AcceptHandler &&callback) override;
    void *asyncRecv(SocketView socket,
                    void *buf,
                    size_t n,
                    RecvHandler &&callback) override;
    void *asyncSend(SocketView socket,
                    const void *buf,
                    size_t n,
                    SendHandler &&callback) override;
    void *asyncRecvfrom(
                    SocketView socket,
                    void *buf,
                    size_t n,
                    RecvfromHandler &&callback) override;
    void *asyncSendto(
                    SocketView socket,
                    const void *buf,
                    size_t n,
                    const IPEndpoint &ep,
                    SendtoHandler &&callback) override;
private:
    void _run();
    void _stop();
    void _dump();
    void _onEvent(IOCPOverlapped *overlapped);
    
    SockInitializer       mInitalizer;
    LPFN_GETACCEPTEXSOCKADDRS mFnGetAcceptExSocketAddress = nullptr;
    LPFN_ACCEPTEX mFnAcceptEx = nullptr;
    LPFN_CONNECTEX mFnConnectEx = nullptr;

    std::thread           mThread; //< Threads for network poll
    std::mutex            mMutex;
    std::map<socket_t, std::unique_ptr<IOCPOverlapped>> mIOCPOverlappedMap;
    HANDLE mIocpFd = INVALID_HANDLE_VALUE; //< iocp fd
    const int mNumberOfCurrentThreads = 16;
};

// using IOContext = IOCPContext;

ILIAS_NS_END
