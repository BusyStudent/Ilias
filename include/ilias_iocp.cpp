#include "ilias_iocp.hpp"
#include "ilias_latch.hpp"
#include <memory>

ILIAS_NS_BEGIN

enum IOCPEvent : int {
    Recv     = 1 << 0,
    Recvfrom = 1 << 1,
    Send     = 1 << 2,
    Accept   = 1 << 3,
    Connect  = 1 << 4,
    Error    = 1 << 5
};

struct IOCPOverlapped : OVERLAPPED {
    int event = 0;
    WSABUF wsaBuf {0, 0};
    DWORD byteTrans = 0;
    socket_t sockfd = INVALID_SOCKET;
    socket_t peerfd = INVALID_SOCKET; //< Peer fd (used by AcceptEx)
    DWORD dwflag = 0;
    std::atomic_bool isCompleted {true}; //< Does it has a operation?

    uint8_t addressBuffer[(sizeof(::sockaddr_storage) + 16) * 2]; //< Internal Used address buffer
    int addressLength; //< address length (used by recvfrom)

    AcceptHandler AcceptCallback;
    ConnectHandler ConnectCallback;
    RecvfromHandler RecvfromCallback;
    Function<void(Expected<size_t, SockError>)> RecvSendCallback;

    void clear() {
        ::memset(static_cast<OVERLAPPED*>(this), 0, sizeof(OVERLAPPED));
        event = 0;
        wsaBuf.buf = 0;
        wsaBuf.len = 0;
        byteTrans = 0;
        peerfd = 0;
        dwflag = 0;
        isCompleted = true;
    }
};

inline IOCPContext::IOCPContext() {
    // Init iocp
    mIocpFd = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, mNumberOfCurrentThreads);
    ILIAS_ASSERT(mIocpFd != INVALID_HANDLE_VALUE);

    // Get Socket Ext
    Socket helper(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    GUID guidAcceptEx = WSAID_ACCEPTEX;
    DWORD dwBytes;
    auto rt = ::WSAIoctl(helper.get(),
                        SIO_GET_EXTENSION_FUNCTION_POINTER,
                        &guidAcceptEx, sizeof(guidAcceptEx),
                        &mFnAcceptEx, sizeof(mFnAcceptEx),
                        &dwBytes, nullptr, nullptr);
    if (rt == SOCKET_ERROR) {
        fprintf(stderr, "WSAIoctl get AcceptEx lfp failed with error: %d\n", WSAGetLastError());
        mFnAcceptEx = nullptr;
        ILIAS_ASSERT(false);
    }
    GUID guidGetAcceptExSocketAddress = WSAID_GETACCEPTEXSOCKADDRS;
    rt = ::WSAIoctl(helper.get(),
                    SIO_GET_EXTENSION_FUNCTION_POINTER,
                    &guidGetAcceptExSocketAddress, sizeof(guidGetAcceptExSocketAddress),
                    &mFnGetAcceptExSocketAddress, sizeof(mFnGetAcceptExSocketAddress),
                    &dwBytes, nullptr, nullptr);
    if (rt == SOCKET_ERROR) {
        fprintf(stderr, "WSAIoctl get GetAcceptExSocketAddress lfp failed with error: %d\n", WSAGetLastError());
        mFnGetAcceptExSocketAddress = nullptr;
        ILIAS_ASSERT(false);
    }
    GUID guidConnectEx = WSAID_CONNECTEX;
    rt = ::WSAIoctl(helper.get(),
                    SIO_GET_EXTENSION_FUNCTION_POINTER,
                    &guidConnectEx, sizeof(guidConnectEx),
                    &mFnConnectEx, sizeof(mFnConnectEx),
                    &dwBytes, nullptr, nullptr);
    if (rt == SOCKET_ERROR) {
        fprintf(stderr, "WSAIoctl get ConnectEx lfp failed with error: %d\n", WSAGetLastError());
        mFnConnectEx = nullptr;
        ILIAS_ASSERT(false);
    }
    GUID guidTransmitFile = WSAID_TRANSMITFILE;
    rt = ::WSAIoctl(helper.get(),
                    SIO_GET_EXTENSION_FUNCTION_POINTER,
                    &guidTransmitFile, sizeof(guidTransmitFile),
                    &mFnTransmitFile, sizeof(mFnTransmitFile),
                    &dwBytes, nullptr, nullptr);
    if (rt == SOCKET_ERROR) {
        fprintf(stderr, "WSAIoctl get TransmitFile lfp failed with error: %d\n", WSAGetLastError());
        mFnTransmitFile = nullptr;
        ILIAS_ASSERT(false);
    }

    mThread = std::thread(&IOCPContext::_run, this);
}
inline IOCPContext::~IOCPContext() {
    _stop();
    mThread.join();

    // Close IOCP
    ::CloseHandle(mIocpFd);
}

