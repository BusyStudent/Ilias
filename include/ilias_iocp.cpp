#ifdef _WIN32
#include "ilias_iocp.hpp"
#include "ilias_latch.hpp"
#include <memory>
#include <thread>

ILIAS_NS_BEGIN

struct WSAExtFunctions {
    LPFN_GETACCEPTEXSOCKADDRS GetAcceptExSocketAddress = nullptr;
    LPFN_ACCEPTEX AcceptEx = nullptr;
    LPFN_CONNECTEX ConnectEx = nullptr;
    LPFN_TRANSMITFILE TransmitFile = nullptr;
};
static WSAExtFunctions Fns;
static std::once_flag FnsOnceFlag;

IOCPContext::IOCPContext() {
    mIocpFd = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (!mIocpFd) {
        return;
    }
    // Get extension functions
    std::call_once(FnsOnceFlag, &IOCPContext::_loadFunctions, this);
}
IOCPContext::~IOCPContext() {
    ::CloseHandle(mIocpFd);
}

auto IOCPContext::run() -> void {
    DWORD bytesTrans = 0;
    ULONG_PTR compeleteKey;
    LPOVERLAPPED overlapped = nullptr;
    while (!mQuit) {
        auto ret = ::GetQueuedCompletionStatus(
            mIocpFd, 
            &bytesTrans, 
            &compeleteKey, 
            &overlapped,
            _calcWaiting()
        );
        if (!ret) {
            auto err = ::GetLastError();
            if (err == ERROR_OPERATION_ABORTED) {
                // Skip aborted operation
                continue;
            }
        }
        // Is a normal callback, overlapped is a args
        if (compeleteKey) {
            ILIAS_ASSERT(bytesTrans == 0x114514);
            auto cb = reinterpret_cast<void(*)(void*)>(compeleteKey);
            cb(overlapped);
            continue;
        }
        if (overlapped) {
            auto lap = static_cast<IOCPOverlapped*>(overlapped);
            lap->onCompelete(lap, ret, bytesTrans);            
        }
    }
    mQuit = false;
}
auto IOCPContext::post(void (*fn)(void *), void *args) -> void {
    ::PostQueuedCompletionStatus(
        mIocpFd, 
        0x114514, 
        reinterpret_cast<ULONG_PTR>(fn), 
        reinterpret_cast<LPOVERLAPPED>(args)
    );
}
auto IOCPContext::quit() -> void {
    post([](void *self) {
        static_cast<IOCPContext*>(self)->mQuit = true;
    }, this);
}

// Timer TODO
auto IOCPContext::delTimer(uintptr_t timer) -> bool {
    return false;
}
auto IOCPContext::addTimer(int64_t ms, void (*fn)(void *), void *arg, int flags) -> uintptr_t {
    return 0;
}

// Add / Remove
auto IOCPContext::addSocket(SocketView sock) -> Result<void> {
    auto ret = ::CreateIoCompletionPort(HANDLE(sock.get()), mIocpFd, 0, 0);
    if (!ret) {
        return Unexpected(Error::fromErrno());
    }
    ::SetFileCompletionNotificationModes
        (HANDLE(sock.get()), 
        FILE_SKIP_COMPLETION_PORT_ON_SUCCESS | FILE_SKIP_SET_EVENT_ON_HANDLE
    );
    return {};
}
auto IOCPContext::removeSocket(SocketView sock) -> Result<void> {
    return {};
}

// IOCP Recv / Send / Etc...
template <typename T, typename RetT>
class IOCPAwaiter : public IOCPOverlapped {
public:
    IOCPAwaiter() {
        onCompelete = [](IOCPOverlapped *data, BOOL ok, DWORD byteTrans) {
            auto self = static_cast<IOCPAwaiter*>(data);
            self->mOk = ok;
            self->bytesTransfered = byteTrans;
            if (self->mCallerHandle) {
                self->mCallerHandle.resume();
            }
        };
    }
    auto await_ready() const noexcept -> bool {
        return false;
    }
    template <typename U>
    auto await_suspend(std::coroutine_handle<TaskPromise<U> > h) noexcept -> bool {
        mCaller = &h.promise();
        mCallerHandle = h;

        if (mCaller->isCanceled()) {
            return true;
        }
        if (static_cast<T*>(this)->doIocp()) {
            // Return Ok
            mOk = true;
            return false; //< Resume
        }
        if (::WSAGetLastError() != ERROR_IO_PENDING) {
            mOk = false;
            return false; //< Resume
        }
        mOverlappedStarted = true;
        return true; //< Suspend, waiting for IO
    }
    auto await_resume() -> RetT {
        if (mCaller->isCanceled() && !mOverlappedStarted) {
            return Unexpected(Error::Canceled);
        }
        if (mCaller->isCanceled() && mOverlappedStarted) {
            // Io is started
            // TODO: How to handle it
            ::CancelIoEx(HANDLE(sock), this);
            return Unexpected(Error::Canceled);
        }
        // Get result
        return static_cast<T*>(this)->onCompelete(mOk, bytesTransfered);
    }

