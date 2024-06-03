#ifdef _WIN32
#include "ilias_iocp.hpp"
#include <winternl.h>
#include <algorithm>
#include <memory>

#if 1
// #if 0
    #define IOCP_LOG(...) ::fprintf(stderr, __VA_ARGS__)
#else
    #define IOCP_LOG(...) do {} while(0)
#endif

#undef min
#undef max

ILIAS_NS_BEGIN

// Basic defs
struct WSAExtFunctions {
    LPFN_GETACCEPTEXSOCKADDRS GetAcceptExSocketAddress = nullptr;
    LPFN_ACCEPTEX AcceptEx = nullptr;
    LPFN_CONNECTEX ConnectEx = nullptr;
    LPFN_TRANSMITFILE TransmitFile = nullptr;
};
class IOCPOverlapped : public ::OVERLAPPED {
public:
    IOCPOverlapped() {
        ::memset(static_cast<OVERLAPPED*>(this), 0, sizeof(OVERLAPPED));
    }

    // Called on Dispatched
    void (*onCompelete)(IOCPOverlapped *self, BOOL ok, DWORD byteTrans) = nullptr;
};

// AFD Poll
typedef struct _AFD_POLL_HANDLE_INFO {
    HANDLE Handle;
    ULONG Events;
    NTSTATUS Status;
} AFD_POLL_HANDLE_INFO, *PAFD_POLL_HANDLE_INFO;

typedef struct _AFD_POLL_INFO {
    LARGE_INTEGER Timeout;
    ULONG NumberOfHandles;
    ULONG Exclusive;
    AFD_POLL_HANDLE_INFO Handles[1];
} AFD_POLL_INFO, *PAFD_POLL_INFO;

struct NtFunctions {
    decltype(::NtCreateFile) *NtCreateFile = nullptr;
    decltype(::NtDeviceIoControlFile) *NtDeviceIoControlFile = nullptr;
    decltype(::RtlNtStatusToDosError) *RtlNtStatusToDosError = nullptr;
};

// Static data
static WSAExtFunctions Fns;
static NtFunctions NtFns;
static std::once_flag FnsOnceFlag;

#pragma region Loop
IOCPContext::IOCPContext() {
    mIocpFd = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (!mIocpFd) {
        return;
    }
    // Get extension functions
    std::call_once(FnsOnceFlag, &IOCPContext::_loadFunctions, this);

    // Init Poll
    _initPoll();
}
IOCPContext::~IOCPContext() {
    ::CloseHandle(mIocpFd);
    ::CloseHandle(mAfdDevice);
}

inline
auto IOCPContext::_runIo(DWORD timeout) -> void {
    DWORD bytesTrans = 0;
    ULONG_PTR compeleteKey;
    LPOVERLAPPED overlapped = nullptr;
    auto ret = ::GetQueuedCompletionStatus(
        mIocpFd, 
        &bytesTrans, 
        &compeleteKey, 
        &overlapped,
        timeout
    );
    if (!ret) {
        auto err = ::GetLastError();
        if (err == WAIT_TIMEOUT) {
            // Skip timeouted
            return;
        }
    }
    // Is a normal callback, overlapped is a args
    if (compeleteKey) {
        ILIAS_ASSERT(bytesTrans == 0x114514);
        auto cb = reinterpret_cast<void(*)(void*)>(compeleteKey);
        cb(overlapped);
        return;
    }
    if (overlapped) {
        auto lap = static_cast<IOCPOverlapped*>(overlapped);
        lap->onCompelete(lap, ret, bytesTrans);            
    }
}
auto IOCPContext::run(StopToken &token) -> void {
    while (!token.isStopRequested()) {
        _runTimers();
        _runIo(_calcWaiting());
    }
}
auto IOCPContext::post(void (*fn)(void *), void *args) -> void {
    ::PostQueuedCompletionStatus(
        mIocpFd, 
        0x114514, 
        reinterpret_cast<ULONG_PTR>(fn), 
        reinterpret_cast<LPOVERLAPPED>(args)
    );
}