inline bool IOCPContext::asyncInitialize(SocketView socket) {
    std::lock_guard<std::mutex> lock(mMutex);
    auto ret = ::CreateIoCompletionPort((HANDLE)socket.get(), mIocpFd, 0, mNumberOfCurrentThreads);
    if (ret == nullptr) {
        return false;
    }
    auto iter = mIOCPOverlappedMap.find(socket.get());
    if (iter == mIOCPOverlappedMap.end()) {
        iter = mIOCPOverlappedMap.emplace(socket.get(), std::make_unique<IOCPOverlapped>()).first;
    }
    ::SetFileCompletionNotificationModes(
        (HANDLE)socket.get(), 
        FILE_SKIP_COMPLETION_PORT_ON_SUCCESS | FILE_SKIP_SET_EVENT_ON_HANDLE
    );
    auto overlapped = iter->second.get();

    overlapped->clear();
    overlapped->sockfd = socket.get();
    ::fprintf(stderr, "[Ilias::IOCPContext] asyncInitalize socket(%p)\n", socket.get());
    return true;
}

bool IOCPContext::asyncCleanup(SocketView sock) {
    std::lock_guard<std::mutex> lock(mMutex);
    auto iter = mIOCPOverlappedMap.find(sock.get());
    if (iter == mIOCPOverlappedMap.end()) {
        return false;
    }
    bool ret = true;
    if (!iter->second->isCompleted) {
        ret = ::CancelIoEx((HANDLE)sock.get(), nullptr);
    }
    mIOCPOverlappedMap.erase(iter);

    ::fprintf(stderr, "[Ilias::IOCPContext] asyncCleanup socket(%p)\n", sock.get());
    return ret;
}

bool IOCPContext::asyncCancel(SocketView socket, void *operation) {
    std::lock_guard<std::mutex> lock(mMutex);
    auto iter = mIOCPOverlappedMap.find(socket.get());
    if (iter == mIOCPOverlappedMap.end()) {
        return false;
    }
    if (operation != (void *)iter->second.get() && operation != nullptr) {
        return false;
    }
    return TRUE == ::CancelIoEx((HANDLE)socket.get(), iter->second.get());
}

void *IOCPContext::asyncConnect(SocketView socket, const IPEndpoint &ep,
                                    ConnectHandler &&callback) 
{
    std::lock_guard<std::mutex> lock(mMutex);
    auto iter = mIOCPOverlappedMap.find(socket.get());
    auto overlapped = iter->second.get();
    
    ILIAS_ASSERT(iter != mIOCPOverlappedMap.end());
    ILIAS_ASSERT(overlapped != nullptr);
    ILIAS_ASSERT(overlapped->isCompleted == true);
    ILIAS_ASSERT(callback != nullptr);

    overlapped->clear();
    overlapped->ConnectCallback = std::move(callback);
    overlapped->event = IOCPEvent::Connect;

    // Before ConnectEx, we need bind first
    ::sockaddr_storage storage {};
    ::memset(&storage, 0, sizeof(storage));
    storage.ss_family = socket.family();
    if (!socket.bind(storage)) {
        overlapped->ConnectCallback(Unexpected<SockError>(SockError::fromErrno()));
        overlapped->isCompleted = true;
        return nullptr;
    }

    if (!mFnConnectEx(socket.get(), &ep.data<sockaddr>(), ep.length(),
                        nullptr, 0, &overlapped->dwflag, overlapped)) {
        if (WSAGetLastError () != ERROR_IO_PENDING) {
            overlapped->ConnectCallback(Unexpected<SockError>(SockError::fromErrno()));
            overlapped->isCompleted = true;
            return nullptr;
        }
        else {
            return overlapped;
        }
    }
    // Got 
    _onEvent(overlapped);
    return nullptr;
}