    ::SOCKET sock;
    ::DWORD bytesTransfered = 0;
private:
    PromiseBase *mCaller = nullptr;
    std::coroutine_handle<> mCallerHandle;
    bool mOk = false;
    bool mOverlappedStarted = false;
};

struct RecvAwaiter : public IOCPAwaiter<RecvAwaiter, Result<size_t> > {
    auto doIocp() -> bool {
        return ::WSARecv(sock, &buf, 1, &bytesTransfered, &flags, this, nullptr) == 0;
    }
    auto onCompelete(bool ok, DWORD byteTrans) -> Result<size_t> {
        if (ok) {
            return byteTrans;
        } 
        return Unexpected(Error::fromErrno());
    }

    ::WSABUF buf;
    ::DWORD flags = 0;
};

auto IOCPContext::recv(SocketView sock, void *buf, size_t len) -> Task<size_t> {
    RecvAwaiter awaiter;
    awaiter.sock = sock.get();
    awaiter.buf.buf = (CHAR*) buf;
    awaiter.buf.len = len;
    co_return co_await awaiter;
}

struct SendAwaiter : public IOCPAwaiter<SendAwaiter, Result<size_t> > {
    auto doIocp() -> bool {
        return ::WSASend(sock, &buf, 1, &bytesTransfered, flags, this, nullptr) == 0;
    }
    auto onCompelete(bool ok, DWORD byteTrans) -> Result<size_t> {
        if (ok) {
            return byteTrans;
        }
        return Unexpected(Error::fromErrno());
    }
    
    ::WSABUF buf;
    ::DWORD flags = 0;
};

auto IOCPContext::send(SocketView sock, const void *buf, size_t len) -> Task<size_t> {
    SendAwaiter awaiter;
    awaiter.sock = sock.get();
    awaiter.buf.buf = (CHAR*) buf;
    awaiter.buf.len = len;
    co_return co_await awaiter;
}

struct ConnectAwaiter : public IOCPAwaiter<ConnectAwaiter, Result<void> > {
    auto doIocp() -> bool {
        ::WSAPROTOCOL_INFO info;
        ::socklen_t infoLen = sizeof(info);
        ::sockaddr_storage addr;
        ::socklen_t len = sizeof(addr);
        if (::getsockname(sock, reinterpret_cast<::sockaddr*>(&addr), &len) != 0) {
            if (::getsockopt(sock, SOL_SOCKET, SO_PROTOCOL_INFO, (char*) &info, &infoLen) != 0) {
                return false;
            }
            // Not binded
            ::memset(&addr, 0, sizeof(addr));
            addr.ss_family = info.iAddressFamily;
            ::bind(sock, reinterpret_cast<::sockaddr*>(&addr), endpoint.length());
        }
        return Fns.ConnectEx(
            sock, &endpoint.data<::sockaddr>(), endpoint.length(), 
            nullptr, 0, &bytesTransfered, this
        );
    }
    auto onCompelete(bool ok, DWORD byteTrans) -> Result<void> {
        if (ok) {
            ::setsockopt(sock, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0);
            return {};
        }
        return Unexpected(Error::fromErrno());
    }

    IPEndpoint endpoint;
};

auto IOCPContext::connect(SocketView sock, const IPEndpoint &addr) -> Task<void> {
    ConnectAwaiter awaiter;
    awaiter.sock = sock.get();
    awaiter.endpoint = addr;
    co_return co_await awaiter;
}

struct AcceptAwaiter : public IOCPAwaiter<AcceptAwaiter, Result<std::pair<Socket, IPEndpoint> > > {
    auto doIocp() -> bool {
        ::WSAPROTOCOL_INFO info;
        ::socklen_t len = sizeof(info);
        if (::getsockopt(sock, SOL_SOCKET, SO_PROTOCOL_INFO, (char*) &info, &len) != 0) {
            return false;
        }
        newSocket = ::socket(info.iAddressFamily, info.iSocketType, info.iProtocol);
        if (newSocket == INVALID_SOCKET) {
            return false;
        }
        return Fns.AcceptEx(
            sock,
            newSocket,
            addressBuffer,
            0,
            sizeof(::sockaddr_storage) + 16, //< Max address size
            sizeof(::sockaddr_storage) + 16, //< Max address size
            &bytesTransfered,
            this
        );
    }
    auto onCompelete(bool ok, DWORD byteTrans) -> Result<std::pair<Socket, IPEndpoint> > {
        if (!ok) {
            ::closesocket(newSocket);
            return Unexpected(Error::fromErrno());
        }
        ::sockaddr *remote = nullptr;
        ::sockaddr *local = nullptr;
        ::socklen_t remoteLen = 0;
        ::socklen_t localLen = 0;

        Fns.GetAcceptExSocketAddress(
            addressBuffer, 0,
            sizeof(::sockaddr_storage) + 16, //< Max address size
            sizeof(::sockaddr_storage) + 16, //< Max address size
            &local, &localLen,
            &remote, &remoteLen
        );
        ::setsockopt(sock, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, nullptr, 0);

        return std::make_pair(Socket(newSocket), IPEndpoint::fromRaw(remote, remoteLen));
    }
    ::SOCKET newSocket = INVALID_SOCKET;
    ::uint8_t addressBuffer[(sizeof(::sockaddr_storage) + 16) * 2];
};

