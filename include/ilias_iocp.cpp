#include "ilias_iocp.hpp"
#include "ilias_latch.hpp"

ILIAS_NS_BEGIN

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

    ::memset(&iter->second->overlap, 0, sizeof(iter->second->overlap));
    iter->second->isCompleted = true;
    iter->second->sockfd = socket.get();
    iter->second->RecvSendCallback = nullptr;
    iter->second->AcceptCallback = nullptr;
    iter->second->ConnectCallback = nullptr;
    iter->second->wsaBuf.buf = nullptr;
    iter->second->wsaBuf.len = 0;
    ::fprintf(stderr, "[Ilias::IOCPContext] asyncInitalize socket(%p)\n", socket.get());
    _dump();
    return true;
}

bool IOCPContext::asyncCleanup(SocketView sock) {
    std::lock_guard<std::mutex> lock(mMutex);
    auto iter = mIOCPOverlappedMap.find(sock.get());
    if (iter == mIOCPOverlappedMap.end()) {
        return false;
    }
    auto ret = ::CancelIoEx((HANDLE)sock.get(), nullptr);
    mIOCPOverlappedMap.erase(iter);

    ::fprintf(stderr, "[Ilias::IOCPContext] asyncCleanup socket(%p)\n", sock.get());
    return (ret == TRUE);
}

bool IOCPContext::asyncCancel(SocketView socket, void *operation) {
    std::lock_guard<std::mutex> lock(mMutex);
    auto iter = mIOCPOverlappedMap.find(socket.get());
    if (iter == mIOCPOverlappedMap.end()) {
        return false;
    }
    if (operation != (void *)iter->second.get()) {
        return false;
    }
    return TRUE == ::CancelIoEx((HANDLE)socket.get(), (LPOVERLAPPED)iter->second.get());
}

