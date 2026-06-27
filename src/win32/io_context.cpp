// Impl some win32 io operations
#define UMDF_USING_NTSTATUS // Avoid conflict with winnt.h
#include <ilias/detail/scope_exit.hpp> // ScopeExit
#include <ilias/platform/detail/blocking.hpp>
#include <ilias/platform/iocp.hpp>
#include <ilias/net/endpoint.hpp>
#include <ilias/net/msghdr.hpp> // MsgHdr, MutableMsgHdr
#include <ilias/net/system.hpp>
#include <ilias/net/sockfd.hpp>
#include <ilias/io/context.hpp>
#include <ilias/io/fd_utils.hpp>
#include <ntstatus.h> // STATUS_CANCELLED
#include <algorithm> // std::clamp
#include <atomic> // std::atomic
#include "iocp_ops.hpp"
#include "iocp_afd.hpp"
#include "ntdll.hpp"

ILIAS_NS_BEGIN

namespace win32 {

class IocpDescriptor final : public IoDescriptor {
public:
    union {
        ::SOCKET sockfd;
        ::HANDLE handle;
    };

    IoDescriptor::Type type = Unknown;

    // For Socket data
    struct {
        LPFN_CONNECTEX ConnectEx = nullptr;
        LPFN_DISCONNECTEX DisconnectEx = nullptr;
        LPFN_TRANSMITFILE TransmitFile = nullptr;
        LPFN_ACCEPTEX AcceptEx = nullptr;
        LPFN_GETACCEPTEXSOCKADDRS GetAcceptExSockaddrs = nullptr;
        LPFN_TRANSMITPACKETS TransmitPackets = nullptr;
        LPFN_WSASENDMSG WSASendMsg = nullptr;
        LPFN_WSARECVMSG WSARecvMsg = nullptr;