// Timer
#pragma region Timer
auto IOCPContext::delTimer(uintptr_t timer) -> bool {
    auto iter = mTimers.find(timer);
    if (iter == mTimers.end()) {
        return false;
    }
    mTimerQueue.erase(iter->second);
    mTimers.erase(iter);
    return true;
}
auto IOCPContext::addTimer(int64_t ms, void (*fn)(void *), void *arg, int flags) -> uintptr_t {
    uintptr_t id = mTimerIdBase + 1;
    while (mTimers.find(id) != mTimers.end()) {
        id ++;
    }
    mTimerIdBase = id;
    uint64_t expireTime = ::GetTickCount64() + ms;

    auto iter = mTimerQueue.insert(std::pair(expireTime, Timer{id, ms, flags, fn, arg}));
    mTimers.insert(std::pair(id, iter));
    return id;
}
inline
auto IOCPContext::_runTimers() -> void {
    if (mTimerQueue.empty()) {
        return;
    }
    auto now = ::GetTickCount64();
    for (auto iter = mTimerQueue.begin(); iter != mTimerQueue.end();) {
        auto [expireTime, timer] = *iter;
        if (expireTime > now) {
            break;
        }
        // Invoke
        post(timer.fn, timer.arg);

        // Cleanup if
        if (timer.flags & TimerFlags::TimerSingleShot) {
            mTimers.erase(timer.id); // Remove the timer
        }
        else {
            auto newExpireTime = ::GetTickCount64() + timer.ms;
            auto newIter = mTimerQueue.insert(iter, std::make_pair(newExpireTime, timer));
            mTimers[timer.id] = newIter;
        }
        iter = mTimerQueue.erase(iter); // Move next
    }
}
inline
auto IOCPContext::_calcWaiting() const -> DWORD {
    if (mTimerQueue.empty()) {
        return INFINITE;
    }
    int64_t time = mTimerQueue.begin()->first - ::GetTickCount64();
    IOCP_LOG("[Ilias] IOCP Waiting: %lld\n", time);
    return std::clamp<int64_t>(time, 0, INFINITE - 1);
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
#pragma region IOCP Network
template <typename T, typename RetT>
class IOCPAwaiter : public IOCPOverlapped {
public:
    IOCPAwaiter() {
        onCompelete = [](IOCPOverlapped *data, BOOL ok, DWORD byteTrans) {
            auto self = static_cast<IOCPAwaiter*>(data);
            self->mOk = ok;
            self->mGot = true; //< Got value
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
        if (::GetLastError() != ERROR_IO_PENDING) { //< In WindowNT GetLastError is equals to WSAGetLastError
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
            _doCancel();
            return Unexpected(Error::Canceled);
        }
        // Get result
        return static_cast<T*>(this)->onCompelete(mOk, bytesTransfered);
    }
    auto isCanceled() -> bool {
        return mCaller->isCanceled();
    }

    union {
        ::SOCKET sock;
        ::HANDLE fd = INVALID_HANDLE_VALUE;
    };
    ::DWORD bytesTransfered = 0;
    IOCPContext *ctxt = nullptr;
private:
    auto _doCancel() {
        IOCP_LOG("[Ilias] IOCP doCancel to (%p, %p)\n", fd, this);
        mCallerHandle = nullptr; //< Make it to nullptr, avoid we got resumed on the next event loop
        if (!::CancelIoEx(fd, this)) {
            auto err = ::GetLastError();
            IOCP_LOG("[Ilias] IOCP failed to CancelIoEx(%p, %p) => %d\n", fd, this, int(err));
        }
        // Collect the cancel result
        IOCP_LOG("[Ilias] Enter EventLoop to get cancel result\n");
        while (!mGot) {
            ctxt->_runIo(INFINITE);
        }
        IOCP_LOG("[Ilias] Got result\n");
        // Call it
        static_cast<T*>(this)->onCompelete(mOk, bytesTransfered);
    }

    PromiseBase *mCaller = nullptr;
    std::coroutine_handle<> mCallerHandle;
    bool mOk = false;
    bool mGot = false;
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
    awaiter.ctxt = this;
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
    awaiter.ctxt = this;
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
    awaiter.ctxt = this;
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
    awaiter.ctxt = this;
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
    awaiter.ctxt = this;
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
            return std::make_pair(size_t(byteTrans), IPEndpoint::fromRaw(&addr, len));
        }
        return Unexpected(Error::fromErrno());
    }

    ::WSABUF buf;
    ::DWORD flags = 0;
    ::sockaddr_storage addr;
    ::socklen_t len = sizeof(addr);
};