void *IOCPContext::asyncAccept(
    SocketView socket,
    AcceptHandler &&callback)
{
    std::lock_guard<std::mutex> lock(mMutex);
    auto iter = mIOCPOverlappedMap.find(socket.get());
    auto overlapped = iter->second.get();

    ILIAS_ASSERT(iter != mIOCPOverlappedMap.end());
    ILIAS_ASSERT(overlapped != nullptr);
    ILIAS_ASSERT(overlapped->isCompleted == true);
    ILIAS_ASSERT(callback != nullptr);

    overlapped->clear();

    // Get socket family and type
    ::WSAPROTOCOL_INFO info;
    ::socklen_t len = sizeof(info);
    if (::getsockopt(socket.get(), SOL_SOCKET, SO_PROTOCOL_INFO, (char *)&info, &len) == SOCKET_ERROR) {
        overlapped->AcceptCallback(Unexpected<SockError>(SockError::fromErrno()));
        overlapped->isCompleted = true;
        return nullptr;
    }

    overlapped->AcceptCallback = std::move(callback);
    overlapped->event = IOCPEvent::Accept;
    overlapped->peerfd = ::socket(info.iAddressFamily, info.iSocketType, info.iProtocol);
    overlapped->wsaBuf.buf = (char*) overlapped->addressBuffer;
    overlapped->wsaBuf.len = sizeof(overlapped->addressBuffer);

    if (!mFnAcceptEx(socket.get(), overlapped->peerfd,
                        overlapped->wsaBuf.buf, 0, 
                        sizeof(sockaddr_storage) + 16,
                        sizeof(sockaddr_storage) + 16, &overlapped->byteTrans, overlapped))
    {
        auto error = ::WSAGetLastError();
        if (error != ERROR_IO_PENDING) {
            overlapped->AcceptCallback(Unexpected<SockError>(error));
            overlapped->isCompleted = true;
            return nullptr;
        } else {
            return overlapped;
        }
    }
    _onEvent(overlapped);
    return nullptr;
}

void *IOCPContext::asyncRecv(
    SocketView socket, void *buf, size_t n,
    RecvHandler &&callback) 
{
    std::lock_guard<std::mutex> lock(mMutex);
    auto iter = mIOCPOverlappedMap.find(socket.get());
    auto overlapped = iter->second.get();

    ILIAS_ASSERT(iter != mIOCPOverlappedMap.end());
    ILIAS_ASSERT(overlapped != nullptr);
    ILIAS_ASSERT(overlapped->isCompleted == true);
    ILIAS_ASSERT(callback != nullptr);
    ILIAS_ASSERT(buf != nullptr);
    ILIAS_ASSERT(n > 0);

    overlapped->clear();

    overlapped->RecvSendCallback = std::move(callback);
    overlapped->wsaBuf.buf = (char *)buf;
    overlapped->wsaBuf.len = n;
    overlapped->event = IOCPEvent::Recv;

    int ret = ::WSARecv(socket.get(), 
                &overlapped->wsaBuf, 1, 
                &overlapped->byteTrans, 
                &overlapped->dwflag, 
                overlapped, nullptr);

    if (ret == SOCKET_ERROR) {
        auto error = ::WSAGetLastError();
        if (error != ERROR_IO_PENDING) {
            overlapped->RecvSendCallback(Unexpected<SockError>(error));
            overlapped->isCompleted = true;
            return nullptr;
        }
        else {
            return overlapped;
        }
    }
    _onEvent(overlapped);
    return nullptr;
}