        int family = 0;
        int type = 0;
        int protocol = 0;
    } sock;
};

// The magic used to assert is a valid callback
constexpr DWORD CALLBACK_MAGIC = 0x114514;

// Create the completion port, it must success
static auto createCompletionPort() -> ::HANDLE {
    auto handle = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (!handle) {
        ILIAS_ERROR("IOCP", "Failed to create iocp: {}", ::GetLastError());
        ILIAS_THROW(std::system_error(SystemError::fromErrno()));
    }
    return handle;
}

IocpContext::IocpContext() : 
    mNt(ntdll()),
    mIocpFd(createCompletionPort()), // MUST success
    mAfdDevice(afdOpenDevice(mNt).value_or(nullptr))
{
    if (mAfdDevice) {
        if (::CreateIoCompletionPort(mAfdDevice.get(), mIocpFd.get(), 0, 0) != mIocpFd.get()) {
            ILIAS_WARN("IOCP", "Failed to add afd device handle to iocp: {}", ::GetLastError());
        }
        if (!::SetFileCompletionNotificationModes(mAfdDevice.get(), FILE_SKIP_COMPLETION_PORT_ON_SUCCESS | FILE_SKIP_SET_EVENT_ON_HANDLE)) {
            ILIAS_WARN("IOCP", "Failed to set completion notification modes: {}", ::GetLastError());
        }
    }

    // Using high resolution timer if available
    if (mNt.hasWaitCompletionPacket()) {
        initTimer();
    }
}

IocpContext::~IocpContext() {

}

// MARK: Executor
auto IocpContext::post(void (*fn)(void *), void *args) -> void {
    ILIAS_ASSERT(fn, "fn must not be nullptr");
    if (runtime::Executor::currentThread() == this) { // In the same thread, use queue directly
        mCallbacks.emplace_back(fn, args);
        return;
    }
    ::PostQueuedCompletionStatus(
        mIocpFd.get(), 
        CALLBACK_MAGIC, 
        reinterpret_cast<ULONG_PTR>(fn), 
        reinterpret_cast<LPOVERLAPPED>(args)
    );
}

auto IocpContext::run(runtime::StopToken token) -> void {
    bool running = true;
    runtime::StopCallback calback(token, [&, this]() {
        schedule([&]() { running = false; });
    });
    DWORD timeout = INFINITE;
    while (true) {
        if (!mTimerFd) { // Use iocp's timeout as timer
            auto nextTimepoint = mService.nextTimepoint();
            if (nextTimepoint) {
                auto diffRaw = *nextTimepoint - std::chrono::steady_clock::now();
                auto diffMs = std::chrono::duration_cast<std::chrono::milliseconds>(diffRaw).count();
                timeout = std::clamp<int64_t>(diffMs, 0, INFINITE - 1);
            }
            mService.updateTimers();
        }
        // Drain the callback queue first
        while (!mCallbacks.empty()) {
            auto cb = mCallbacks.front();
            mCallbacks.pop_front();
            cb.first(cb.second);
            mService.updateTimers();
        }
        if (!running) {
            break;
        }

        // Process io completion
        processCompletion(timeout);
    }
}

auto IocpContext::processCompletion(DWORD timeout) -> void {
    if (mEntriesIdx >= mEntriesSize) { // We need more entries
        if (!::GetQueuedCompletionStatusEx(mIocpFd.get(), mEntries.data(), mEntries.size(), &mEntriesSize, timeout, FALSE)) {
            mEntriesSize = 0;
            mEntriesIdx = 0;
            auto error = ::GetLastError();
            if (error == WAIT_TIMEOUT) {
                return;
            }
            ILIAS_WARN("IOCP", "GetQueuedCompletionStatusEx failed, Error {}", error);
            return;
        }
        mEntriesIdx = 0;
    }
    // Dispatch the completion
    while (mEntriesIdx < mEntriesSize) {
        const auto idx = mEntriesIdx++; // Get the index of the current entry, and move to next immediately, because run is reentrant
        const auto &bytesTransferred = mEntries[idx].dwNumberOfBytesTransferred;
        const auto &overlapped = mEntries[idx].lpOverlapped;
        const auto &key = mEntries[idx].lpCompletionKey;
        if (key) {
            // When key is not 0, it means it is a function pointer
            ILIAS_TRACE("IOCP", "Call callback function ({}, {})", (void*)key, (void*)overlapped);
            ILIAS_ASSERT(bytesTransferred == CALLBACK_MAGIC);
            auto fn = reinterpret_cast<void (*)(void *)>(key);
            fn(overlapped);
            continue;
        }
        if (overlapped) {
            ILIAS_TRACE("IOCP", "Dispatch completion for overlapped {}", (void*)overlapped);
            auto lap = static_cast<IocpOverlapped*>(overlapped);
            auto status = overlapped->Internal; //< Acroding to Microsoft, it stores the error code, BUT NTSTATUS
            auto error = mNt.RtlNtStatusToDosError(status);
            ILIAS_ASSERT(lap->checkMagic(), "The magic of {} is not correct, memory is corrupted?", (void*)overlapped);
            lap->onCompleteCallback(lap, error, bytesTransferred);
        }
        else {
            ILIAS_WARN("IOCP", "GetQueuedCompletionStatusEx returned nullptr overlapped, idx {}", idx);
        }
    }
}

// MARK: Timer
auto IocpContext::initTimer() -> void {
    HANDLE packet = nullptr;
    if (auto status = mNt.NtCreateWaitCompletionPacket(&packet, GENERIC_ALL, nullptr); FAILED(status)) {
        ILIAS_WARN("IOCP", "NtCreateWaitCompletionPacket failed: {}", SystemError(mNt.RtlNtStatusToDosError(status)));
        return;
    }
    mTimerPacket.reset(packet);
    mTimerFd.reset(::CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS));
    if (!mTimerFd) {
        ILIAS_WARN("IOCP", "Failed to create timer: {}", ::GetLastError());
        return;
    }

    // Bind it to iocp
    BOOLEAN alreadySignaled = FALSE;
    if (auto err = submitTimerWait(mTimerPacket.get(), mTimerFd.get(), &alreadySignaled); err != 0 || alreadySignaled) { // Impossible for alreadySignaled to be true
        ILIAS_WARN("IOCP", "Failed to associate timer packet with iocp: {}", SystemError(err));
        mTimerFd = nullptr;
        return;
    }
    mService.setCallback([this](auto timepoint) {
        if (!timepoint) { // No timers
            ::CancelWaitableTimer(mTimerFd.get());
            return;
        }
        // Convert the timeout to FILETIME
        auto now = std::chrono::steady_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::nanoseconds>(*timepoint - now);
        auto time = LARGE_INTEGER {
            .QuadPart = -(diff.count() / 100) // 100 nanoseconds per tick, negative value means relative time
        };
        if (time.QuadPart > 0) { // The diff is negative, so we need to set the time to 0
            time.QuadPart = 0;
        }
        if (!::SetWaitableTimer(mTimerFd.get(), &time, 0, nullptr, nullptr, FALSE)) { // Oneshot
            ILIAS_WARN("IOCP", "Failed to set timer: {}", ::GetLastError());
        }
    });
}

