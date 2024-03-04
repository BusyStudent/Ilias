#include "ilias_iocp.hpp"

ILIAS_NS_BEGIN

inline IOCPContext::IOCPContext() {
    // Init iocp
    mIocpFd = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, mNumberOfCurrentThreads);
    ILIAS_ASSERT(mIocpFd != INVALID_HANDLE_VALUE);
    // Init socket
    Socket server(AF_INET, SOCK_STREAM, 0);
    server.bind(IPEndpoint(IPAddress4::loopback(), 0));
    server.listen();
    mControl = Socket(AF_INET, SOCK_STREAM, 0);
    mControl.connect(server.localEndpoint());
    mEvent = server.accept().first;
    mEvent.setBlocking(false);

    GUID GuidAcceptEx = WSAID_ACCEPTEX;
    DWORD dwBytes;
    auto rt = WSAIoctl( mEvent.get(),
                        SIO_GET_EXTENSION_FUNCTION_POINTER,
                        &GuidAcceptEx, sizeof(GuidAcceptEx),
                        &mFnAcceptEx, sizeof(mFnAcceptEx),
                        &dwBytes, nullptr, nullptr);
    if (rt == SOCKET_ERROR) {
        fprintf(stderr, "WSAIoctl get AcceptEx lfp failed with error: %d\n", WSAGetLastError());
        mFnAcceptEx = nullptr;
        ILIAS_ASSERT(false);
    }
    GUID GuidGetAcceptExSocketAddress = WSAID_GETACCEPTEXSOCKADDRS;
    rt = WSAIoctl(  mEvent.get(),
                    SIO_GET_EXTENSION_FUNCTION_POINTER,
                    &GuidGetAcceptExSocketAddress, sizeof(GuidGetAcceptExSocketAddress),
                    &mFnGetAcceptExSocketAddress, sizeof(mFnGetAcceptExSocketAddress),
                    &dwBytes, nullptr, nullptr);
    if (rt == SOCKET_ERROR) {
        fprintf(stderr, "WSAIoctl get GetAcceptExSocketAddress lfp failed with error: %d\n", WSAGetLastError());
        mFnGetAcceptExSocketAddress = nullptr;
        ILIAS_ASSERT(false);
    }
    GUID GuidConnectEx = WSAID_CONNECTEX;
    rt = WSAIoctl(  mEvent.get(),
                    SIO_GET_EXTENSION_FUNCTION_POINTER,
                    &GuidConnectEx, sizeof(GuidConnectEx),
                    &mFnConnectEx, sizeof(mFnConnectEx),
                    &dwBytes, nullptr, nullptr);
    if (rt == SOCKET_ERROR) {
        fprintf(stderr, "WSAIoctl get ConnectEx lfp failed with error: %d\n", WSAGetLastError());
        mFnConnectEx = nullptr;
        ILIAS_ASSERT(false);
    }

    // Add event socket
    asyncInitalize(mEvent);
    // TODO:read event
    // asyncRecv(socket, buf, n)
    // Start Work ThrearegistWatcher = std::thread(&IOCPContext:addWatcher
}
inline IOCPContext::~IOCPContext() {
    _stop();
    mThread.join();
}

inline bool IOCPContext::asyncInitalize(SocketView socket) {
    std::lock_guard<std::mutex> lock(mMutex);
    auto ret = ::CreateIoCompletionPort((HANDLE)socket.get(), mIocpFd, 0, mNumberOfCurrentThreads);
    if (ret == nullptr) {
        return false;
    }
    auto iter = mIOCPOverlappedMap.find(socket.get());
    if (iter == mIOCPOverlappedMap.end()) {
        iter = mIOCPOverlappedMap.emplace(socket.get(), std::make_unique<IOCPOverlapped>()).first;
    }
    SetFileCompletionNotificationModes((HANDLE)socket.get(), FILE_SKIP_COMPLETION_PORT_ON_SUCCESS);

    memset(&iter->second->overlap, 0, sizeof(iter->second->overlap));
    iter->second->isCompleted = true;
    iter->second->sockfd = socket.get();
    iter->second->RecvSendCallback = nullptr;
    iter->second->AcceptCallback = nullptr;
    iter->second->ConnectCallback = nullptr;
    iter->second->wsaBuf.buf = nullptr;
    iter->second->wsaBuf.len = 0;
    fprintf(stderr, "IOCPContext::asyncInitalize socket(%p)\n", socket.get());
    _dump();
    return true;
}

bool IOCPContext::asyncCleanup(SocketView sock) {
    std::lock_guard<std::mutex> lock(mMutex);
    auto iter = mIOCPOverlappedMap.find(sock.get());
    if (iter == mIOCPOverlappedMap.end()) {
        return false;
    }
    auto ret = CancelIoEx((HANDLE)sock.get(), nullptr);
    mIOCPOverlappedMap.erase(iter);
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
    return TRUE == CancelIoEx((HANDLE)socket.get(), (LPOVERLAPPED)iter->second.get());
}