void *IOCPContext::asyncSend(
    SocketView socket, const void *buf, size_t n,
    SendHandler &&callback) 
{
    std::lock_guard<std::mutex> lock(mMutex);
    auto iter = mIOCPOverlappedMap.find(socket.get());
    auto overlapped = iter->second.get();

    ILIAS_ASSERT(iter != mIOCPOverlappedMap.end());
    ILIAS_ASSERT(overlapped != nullptr);
    ILIAS_ASSERT(overlapped->isCompleted == true);
    ILIAS_ASSERT(callback != nullptr);
    ILIAS_ASSERT(buf != nullptr);
    ILIAS_ASSERT(n > 0);

    overlapped->clear();

    overlapped->RecvSendCallback = std::move(callback);
    overlapped->wsaBuf.buf = (char *)buf;
    overlapped->wsaBuf.len = n;
    overlapped->event = IOCPEvent::Send;

    int ret = ::WSASend(socket.get(), 
                &overlapped->wsaBuf, 1, 
                &overlapped->byteTrans, 
                overlapped->dwflag, 
                overlapped, nullptr);

    if (ret == SOCKET_ERROR) {
        auto error = ::WSAGetLastError();
        if (error != ERROR_IO_PENDING) {
            overlapped->RecvSendCallback(Unexpected<SockError>(error));
            overlapped->isCompleted = true;
            return nullptr;
        } else {
            return overlapped;
        }
    }
    _onEvent(overlapped);
    return nullptr;
}
void *IOCPContext::asyncRecvfrom(
    SocketView socket,
    void *buf,
    size_t n,
    RecvfromHandler &&callback)
{

    std::lock_guard<std::mutex> lock(mMutex);
    auto iter = mIOCPOverlappedMap.find(socket.get());
    auto overlapped = iter->second.get();

    ILIAS_ASSERT(iter != mIOCPOverlappedMap.end());
    ILIAS_ASSERT(overlapped != nullptr);
    ILIAS_ASSERT(overlapped->isCompleted == true);
    ILIAS_ASSERT(callback != nullptr);
    ILIAS_ASSERT(buf != nullptr);
    ILIAS_ASSERT(n > 0);

    overlapped->clear();

    overlapped->RecvfromCallback = std::move(callback);
    overlapped->wsaBuf.buf = (char *)buf;
    overlapped->wsaBuf.len = n;
    overlapped->event = IOCPEvent::Recvfrom;

    int ret = ::WSARecvFrom(
        socket.get(),
        &overlapped->wsaBuf, 1,
        &overlapped->byteTrans,
        &overlapped->dwflag,
        (sockaddr*) overlapped->addressBuffer,
        &overlapped->addressLength,
        overlapped,
        nullptr
    );
    if (ret == SOCKET_ERROR) {
        auto error = ::WSAGetLastError();
        if (error != ERROR_IO_PENDING) {
            overlapped->RecvfromCallback(Unexpected<SockError>(error));
            overlapped->isCompleted = true;
            return nullptr;
        } else {
            return overlapped;
        }
    }

    _onEvent(overlapped);
    return nullptr;
}
void *IOCPContext::asyncSendto(
    SocketView socket,
    const void *buf,
    size_t n,
    const IPEndpoint &ep,
    SendtoHandler &&callback) {
    
    std::lock_guard<std::mutex> lock(mMutex);
    auto iter = mIOCPOverlappedMap.find(socket.get());
    auto overlapped = iter->second.get();

    ILIAS_ASSERT(iter != mIOCPOverlappedMap.end());
    ILIAS_ASSERT(overlapped != nullptr);
    ILIAS_ASSERT(overlapped->isCompleted == true);
    ILIAS_ASSERT(callback != nullptr);
    ILIAS_ASSERT(buf != nullptr);
    ILIAS_ASSERT(n > 0);

    overlapped->clear();

    overlapped->RecvSendCallback = std::move(callback);
    overlapped->wsaBuf.buf = (char *)buf;
    overlapped->wsaBuf.len = n;
    overlapped->event = IOCPEvent::Send;

    int ret = ::WSASendTo(socket.get(), 
                &overlapped->wsaBuf, 1, 
                &overlapped->byteTrans, 
                overlapped->dwflag, 
                &ep.data<::sockaddr>(),
                ep.length(),
                overlapped, nullptr);

    if (ret == SOCKET_ERROR) {
        auto error = ::WSAGetLastError();
        if (error != ERROR_IO_PENDING) {
            overlapped->RecvSendCallback(Unexpected<SockError>(error));
            overlapped->isCompleted = true;
            return nullptr;
        } else {
            return overlapped;
        }
    }
    _onEvent(overlapped);
    return nullptr;
}

inline void IOCPContext::_run() {
    IOCPOverlapped *data = nullptr;
    DWORD byteTrans = 0;
    void *completionKey = nullptr;

    while (true) {
        BOOL ret = ::GetQueuedCompletionStatus(mIocpFd, 
                                              &byteTrans, 
                                              (PULONG_PTR)&completionKey, 
                                              (LPOVERLAPPED*)&data,
                                              INFINITE);
        if (completionKey == nullptr && data == nullptr) {
            // Request Exit
            break;
        }
        if (data == nullptr) {
            continue;
        }
        ILIAS_ASSERT(data != nullptr);
        data->byteTrans = byteTrans;
        if (!ret) {
            data->event |= IOCPEvent::Error;
        }
        // Dispatch
        _dump(data);
        _onEvent(data);
    }
}