auto IocpContext::processTimer() -> void {
    ILIAS_TRACE("IOCP", "System Timer signaled, processing");
    BOOLEAN alreadySignaled = TRUE;
    while (alreadySignaled) {
        mService.updateTimers(); // Update the timers group

        // Re-arm it
        if (auto err = submitTimerWait(mTimerPacket.get(), mTimerFd.get(), &alreadySignaled); err != 0) {
            ILIAS_WARN("IOCP", "Failed to associate timer packet with iocp: {}", SystemError(err));
            return;
        }
    }
}

auto IocpContext::submitTimerWait(HANDLE packet, HANDLE handle, BOOLEAN *alreadySignaled) -> DWORD {
    auto callback = +[](void *ctxt) {
        return static_cast<IocpContext*>(ctxt)->processTimer();
    };
    auto status = mNt.NtAssociateWaitCompletionPacket(
        packet,
        mIocpFd.get(),
        handle,
        reinterpret_cast<void*>(callback), // CompletionKey callback fn (using function pointer as key)
        this,                              // Overlapped callback args (this)
        0,
        CALLBACK_MAGIC,                    // Callback magic
        alreadySignaled
    );
    if (status != STATUS_SUCCESS) {
        status = mNt.RtlNtStatusToDosError(status);
    }
    ILIAS_TRACE("IOCP", "Submitted timer wait, status = {}, alreadySignaled = {}", status, *alreadySignaled);
    return status;
}

auto IocpContext::sleep(std::chrono::nanoseconds ns) -> Task<void> {
    co_return co_await mService.sleep(ns);
}