void *IOCPContext::asyncConnect(SocketView socket, const IPEndpoint &ep,
                                    ConnectHandler &&callback) {
    std::lock_guard<std::mutex> lock(mMutex);
    auto iter = mIOCPOverlappedMap.find(socket.get());
    
    ILIAS_ASSERT(iter != mIOCPOverlappedMap.end());
    ILIAS_ASSERT(iter->second != nullptr);
    ILIAS_ASSERT(iter->second->isCompleted == true);
    ILIAS_ASSERT(callback != nullptr);

    ::memset(&iter->second->overlap, 0, sizeof(iter->second->overlap));

    iter->second->isCompleted = false;
    iter->second->ConnectCallback = std::move(callback);
    iter->second->event = IOCPEvent::Connect;
    iter->second->dwflag = 0;

    // Before ConnectEx, we need bind first
    ::sockaddr_storage storage {};
    ::memset(&storage, 0, sizeof(storage));
    storage.ss_family = socket.family();
    if (!socket.bind(storage)) {
        iter->second->ConnectCallback(Unexpected<SockError>(SockError::fromErrno()));
        iter->second->isCompleted = true;
        return nullptr;
    }

    if (!mFnConnectEx(socket.get(), &ep.data<sockaddr>(), ep.length(),
                        nullptr, 0, &iter->second->dwflag, (LPOVERLAPPED)iter->second.get())) {
        if (WSAGetLastError () != ERROR_IO_PENDING) {
            iter->second->ConnectCallback(Unexpected<SockError>(SockError::fromErrno()));
            iter->second->isCompleted = true;
            return nullptr;
        } else {
            return (void *)iter->second.get();
        }
    }
    ::setsockopt(iter->second->sockfd, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
    iter->second->ConnectCallback(Expected<void, SockError>());
    iter->second->isCompleted = true;
    return nullptr;
}

void *IOCPContext::asyncAccept(
    SocketView socket,
    AcceptHandler &&callback) {
    std::lock_guard<std::mutex> lock(mMutex);
    auto iter = mIOCPOverlappedMap.find(socket.get());

    ILIAS_ASSERT(iter != mIOCPOverlappedMap.end());
    ILIAS_ASSERT(iter->second != nullptr);
    ILIAS_ASSERT(iter->second->isCompleted == true);
    ILIAS_ASSERT(callback != nullptr);

    ::memset(&iter->second->overlap, 0, sizeof(iter->second->overlap));

    // Get socket family and type
    ::WSAPROTOCOL_INFO info;
    ::socklen_t len = sizeof(info);
    if (::getsockopt(socket.get(), SOL_SOCKET, SO_PROTOCOL_INFO, (char *)&info, &len) == SOCKET_ERROR) {
        iter->second->AcceptCallback(Unexpected<SockError>(SockError::fromErrno()));
        iter->second->isCompleted = true;
        return nullptr;
    }

    iter->second->AcceptCallback = std::move(callback);
    iter->second->event = IOCPEvent::Accept;
    iter->second->isCompleted = false;
    iter->second->peerfd = ::socket(info.iAddressFamily, info.iSocketType, info.iProtocol);
    iter->second->wsaBuf.buf = new char[(sizeof(sockaddr_storage) + 16) * 2];
    iter->second->wsaBuf.len = (sizeof(sockaddr_storage) + 16) * 2;

    if (!mFnAcceptEx(socket.get(), iter->second->peerfd,
                        iter->second->wsaBuf.buf, 0, 
                        sizeof(sockaddr_storage) + 16,
                        sizeof(sockaddr_storage) + 16, &iter->second->byteTrans, (LPOVERLAPPED)iter->second.get()))
    {
        auto error = ::WSAGetLastError();
        if (error != ERROR_IO_PENDING) {
            iter->second->AcceptCallback(Unexpected<SockError>(error));
            iter->second->isCompleted = true;
            return nullptr;
        } else {
            return (void *)iter->second.get();
        }
    }
    sockaddr *remote, *local;
    int local_len, remote_len;
    mFnGetAcceptExSocketAddress(iter->second->wsaBuf.buf, 0, 
                                sizeof(sockaddr_storage) + 16, sizeof(sockaddr_storage) + 16, 
                                &local, &local_len,
                                    &remote, &remote_len);
    setsockopt(iter->second->peerfd, SOL_SOCKET, 
                SO_UPDATE_ACCEPT_CONTEXT, 
                (char*)&iter->second->sockfd, 
                sizeof(iter->second->sockfd));
    delete[] iter->second->wsaBuf.buf;
    iter->second->wsaBuf.buf = nullptr;
    iter->second->wsaBuf.len = 0;
    iter->second->AcceptCallback(std::make_pair<Socket, IPEndpoint>(Socket(iter->second->peerfd), 
                                                                    IPEndpoint::fromRaw(remote, remote_len)));
    iter->second->isCompleted = true;
    return nullptr;
}

void *IOCPContext::asyncRecv(
    SocketView socket, void *buf, size_t n,
    RecvHandler &&callback) {
    std::lock_guard<std::mutex> lock(mMutex);
    auto iter = mIOCPOverlappedMap.find(socket.get());

    ILIAS_ASSERT(iter != mIOCPOverlappedMap.end());
    ILIAS_ASSERT(iter->second != nullptr);
    ILIAS_ASSERT(iter->second->isCompleted == true);
    ILIAS_ASSERT(callback != nullptr);
    ILIAS_ASSERT(buf != nullptr);
    ILIAS_ASSERT(n > 0);

    ::memset(&iter->second->overlap, 0, sizeof(iter->second->overlap));

    iter->second->isCompleted = false;
    iter->second->RecvSendCallback = std::move(callback);
    iter->second->wsaBuf.buf = (char *)buf;
    iter->second->wsaBuf.len = n;
    iter->second->dwflag = 0;
    iter->second->byteTrans = 0;
    iter->second->event = IOCPEvent::Read;

    int ret = ::WSARecv(socket.get(), 
                &iter->second->wsaBuf, 1, 
                &iter->second->byteTrans, 
                &iter->second->dwflag, 
                &iter->second->overlap, nullptr);

    if (ret == SOCKET_ERROR) {
        auto error = ::WSAGetLastError();
        if (error != ERROR_IO_PENDING) {
            iter->second->RecvSendCallback(Unexpected<SockError>(error));
            iter->second->isCompleted = true;
            return nullptr;
        } else {
            return (void *)iter->second.get();
        }
    }
    iter->second->RecvSendCallback(iter->second->byteTrans);
    iter->second->isCompleted = true;
    return nullptr;
}

void *IOCPContext::asyncSend(
    SocketView socket, const void *buf, size_t n,
    SendHandler &&callback) {
    std::lock_guard<std::mutex> lock(mMutex);
    auto iter = mIOCPOverlappedMap.find(socket.get());

    ILIAS_ASSERT(iter != mIOCPOverlappedMap.end());
    ILIAS_ASSERT(iter->second != nullptr);
    ILIAS_ASSERT(iter->second->isCompleted == true);
    ILIAS_ASSERT(callback != nullptr);
    ILIAS_ASSERT(buf != nullptr);
    ILIAS_ASSERT(n > 0);

    ::memset(&iter->second->overlap, 0, sizeof(iter->second->overlap));

    iter->second->isCompleted = false;
    iter->second->RecvSendCallback = std::move(callback);
    iter->second->wsaBuf.buf = (char *)buf;
    iter->second->wsaBuf.len = n;
    iter->second->dwflag = 0;
    iter->second->byteTrans = 0;
    iter->second->event = IOCPEvent::Write;

    int ret = ::WSASend(socket.get(), 
                &iter->second->wsaBuf, 1, 
                &iter->second->byteTrans, 
                iter->second->dwflag, 
                &iter->second->overlap, nullptr);

    if (ret == SOCKET_ERROR) {
        auto error = ::WSAGetLastError();
        if (error != ERROR_IO_PENDING) {
            iter->second->RecvSendCallback(Unexpected<SockError>(error));
            iter->second->isCompleted = true;
            return nullptr;
        } else {
            return (void *)iter->second.get();
        }
    }
    iter->second->RecvSendCallback(iter->second->byteTrans);
    iter->second->isCompleted = true;
    return nullptr;
}
void *IOCPContext::asyncRecvfrom(
    SocketView socket,
    void *buf,
    size_t n,
    RecvfromHandler &&callback) {

    std::lock_guard<std::mutex> lock(mMutex);
    auto iter = mIOCPOverlappedMap.find(socket.get());

    ILIAS_ASSERT(iter != mIOCPOverlappedMap.end());
    ILIAS_ASSERT(iter->second != nullptr);
    ILIAS_ASSERT(iter->second->isCompleted == true);
    ILIAS_ASSERT(callback != nullptr);
    ILIAS_ASSERT(buf != nullptr);
    ILIAS_ASSERT(n > 0);

    ::memset(&iter->second->overlap, 0, sizeof(iter->second->overlap));

    iter->second->isCompleted = false;
    iter->second->RecvfromCallback = std::move(callback);
    iter->second->wsaBuf.buf = (char *)buf;
    iter->second->wsaBuf.len = n;
    iter->second->dwflag = 0;
    iter->second->byteTrans = 0;
    iter->second->event = IOCPEvent::ReadFrom;

    int ret = ::WSARecvFrom(
        socket.get(),
        &iter->second->wsaBuf, 1,
        &iter->second->byteTrans,
        &iter->second->dwflag,
        (sockaddr*) iter->second->addressBuffer,
        &iter->second->addressLength,
        &iter->second->overlap,
        nullptr
    );
    if (ret == SOCKET_ERROR) {
        auto error = ::WSAGetLastError();
        if (error != ERROR_IO_PENDING) {
            iter->second->RecvfromCallback(Unexpected<SockError>(error));
            iter->second->isCompleted = true;
            return nullptr;
        } else {
            return (void *)iter->second.get();
        }
    }

    iter->second->RecvfromCallback(
        std::make_pair<size_t, IPEndpoint>(
            ret,
            IPEndpoint::fromRaw(iter->second->addressBuffer, iter->second->addressLength)
        )
    );
    iter->second->isCompleted = true;
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

    ILIAS_ASSERT(iter != mIOCPOverlappedMap.end());
    ILIAS_ASSERT(iter->second != nullptr);
    ILIAS_ASSERT(iter->second->isCompleted == true);
    ILIAS_ASSERT(callback != nullptr);
    ILIAS_ASSERT(buf != nullptr);
    ILIAS_ASSERT(n > 0);

    ::memset(&iter->second->overlap, 0, sizeof(iter->second->overlap));

    iter->second->isCompleted = false;
    iter->second->RecvSendCallback = std::move(callback);
    iter->second->wsaBuf.buf = (char *)buf;
    iter->second->wsaBuf.len = n;
    iter->second->dwflag = 0;
    iter->second->byteTrans = 0;
    iter->second->event = IOCPEvent::Write;

    int ret = ::WSASendTo(socket.get(), 
                &iter->second->wsaBuf, 1, 
                &iter->second->byteTrans, 
                iter->second->dwflag, 
                &ep.data<::sockaddr>(),
                ep.length(),
                &iter->second->overlap, nullptr);

    if (ret == SOCKET_ERROR) {
        auto error = ::WSAGetLastError();
        if (error != ERROR_IO_PENDING) {
            iter->second->RecvSendCallback(Unexpected<SockError>(error));
            iter->second->isCompleted = true;
            return nullptr;
        } else {
            return (void *)iter->second.get();
        }
    }
    iter->second->RecvSendCallback(iter->second->byteTrans);
    iter->second->isCompleted = true;
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
        _onEvent(data);
    }
}