auto IOCPContext::recvfrom(SocketView sock, void *buf, size_t len) -> Task<std::pair<size_t, IPEndpoint> > {
    RecvfromAwaiter awaiter;
    awaiter.ctxt = this;
    awaiter.sock = sock.get();
    awaiter.buf.buf = (CHAR*) buf;
    awaiter.buf.len = len;
    co_return co_await awaiter;
}

// Poll
// By wepoll implementation, using DeviceIoControl
#pragma region IOCP Poll
#define AFD_POLL               9
#define IOCTL_AFD_POLL 0x00012024

auto IOCPContext::_initPoll() -> void {    
    // Open the afd device for impl poll
    wchar_t path [] = L"\\Device\\Afd\\Ilias";
    ::HANDLE device = nullptr;
    ::UNICODE_STRING deviceName {
        sizeof(path) - sizeof(path[0]),
        sizeof(path),
        path
    };
    ::OBJECT_ATTRIBUTES objAttr {
        sizeof(OBJECT_ATTRIBUTES),
        nullptr,
        &deviceName,
        0,
        nullptr
    };
    ::IO_STATUS_BLOCK statusBlock {};
    auto status = NtFns.NtCreateFile(
        &device,
        SYNCHRONIZE,
        &objAttr,
        &statusBlock,
        nullptr,
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        FILE_OPEN,
        0,
        nullptr,
        0
    );
    if (status != 0) {
        auto winerr = NtFns.RtlNtStatusToDosError(status);
        ::SetLastError(winerr);
        return;
    }

    // Make guard
    std::unique_ptr<void, decltype(::CloseHandle) *> guard {
        device,
        ::CloseHandle
    };

    if (!::CreateIoCompletionPort(device, mIocpFd, 0, 0)) {
        return;
    }
    if (!::SetFileCompletionNotificationModes(device, FILE_SKIP_SET_EVENT_ON_HANDLE)) {
        return;
    }

    // Done
    mAfdDevice = guard.release();
}

#define AFD_POLL_RECEIVE           0x0001
#define AFD_POLL_RECEIVE_EXPEDITED 0x0002
#define AFD_POLL_SEND              0x0004
#define AFD_POLL_DISCONNECT        0x0008
#define AFD_POLL_ABORT             0x0010
#define AFD_POLL_LOCAL_CLOSE       0x0020
#define AFD_POLL_ACCEPT            0x0080
#define AFD_POLL_CONNECT_FAIL      0x0100

struct PollAwaiter : public IOCPOverlapped {
    PollAwaiter(SOCKET sock, HANDLE device, uint32_t events) : sock(sock), device(device) {
        // Settings OVERLAPPED callback
        IOCPOverlapped::onCompelete = onCompelete;
        // Fill the info
        info.Exclusive = TRUE;
        info.NumberOfHandles = 1; //< Only one socket
        info.Timeout.QuadPart = INT64_MAX;
        info.Handles[0].Handle = HANDLE(sock);
        info.Handles[0].Status = 0;
        info.Handles[0].Events = AFD_POLL_LOCAL_CLOSE; //< When socket was ::closesocket(sock)

        if (events & PollEvent::In) {
            info.Handles[0].Events |= (AFD_POLL_RECEIVE | AFD_POLL_DISCONNECT | AFD_POLL_ACCEPT | AFD_POLL_ABORT);
        }
        if (events & PollEvent::Out) {
            info.Handles[0].Events |= (AFD_POLL_SEND | AFD_POLL_CONNECT_FAIL);
        }
        if (events & PollEvent::Err) {
            info.Handles[0].Events |= (AFD_POLL_ABORT | AFD_POLL_CONNECT_FAIL);
        }
    }
    auto await_ready() -> bool {
        if (!submitPoll(sock, device, &info, &rinfo, this)) {
            auto err = ::WSAGetLastError();
            if (err != ERROR_IO_PENDING) {
                IOCP_LOG("[IOCP] Poll failed %d\n", int(err));
                return true;
            }
        }
        IOCP_LOG("[IOCP] Poll Start %p\n", this);
        started = true;
        return false;
    }
    auto await_suspend(std::coroutine_handle<> h) -> void {
        handle = h;
    }
    auto await_resume() -> Result<uint32_t> {
        if (!completed && started) {
            // Cancel the poll
            // TODO:
            IOCP_LOG("[IOCP] TODO: Poll Canceling ...\n");
            ::CancelIoEx(device, this);
        }
        uint32_t revents = 0;
        if (rinfo.Handles[0].Events & (AFD_POLL_RECEIVE | AFD_POLL_DISCONNECT | AFD_POLL_ACCEPT | AFD_POLL_ABORT)) {
            revents |= PollEvent::In;
        }
        if (rinfo.Handles[0].Events & (AFD_POLL_SEND | AFD_POLL_CONNECT_FAIL)) {
            revents |= PollEvent::Out;
        }
        if (rinfo.Handles[0].Events & (AFD_POLL_ABORT | AFD_POLL_CONNECT_FAIL)) {
            revents |= PollEvent::Err;
        }
        IOCP_LOG("[IOCP] Poll Done %p\n", this);
        return revents;
    }