void *IOCPContext::asyncConnect(SocketView socket, const IPEndpoint &ep,
                                    std::function<void(Expected<void, SockError>)> &&callback) {
    std::lock_guard<std::mutex> lock(mMutex);
    auto iter = mIOCPOverlappedMap.find(socket.get());
    
    ILIAS_ASSERT(iter != mIOCPOverlappedMap.end());
    ILIAS_ASSERT(iter->second != nullptr);
    ILIAS_ASSERT(iter->second->isCompleted == true);
    ILIAS_ASSERT(callback != nullptr);

    memset(&iter->second->overlap, 0, sizeof(iter->second->overlap));

    iter->second->isCompleted = false;
    iter->second->ConnectCallback = std::move(callback);
    iter->second->event = IOCPEvent::Connect;
    iter->second->dwflag = 0;
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
    setsockopt(iter->second->sockfd, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
    iter->second->ConnectCallback(Expected<void, SockError>());
    iter->second->isCompleted = true;
    return nullptr;
}

void *IOCPContext::asyncAccept(
    SocketView socket,
    std::function<void(Expected<std::pair<Socket, IPEndpoint>, SockError>)>
        &&callback) {
    std::lock_guard<std::mutex> lock(mMutex);
    auto iter = mIOCPOverlappedMap.find(socket.get());

    ILIAS_ASSERT(iter != mIOCPOverlappedMap.end());
    ILIAS_ASSERT(iter->second != nullptr);
    ILIAS_ASSERT(iter->second->isCompleted == true);
    ILIAS_ASSERT(callback != nullptr);

    memset(&iter->second->overlap, 0, sizeof(iter->second->overlap));

    iter->second->AcceptCallback = std::move(callback);
    iter->second->event = IOCPEvent::Accept;
    iter->second->isCompleted = false;
    iter->second->peerfd = Socket::create(AF_INET, SOCK_STREAM, 0).release();
    iter->second->wsaBuf.buf = new char[(sizeof(sockaddr_storage) + 16) * 2];
    iter->second->wsaBuf.len = (sizeof(sockaddr_storage) + 16) * 2;

    if (!mFnAcceptEx(socket.get(), iter->second->peerfd,
                        iter->second->wsaBuf.buf, 0, 
                        sizeof(sockaddr_storage) + 16,
                        sizeof(sockaddr_storage) + 16, &iter->second->byteTrans, (LPOVERLAPPED)iter->second.get()))
    {
        if (WSAGetLastError () != ERROR_IO_PENDING) {
            iter->second->AcceptCallback(Unexpected<SockError>(SockError::fromErrno()));
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
    std::function<void(Expected<size_t, SockError>)> &&callback) {
    std::lock_guard<std::mutex> lock(mMutex);
    auto iter = mIOCPOverlappedMap.find(socket.get());

    ILIAS_ASSERT(iter != mIOCPOverlappedMap.end());
    ILIAS_ASSERT(iter->second != nullptr);
    ILIAS_ASSERT(iter->second->isCompleted == true);
    ILIAS_ASSERT(callback != nullptr);
    ILIAS_ASSERT(buf != nullptr);
    ILIAS_ASSERT(n > 0);

    memset(&iter->second->overlap, 0, sizeof(iter->second->overlap));

    iter->second->isCompleted = false;
    iter->second->RecvSendCallback = std::move(callback);
    iter->second->wsaBuf.buf = (char *)buf;
    iter->second->wsaBuf.len = n;
    iter->second->dwflag = 0;
    iter->second->byteTrans = 0;

    if (!WSARecv(socket.get(), 
                &iter->second->wsaBuf, 1, 
                &iter->second->byteTrans, 
                &iter->second->dwflag, 
                &iter->second->overlap, nullptr)) {
        if (WSAGetLastError () != ERROR_IO_PENDING) {
            iter->second->RecvSendCallback(Unexpected<SockError>(SockError::fromErrno()));
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
    std::function<void(Expected<size_t, SockError>)> &&callback) {
    std::lock_guard<std::mutex> lock(mMutex);
    auto iter = mIOCPOverlappedMap.find(socket.get());

    ILIAS_ASSERT(iter != mIOCPOverlappedMap.end());
    ILIAS_ASSERT(iter->second != nullptr);
    ILIAS_ASSERT(iter->second->isCompleted == true);
    ILIAS_ASSERT(callback != nullptr);
    ILIAS_ASSERT(buf != nullptr);
    ILIAS_ASSERT(n > 0);

    memset(&iter->second->overlap, 0, sizeof(iter->second->overlap));

    iter->second->isCompleted = false;
    iter->second->RecvSendCallback = std::move(callback);
    iter->second->wsaBuf.buf = (char *)buf;
    iter->second->wsaBuf.len = n;
    iter->second->dwflag = 0;
    iter->second->byteTrans = 0;

    if (!WSASend(socket.get(), 
                &iter->second->wsaBuf, 1, 
                &iter->second->byteTrans, 
                iter->second->dwflag, 
                &iter->second->overlap, nullptr)) {
        if (WSAGetLastError () != ERROR_IO_PENDING) {
            iter->second->RecvSendCallback(Unexpected<SockError>(SockError::fromErrno()));
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
    static IOCPOverlapped *data = nullptr;
    static DWORD byteTrans = 0;
    static void *_completionKey = nullptr;

    while (mRunning) {
        BOOL ret = GetQueuedCompletionStatus(mIocpFd, 
                                            &byteTrans, 
                                            (PULONG_PTR)&_completionKey, 
                                            (LPOVERLAPPED*)&data,
                                             INFINITE);
        (void)_completionKey;

        if (!ret && data == nullptr) {
            ::printf("[Ilias::IOCPContext] Poll Error %s\n", SockError::fromErrno().message().c_str());
            continue;
        }
        ILIAS_ASSERT(data != nullptr);
        data->byteTrans = byteTrans;
        if (!ret) {
            data->event |= IOCPEvent::Error;
        }
        // Dispatch
        onEvent(data);
    }
}

inline void IOCPContext::_dump() {
#ifndef NDEBUG
    ::printf("[Ilias::IOCPContext] Dump Watchers\n");
    for (const auto &pfd : mIOCPOverlappedMap) {
        if (pfd.first == mEvent.get()) {
            continue;
        }
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
        ::printf(
            "[Ilias::IOCPContext] Socket %lu Event %s\n",
            uintptr_t(pfd.first), 
            events.c_str()
        );
    }
#endif
}

inline void IOCPContext::onEvent(IOCPOverlapped *overlapped) {
    if (overlapped->event & IOCPEvent::Read) {
        ILIAS_ASSERT(overlapped->RecvSendCallback != nullptr);
        if (overlapped->event & IOCPEvent::Error) {
            overlapped->RecvSendCallback(SockError::fromErrno());
        } else {
            overlapped->RecvSendCallback(overlapped->byteTrans);
        }
    } else if (overlapped->event & IOCPEvent::Write) {
        ILIAS_ASSERT(overlapped->RecvSendCallback != nullptr);
        if (overlapped->event & IOCPEvent::Error) {
            overlapped->RecvSendCallback(SockError::fromErrno());
        } else {
            overlapped->RecvSendCallback(overlapped->byteTrans);
        }
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
            setsockopt(overlapped->peerfd, SOL_SOCKET, 
                        SO_UPDATE_ACCEPT_CONTEXT, 
                        (char*)&overlapped->sockfd, 
                        sizeof(overlapped->sockfd));
            delete[] overlapped->wsaBuf.buf;
            overlapped->wsaBuf.buf = nullptr;
            overlapped->wsaBuf.len = 0;
            overlapped->AcceptCallback(std::make_pair<Socket, IPEndpoint>(Socket(overlapped->peerfd), IPEndpoint::fromRaw(remote, remote_len)));
        }
    } else if (overlapped->event & IOCPEvent::Connect) {
        ILIAS_ASSERT(overlapped->ConnectCallback != nullptr);
        if (overlapped->event & IOCPEvent::Error) {
            overlapped->ConnectCallback(Unexpected<SockError>(SockError::fromErrno()));
        } else {
            setsockopt(overlapped->sockfd, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
            overlapped->ConnectCallback(Expected<void, SockError>());
        }
    } else {
        fprintf(stderr, "Unknown event: %d\n", overlapped->event);
        ILIAS_ASSERT(false);
        // overlapped->RecvSendCallback(SockError::fromErrno());
    }

    overlapped->isCompleted = true;
    // Fn fn;
    // while (mEvent.recv(&fn, sizeof(Fn)) == sizeof(Fn)) {
    //     fn.fn(fn.arg);
    // }
}
inline void IOCPContext::_stop() {
    Fn fn;
    fn.fn = [](void *arg) { static_cast<IOCPContext *>(arg)->mRunning = false; };
    fn.arg = this;
    _invoke(fn);
}
inline void IOCPContext::_invoke(Fn fn) {
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
inline void IOCPContext::_invoke4(Callable &&callable) {
    Fn fn;
    fn.fn = [](void *callable) {
        auto c = static_cast<Callable *>(callable);
        (*c)();
    };
    fn.arg = &callable;
    _invoke(fn);
}


ILIAS_NS_END