inline void IOCPContext::_dump() {
#ifndef NDEBUG
    ::printf("[Ilias::IOCPContext] Dump Watchers\n");
    for (const auto &pfd : mIOCPOverlappedMap) {
        std::string events;
        if (pfd.second->event & IOCPEvent::Read) {
            events += "In ";
        }
        if (pfd.second->event & IOCPEvent::Write) {
            events += "Out ";
        }
        if (pfd.second->event & IOCPEvent::Accept) {
            events += "Accept ";
        }
        if (pfd.second->event & IOCPEvent::Connect) {
            events += "Accept ";
        }
        ::printf(
            "[Ilias::IOCPContext] Socket %lu Event %s\n",
            uintptr_t(pfd.first), 
            events.c_str()
        );
    }
#endif
}

inline void IOCPContext::_onEvent(IOCPOverlapped *overlapped) {
    if (overlapped->event & IOCPEvent::Read) {
        ILIAS_ASSERT(overlapped->RecvSendCallback != nullptr);
        if (overlapped->event & IOCPEvent::Error) {
            overlapped->RecvSendCallback(SockError::fromErrno());
        } else {
            overlapped->RecvSendCallback(overlapped->byteTrans);
        }
        overlapped->RecvSendCallback = nullptr;
    } else if (overlapped->event & IOCPEvent::Write) {
        ILIAS_ASSERT(overlapped->RecvSendCallback != nullptr);
        if (overlapped->event & IOCPEvent::Error) {
            overlapped->RecvSendCallback(SockError::fromErrno());
        } else {
            overlapped->RecvSendCallback(overlapped->byteTrans);
        }
        overlapped->RecvSendCallback = nullptr;
    } else if (overlapped->event & IOCPEvent::Accept) {
        ILIAS_ASSERT(overlapped->AcceptCallback != nullptr);
        if (overlapped->event & IOCPEvent::Error) {
            overlapped->AcceptCallback(Unexpected<SockError>(SockError::fromErrno()));
        } else {
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
            delete[] overlapped->wsaBuf.buf;
            overlapped->wsaBuf.buf = nullptr;
            overlapped->wsaBuf.len = 0;
            overlapped->AcceptCallback(std::make_pair<Socket, IPEndpoint>(Socket(overlapped->peerfd), IPEndpoint::fromRaw(remote, remote_len)));
        }
        overlapped->AcceptCallback = nullptr;
    } else if (overlapped->event & IOCPEvent::Connect) {
        ILIAS_ASSERT(overlapped->ConnectCallback != nullptr);
        if (overlapped->event & IOCPEvent::Error) {
            overlapped->ConnectCallback(Unexpected<SockError>(SockError::fromErrno()));
        } else {
            ::setsockopt(overlapped->sockfd, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
            overlapped->ConnectCallback(Expected<void, SockError>());
        }
        overlapped->ConnectCallback = nullptr;
    } else if (overlapped->event & IOCPEvent::ReadFrom) {
        ILIAS_ASSERT(overlapped->RecvfromCallback != nullptr);
        if (overlapped->event & IOCPEvent::Error) {
            overlapped->RecvfromCallback(Unexpected<SockError>(SockError::fromErrno()));
        } else {
            overlapped->RecvfromCallback(
                std::make_pair<size_t, IPEndpoint>(
                    overlapped->byteTrans,
                    IPEndpoint::fromRaw(overlapped->addressBuffer, overlapped->addressLength)
                )
            );
        }
        overlapped->RecvfromCallback = nullptr;
    } else {
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