    //< Submit a poll to device
    static auto submitPoll(SOCKET sock, HANDLE device, AFD_POLL_INFO *in, AFD_POLL_INFO *out, OVERLAPPED *overlapped) -> BOOL {
        IO_STATUS_BLOCK *ioStatusBlock = nullptr;
        HANDLE event = nullptr;
        void *apcContext = nullptr;
        if (overlapped) {
            ioStatusBlock = (IO_STATUS_BLOCK*) &overlapped->Internal;
            event = overlapped->hEvent;
            apcContext = overlapped;
        }

        ioStatusBlock->Status = STATUS_PENDING;
        auto status = NtFns.NtDeviceIoControlFile(
            device,
            event,
            nullptr,
            apcContext,
            ioStatusBlock,
            IOCTL_AFD_POLL,
            in,
            sizeof(*in),
            out,
            sizeof(*out)
        );
        auto err = NtFns.RtlNtStatusToDosError(status);
        ::WSASetLastError(err);
        return err == ERROR_SUCCESS;
    }
    static auto onCompelete(IOCPOverlapped *_self, BOOL ok, DWORD byteTrans) -> void {
        IOCP_LOG("[IOCP] Poll Awake %p\n", _self);
        auto self = static_cast<PollAwaiter*>(_self);
        self->completed = true;
        if (self->handle) {
            self->handle.resume();
        }
    }

    SOCKET sock = INVALID_SOCKET;
    HANDLE device = INVALID_HANDLE_VALUE;
    bool started = false;
    bool completed = false;
    AFD_POLL_INFO info { };
    AFD_POLL_INFO rinfo { };
    std::coroutine_handle<> handle;
};

auto IOCPContext::poll(SocketView sock, uint32_t event) -> Task<uint32_t> {
    if (!mAfdDevice) {
        co_return Unexpected(Error::OperationNotSupported);
    }
    ::SOCKET baseSocket = INVALID_SOCKET;
    ::DWORD bytesReturned = 0;

    // Get current handle
    if (WSAIoctl(sock.get(), SIO_BASE_HANDLE, nullptr, 0, &baseSocket, sizeof(baseSocket), &bytesReturned, nullptr, nullptr) != 0) {
        co_return Unexpected(Error::fromErrno());
    }

    PollAwaiter awaiter {baseSocket, mAfdDevice, event};
    
    co_return co_await awaiter;
}

#pragma region Function loader
// Load functions
inline auto IOCPContext::_loadFunctions() -> void {
    // Get WSA Ext functions
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

    // Get Nt parts
    auto mod = ::GetModuleHandleA("ntdll.dll");
    NtFns.NtCreateFile = reinterpret_cast<decltype(NtFns.NtCreateFile)>(::GetProcAddress(mod, "NtCreateFile"));
    NtFns.NtDeviceIoControlFile = reinterpret_cast<decltype(NtFns.NtDeviceIoControlFile)>(::GetProcAddress(mod, "NtDeviceIoControlFile"));
    NtFns.RtlNtStatusToDosError = reinterpret_cast<decltype(NtFns.RtlNtStatusToDosError)>(::GetProcAddress(mod, "RtlNtStatusToDosError"));
}

ILIAS_NS_END

#endif