// MARK: Context
auto IocpContext::addDescriptor(fd_t fd, IoDescriptor::Type type) -> IoResult<IoDescriptor*> {
    if (fd == nullptr || fd == INVALID_HANDLE_VALUE) {
        ILIAS_ERROR("IOCP", "Invalid file descriptor in addDescriptor, fd = {}, type = {}", fd, type);
        return Err(IoError::InvalidArgument);
    }
    if (type == IoDescriptor::Unknown) {
        auto ret = fd_utils::type(fd);
        if (!ret) {
            return Err(ret.error());
        }
        type = *ret;
    }

    // Try add it to the completion port
    if (type != IoDescriptor::Tty) {
        if (::CreateIoCompletionPort(fd, mIocpFd.get(), 0, 0) != mIocpFd.get()) {
            return Err(SystemError::fromErrno());
        }

        if (!::SetFileCompletionNotificationModes(fd, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS | FILE_SKIP_SET_EVENT_ON_HANDLE)) {
            return Err(SystemError::fromErrno());
        }
    }

    auto nfd = std::make_unique<IocpDescriptor>();
    nfd->handle = fd;
    nfd->type = type;

    if (nfd->type == IoDescriptor::Socket) {
        // Get The ext function ptr
        ILIAS_TRYV(WSAGetExtensionFnPtr(nfd->sockfd, WSAID_CONNECTEX, &nfd->sock.ConnectEx));
        ILIAS_TRYV(WSAGetExtensionFnPtr(nfd->sockfd, WSAID_ACCEPTEX, &nfd->sock.AcceptEx));
        ILIAS_TRYV(WSAGetExtensionFnPtr(nfd->sockfd, WSAID_DISCONNECTEX, &nfd->sock.DisconnectEx));
        ILIAS_TRYV(WSAGetExtensionFnPtr(nfd->sockfd, WSAID_TRANSMITFILE, &nfd->sock.TransmitFile));
        ILIAS_TRYV(WSAGetExtensionFnPtr(nfd->sockfd, WSAID_GETACCEPTEXSOCKADDRS, &nfd->sock.GetAcceptExSockaddrs));
        ILIAS_TRYV(WSAGetExtensionFnPtr(nfd->sockfd, WSAID_TRANSMITPACKETS, &nfd->sock.TransmitPackets));
        ILIAS_TRYV(WSAGetExtensionFnPtr(nfd->sockfd, WSAID_WSARECVMSG, &nfd->sock.WSARecvMsg));
        ILIAS_TRYV(WSAGetExtensionFnPtr(nfd->sockfd, WSAID_WSASENDMSG, &nfd->sock.WSASendMsg));

        ::WSAPROTOCOL_INFOW info {};
        ::socklen_t infoSize = sizeof(info);
        if (::getsockopt(nfd->sockfd, SOL_SOCKET, SO_PROTOCOL_INFOW, reinterpret_cast<char*>(&info), &infoSize) == SOCKET_ERROR) {
            return Err(SystemError::fromErrno());
        }
        nfd->sock.family = info.iAddressFamily;
        nfd->sock.type = info.iSocketType;
        nfd->sock.protocol = info.iProtocol;

        // Disable UDP NetReset and ConnReset
        if (nfd->sock.type == SOCK_DGRAM) {
            ::DWORD flag = 0;
            ::DWORD bytesReturn = 0;
            if (::WSAIoctl(nfd->sockfd, SIO_UDP_NETRESET, &flag, sizeof(flag), nullptr, 0, &bytesReturn, nullptr, nullptr) == SOCKET_ERROR) {
                ILIAS_WARN("IOCP", "Failed to disable UDP NetReset, error: {}", SystemError::fromErrno());
            }
            if (::WSAIoctl(nfd->sockfd, SIO_UDP_CONNRESET, &flag, sizeof(flag), nullptr, 0, &bytesReturn, nullptr, nullptr) == SOCKET_ERROR) {
                ILIAS_WARN("IOCP", "Failed to disable UDP ConnReset, error: {}", SystemError::fromErrno());
            }
        }
    }
    ILIAS_TRACE("IOCP", "Adding fd: {} to completion port, type: {}", fd, type);
    return nfd.release();
}

auto IocpContext::removeDescriptor(IoDescriptor *descriptor) -> IoResult<void> {
    auto nfd = static_cast<IocpDescriptor*>(descriptor);
    delete nfd;
    return {};
}

// MARK: Fs
auto IocpContext::read(IoDescriptor *fd, MutableBuffer buffer, std::optional<size_t> offset) -> IoTask<size_t> {
    auto nfd = static_cast<IocpDescriptor*>(fd);
    if (nfd->type == IoDescriptor::Tty) { //< MSDN says console only can use blocking IO, use we use threadpool to execute it
        co_return co_await runtime::threadpool::read(nfd->handle, buffer, offset);
    }
    co_return co_await IocpReadAwaiter(nfd->handle, buffer, offset);
}


auto IocpContext::write(IoDescriptor *fd, Buffer buffer, std::optional<size_t> offset) -> IoTask<size_t> {
    auto nfd = static_cast<IocpDescriptor*>(fd);
    if (nfd->type == IoDescriptor::Tty) { //< MSDN says console only can use blocking IO, use we use threadpool to execute it
        co_return co_await runtime::threadpool::write(nfd->handle, buffer, offset);
    }
    co_return co_await IocpWriteAwaiter(nfd->handle, buffer, offset);
}

// MARK: Net
auto IocpContext::accept(IoDescriptor *fd, MutableEndpointView endpoint) -> IoTask<socket_t> {
    auto nfd = static_cast<IocpDescriptor*>(fd);
    if (nfd->type != IoDescriptor::Socket) {
        co_return Err(IoError::OperationNotSupported);
    }
    co_return co_await IocpAcceptAwaiter(nfd->sockfd, endpoint, nfd->sock.AcceptEx, nfd->sock.GetAcceptExSockaddrs);
}

auto IocpContext::connect(IoDescriptor *fd, EndpointView endpoint) -> IoTask<void> {
    auto nfd = static_cast<IocpDescriptor*>(fd);
    if (nfd->type != IoDescriptor::Socket) {
        co_return Err(IoError::OperationNotSupported);
    }
    if (!endpoint) {
        co_return Err(IoError::InvalidArgument);
    }
    co_return co_await IocpConnectAwaiter(nfd->sockfd, endpoint, nfd->sock.ConnectEx);
}