inline void IOCPContext::_dump(IOCPOverlapped *overlapped) {
#ifndef NDEBUG
    std::string events;
    if (overlapped->event & IOCPEvent::Recvfrom) {
        events += "Recvfrom ";
    }
    if (overlapped->event & IOCPEvent::Recv) {
        events += "Recv ";
    }
    if (overlapped->event & IOCPEvent::Send) {
        events += "Send ";
    }
    if (overlapped->event & IOCPEvent::Accept) {
        events += "Accept ";
    }
    if (overlapped->event & IOCPEvent::Connect) {
        events += "Connect ";
    }
    if (overlapped->event & IOCPEvent::Error) {
        events += "Error ";
    }
    ::printf(
        "[Ilias::IOCPContext] Get OVERLAPPED Result : Socket %lu Event %s\n",
        uintptr_t(overlapped->sockfd), 
        events.c_str()
    );
#endif
}

inline void IOCPContext::_onEvent(IOCPOverlapped *overlapped) {
    if (overlapped->event & IOCPEvent::Recv) {
        ILIAS_ASSERT(overlapped->RecvSendCallback != nullptr);
        if (overlapped->event & IOCPEvent::Error) {
            overlapped->RecvSendCallback(SockError::fromErrno());
        }
        else {
            overlapped->RecvSendCallback(overlapped->byteTrans);
        }
        overlapped->RecvSendCallback = nullptr;
    }
    else if (overlapped->event & IOCPEvent::Send) {
        ILIAS_ASSERT(overlapped->RecvSendCallback != nullptr);
        if (overlapped->event & IOCPEvent::Error) {
            overlapped->RecvSendCallback(SockError::fromErrno());
        }
        else {
            overlapped->RecvSendCallback(overlapped->byteTrans);
        }
        overlapped->RecvSendCallback = nullptr;
    }
    else if (overlapped->event & IOCPEvent::Accept) {
        ILIAS_ASSERT(overlapped->AcceptCallback != nullptr);
        if (overlapped->event & IOCPEvent::Error) {
            ::closesocket(overlapped->peerfd);
            overlapped->peerfd = INVALID_SOCKET;
            overlapped->AcceptCallback(Unexpected<SockError>(SockError::fromErrno()));
        }
        else {
            ILIAS_ASSERT(overlapped->peerfd != INVALID_SOCKET);
            sockaddr *remote, *local;
            int local_len, remote_len;
            mFnGetAcceptExSocketAddress(overlapped->wsaBuf.buf, 0, 
                                        sizeof(sockaddr_storage) + 16, sizeof(sockaddr_storage) + 16, 
                                        &local, &local_len,
                                         &remote, &remote_len);
            ::setsockopt(overlapped->peerfd, SOL_SOCKET, 
                        SO_UPDATE_ACCEPT_CONTEXT, 
                        (char*)&overlapped->sockfd, 
                        sizeof(overlapped->sockfd));
            overlapped->wsaBuf.buf = nullptr;
            overlapped->wsaBuf.len = 0;
            overlapped->AcceptCallback(
                std::make_pair<Socket, IPEndpoint>(
                    Socket(overlapped->peerfd), 
                    IPEndpoint::fromRaw(remote, remote_len)
                )
            );
        }
        overlapped->AcceptCallback = nullptr;
    } 
    else if (overlapped->event & IOCPEvent::Connect) {
        ILIAS_ASSERT(overlapped->ConnectCallback != nullptr);
        if (overlapped->event & IOCPEvent::Error) {
            overlapped->ConnectCallback(Unexpected<SockError>(SockError::fromErrno()));
        }
        else {
            ::setsockopt(overlapped->sockfd, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
            overlapped->ConnectCallback(Expected<void, SockError>());
        }
        overlapped->ConnectCallback = nullptr;
    } 
    else if (overlapped->event & IOCPEvent::Recvfrom) {
        ILIAS_ASSERT(overlapped->RecvfromCallback != nullptr);
        if (overlapped->event & IOCPEvent::Error) {
            overlapped->RecvfromCallback(Unexpected<SockError>(SockError::fromErrno()));
        }
        else {
            overlapped->RecvfromCallback(
                std::make_pair<size_t, IPEndpoint>(
                    overlapped->byteTrans,
                    IPEndpoint::fromRaw(overlapped->addressBuffer, overlapped->addressLength)
                )
            );
        }
        overlapped->RecvfromCallback = nullptr;
    }
    else {
        fprintf(stderr, "Unknown event: %d\n", overlapped->event);
        ILIAS_ASSERT(false);
        // overlapped->RecvSendCallback(SockError::fromErrno());
    }

    overlapped->isCompleted = true;
}
inline void IOCPContext::_stop() {
    auto ret = ::PostQueuedCompletionStatus(
        mIocpFd,
        0,
        0,
        nullptr
    );
}


ILIAS_NS_END