auto IOCPContext::accept(SocketView sock) -> Task<std::pair<Socket, IPEndpoint> > {
    AcceptAwaiter awaiter;
    awaiter.sock = sock.get();
    co_return co_await awaiter;
}


struct SendtoAwaiter : public IOCPAwaiter<SendtoAwaiter, Result<size_t> > {
    auto doIocp() -> bool {
        return ::WSASendTo(
            sock,
            &buf, 1, &bytesTransfered, flags, 
            &endpoint.data<::sockaddr>(), endpoint.length(),
            this, nullptr
        ) == 0;
    }
    auto onCompelete(bool ok, DWORD byteTrans) -> Result<size_t> {
        if (ok) {
            return byteTrans;
        }
        return Unexpected(Error::fromErrno());
    }

    ::WSABUF buf;
    ::DWORD flags = 0;
    IPEndpoint endpoint;
};

auto IOCPContext::sendto(SocketView sock, const void *buf, size_t len, const IPEndpoint &addr) -> Task<size_t> {
    SendtoAwaiter awaiter;
    awaiter.sock = sock.get();
    awaiter.buf.buf = (CHAR*) buf;
    awaiter.buf.len = len;
    awaiter.endpoint = addr;
    co_return co_await awaiter;
}

struct RecvfromAwaiter : public IOCPAwaiter<RecvfromAwaiter, Result<std::pair<size_t, IPEndpoint> > > {
    auto doIocp() -> bool {
        return ::WSARecvFrom(
            sock,
            &buf, 1, &bytesTransfered, &flags,
            reinterpret_cast<::sockaddr*>(&addr), &len,
            this, nullptr
        ) == 0;
    }
    auto onCompelete(bool ok, DWORD byteTrans) -> Result<std::pair<size_t, IPEndpoint> > {
        if (ok) {
            return std::make_pair(byteTrans, IPEndpoint::fromRaw(&addr, len));
        }
        return Unexpected(Error::fromErrno());
    }

    ::WSABUF buf;
    ::DWORD flags = 0;
    ::sockaddr_storage addr;
    ::socklen_t len = 0;
};

auto IOCPContext::recvfrom(SocketView sock, void *buf, size_t len) -> Task<std::pair<size_t, IPEndpoint> > {
    RecvfromAwaiter awaiter;
    awaiter.sock = sock.get();
    awaiter.buf.buf = (CHAR*) buf;
    awaiter.buf.len = len;
    co_return co_await awaiter;
}

// Get WSA Ext functions
inline auto IOCPContext::_loadFunctions() -> void {
    ::GUID acceptExId = WSAID_ACCEPTEX;
    ::GUID connectExId = WSAID_CONNECTEX;
    ::GUID transFileId = WSAID_TRANSMITFILE;
    ::GUID getAcceptExSockAddrId = WSAID_GETACCEPTEXSOCKADDRS;
    ::DWORD bytesReturned = 0;
    ::DWORD bytesNeeded = 0;

    Socket helper(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (!helper.isValid()) {
        return;
    }
    ::WSAIoctl(
        helper.get(), SIO_GET_EXTENSION_FUNCTION_POINTER,
        &acceptExId, sizeof(acceptExId), &Fns.AcceptEx, 
        sizeof(Fns.AcceptEx), &bytesNeeded, 
        nullptr, nullptr
    );
    ::WSAIoctl(
        helper.get(), SIO_GET_EXTENSION_FUNCTION_POINTER,
        &connectExId, sizeof(connectExId), &Fns.ConnectEx,
        sizeof(Fns.ConnectEx), &bytesNeeded, 
        nullptr, nullptr
    );
    ::WSAIoctl(
        helper.get(), SIO_GET_EXTENSION_FUNCTION_POINTER,
        &transFileId, sizeof(transFileId), &Fns.TransmitFile,
        sizeof(Fns.TransmitFile), &bytesNeeded, 
        nullptr, nullptr
    );
    ::WSAIoctl(
        helper.get(), SIO_GET_EXTENSION_FUNCTION_POINTER,
        &getAcceptExSockAddrId, sizeof(getAcceptExSockAddrId), &Fns.GetAcceptExSocketAddress,
        sizeof(Fns.GetAcceptExSocketAddress), &bytesNeeded, 
        nullptr, nullptr
    );
}

ILIAS_NS_END

#endif