auto IocpContext::sendto(IoDescriptor *fd, Buffer buffer, int flags, EndpointView endpoint) -> IoTask<size_t> {
    auto nfd = static_cast<IocpDescriptor*>(fd);
    if (nfd->type != IoDescriptor::Socket) {
        co_return Err(IoError::OperationNotSupported);
    }
    co_return co_await IocpSendtoAwaiter(nfd->sockfd, buffer, flags, endpoint);
}

auto IocpContext::recvfrom(IoDescriptor *fd, MutableBuffer buffer, int flags, MutableEndpointView endpoint) -> IoTask<size_t> {
    auto nfd = static_cast<IocpDescriptor*>(fd);
    if (nfd->type != IoDescriptor::Socket) {
        co_return Err(IoError::OperationNotSupported);
    }
    co_return co_await IocpRecvfromAwaiter(nfd->sockfd, buffer, flags, endpoint);
}

auto IocpContext::sendmsg(IoDescriptor *fd, const MsgHdr &msg, int flags) -> IoTask<size_t> {
    auto nfd = static_cast<IocpDescriptor*>(fd);
    if (nfd->type != IoDescriptor::Socket) {
        co_return Err(IoError::OperationNotSupported);
    }
    co_return co_await IocpSendmsgAwaiter(nfd->sockfd, msg, flags, nfd->sock.WSASendMsg);
}

auto IocpContext::recvmsg(IoDescriptor *fd, MutableMsgHdr &msg, int flags) -> IoTask<size_t> {
    auto nfd = static_cast<IocpDescriptor*>(fd);
    if (nfd->type != IoDescriptor::Socket) {
        co_return Err(IoError::OperationNotSupported);
    }
    co_return co_await IocpRecvmsgAwaiter(nfd->sockfd, msg, flags, nfd->sock.WSARecvMsg);
}

// MARK: Poll
auto IocpContext::poll(IoDescriptor *fd, uint32_t events) -> IoTask<uint32_t> {
    auto nfd = static_cast<IocpDescriptor*>(fd);
    if (nfd->type != IoDescriptor::Socket || !mAfdDevice) {
        co_return Err(IoError::OperationNotSupported);
    }
    co_return co_await AfdPollAwaiter(mAfdDevice.get(), nfd->sockfd, events);
}

