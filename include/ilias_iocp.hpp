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

struct IOCPOverlapped;
/**
 * @brief A Context for Windows IOCP
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
                            int64_t timeout,
                            ConnectHandler &&callback) override;
    void *asyncAccept(SocketView socket,
                        int64_t timeout,
                        AcceptHandler &&callback) override;
    void *asyncRecv(SocketView socket,
                    void *buf,
                    size_t n,
                    int64_t timeout,
                    RecvHandler &&callback) override;
    void *asyncSend(SocketView socket,
                    const void *buf,
                    size_t n,
                    int64_t timeout,
                    SendHandler &&callback) override;
    void *asyncRecvfrom(
                    SocketView socket,
                    void *buf,
                    size_t n,
                    int64_t timeout,
                    RecvfromHandler &&callback) override;
    void *asyncSendto(
                    SocketView socket,
                    const void *buf,
                    size_t n,
                    const IPEndpoint &ep,
                    int64_t timeout,
                    SendtoHandler &&callback) override;
private:
    void _run();
    void _stop();
    void _wakeup();
    void _onEvent(IOCPOverlapped *overlapped);
    void _dump(IOCPOverlapped *overlapped);
    void _addTimeout(IOCPOverlapped *overlapped, int64_t timeout);
    void _notifyTimeout();
    DWORD _waitDuration();
    
    SockInitializer       mInitalizer;
    LPFN_GETACCEPTEXSOCKADDRS mFnGetAcceptExSocketAddress = nullptr;
    LPFN_ACCEPTEX mFnAcceptEx = nullptr;
    LPFN_CONNECTEX mFnConnectEx = nullptr;
    LPFN_TRANSMITFILE mFnTransmitFile = nullptr;

    std::thread      mThread; //< Threads for network poll
    std::mutex       mMutex;
    std::atomic_bool mRunning {true};
    std::map<socket_t, std::unique_ptr<IOCPOverlapped> > mIOCPOverlappedMap;
    std::multimap<uint64_t, IOCPOverlapped*> mTimeoutQueue; //< Maps for manage timeouted
    HANDLE mIocpFd = INVALID_HANDLE_VALUE; //< iocp fd
    const int mNumberOfCurrentThreads = 16;
};

// using IOContext = IOCPContext;

ILIAS_NS_END