// MARK: WaitObject
auto IocpContext::waitObject(HANDLE object) -> IoTask<void> {
    // https://learn.microsoft.com/en-us/windows/win32/devnotes/ntassociatewaitcompletionpacket
    do {
        if (!mNt.hasWaitCompletionPacket()) { // NtCreateWaitCompletionPacket, available in Windows 8
            break;
        }
        // Try to get a completion packet from the pool, if not, create a new one
        Win32Handle packet {};
        if (mCompletionPackets.empty()) {
            HANDLE handle = nullptr;
            auto status = mNt.NtCreateWaitCompletionPacket(&handle, GENERIC_ALL, nullptr);
            if (FAILED(status)) {
                ILIAS_ERROR("IOCP", "NtCreateWaitCompletionPacket failed: {}", SystemError(mNt.RtlNtStatusToDosError(status)));
                break;
            }
            packet = Win32Handle {handle};
        }
        else {
            packet = std::move(mCompletionPackets.front());
            mCompletionPackets.pop_front();
        }
        ILIAS_ASSERT(packet);

        // Create an guard
        ScopeExit guard([&packet, this]() {
            if (mCompletionPackets.size() < mCompletionPacketsPoolSize) {
                mCompletionPackets.emplace_back(std::move(packet));
                return;
            }
        });

        // Wait for the packet
        struct Awaiter : public IocpOverlapped {
            auto await_ready() noexcept -> bool {
                IocpOverlapped::onCompleteCallback = onComplete;
                return false;
            }

            auto await_suspend(runtime::CoroHandle h) -> bool {
                handle = h;

                BOOLEAN alreadySignaled = FALSE;
                auto lap = overlapped();
                auto status = nt->NtAssociateWaitCompletionPacket(
                    packet, 
                    iocp,
                    object,
                    nullptr, // CompletionKey, just use nullptr
                    lap, // OVERLAPPED header is IO_STATUS_BLOCK
                    0,
                    0,
                    &alreadySignaled
                );
                if (FAILED(status)) {
                    error = nt->RtlNtStatusToDosError(status);
                    return false;
                }
                // If already signaled, but the completion still be pending, so we need to wait for it
                ILIAS_TRACE("IOCP", "WaitObject wait for overlapped: {}", static_cast<void*>(lap));
                reg.register_<&Awaiter::onStopRequested>(handle.stopToken(), this);
                return true;
            }

            auto await_resume() -> IoResult<void> {
                if (error != ERROR_SUCCESS) {
                    return Err(SystemError(error));
                }
                return {};
            }

            auto onStopRequested() -> void {
                auto status = nt->NtCancelWaitCompletionPacket(packet, TRUE);
                switch (status) {
                    case STATUS_SUCCESS:
                    case STATUS_CANCELLED: {
                        handle.setStopped();
                        break;
                    }
                    case STATUS_PENDING: { // Failed to cancel, so just wait for the completion
                        break;
                    }
                    default: {
                        ILIAS_ERROR("IOCP", "NtCancelWaitCompletionPacket failed: {}", SystemError(nt->RtlNtStatusToDosError(status)));
                        break;
                    }
                }
            }

            static auto onComplete(IocpOverlapped *_self, DWORD dwError, DWORD dwBytesTransferred) -> void {
                auto self = static_cast<Awaiter*>(_self);
                self->error = dwError;
                self->handle.resume();
            }

            HANDLE iocp;
            HANDLE packet;
            HANDLE object;
            const NtDll *nt;

            // Status
            runtime::CoroHandle handle;
            runtime::StopRegistration reg;
            DWORD error = 0;
        };

        Awaiter awaiter;
        awaiter.iocp = mIocpFd.get();
        awaiter.packet = packet.get();
        awaiter.object = object;
        awaiter.nt = &mNt;
        co_return co_await awaiter;
    }
    while (false);
    // Fallback to win32 API
    co_return co_await IoContext::waitObject(object);
}

} // namespace win32


// MARK: Default IoContext
auto IoContext::waitObject(HANDLE object) -> IoTask<void> {
    struct Awaiter {
        auto await_ready() const noexcept -> bool {
            return false;
        }

        auto await_suspend(runtime::CoroHandle h) -> bool {
            handle = h;
            auto ok = ::RegisterWaitForSingleObject(
                &waitObject, 
                object, 
                &Awaiter::onComplete, 
                this, 
                INFINITE, 
                WT_EXECUTEDEFAULT | WT_EXECUTEONLYONCE
            );
            if (!ok) {
                return false; 
            }
            reg.register_<&Awaiter::onStopRequested>(handle.stopToken(), this);
            return true;
        }

        auto await_resume() -> IoResult<void> {
            if (!waitObject) {
                return Err(SystemError::fromErrno());
            }
            if (!::UnregisterWait(waitObject)) {
                ILIAS_ERROR("Win32", "UnregisterWait failed: {}", SystemError::fromErrno());
            }
            ILIAS_ASSERT(waitCompleted.test());
            return {};
        }

        auto onStopRequested() -> void {
            if (waitCompleted.test_and_set()) { // Already completed?
                return;
            }
            if (!::UnregisterWaitEx(waitObject, nullptr)) {
                // WTF
                ILIAS_ERROR("Win32", "UnregisterWaitEx failed: {}", SystemError::fromErrno());
            }
            handle.setStopped();
        }

        static auto CALLBACK onComplete(void *_self, BOOLEAN timeout) -> void {
            ILIAS_ASSERT(!timeout); // We use INFINITE timeout
            auto self = static_cast<Awaiter*>(_self);
            if (self->waitCompleted.test_and_set()) { // Canceled?
                return;
            }
            self->handle.schedule();
        }

        HANDLE waitObject = nullptr;
        HANDLE object = nullptr;
        runtime::CoroHandle handle;
        runtime::StopRegistration reg;
        std::atomic_flag waitCompleted;
    };

    Awaiter awaiter {
        .object = object
    };
    co_return co_await awaiter;
}

ILIAS_NS_END