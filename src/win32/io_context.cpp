// Impl some win32 io operations
#define UMDF_USING_NTSTATUS // Avoid conflict with winnt.h
#include <ilias/detail/scope_exit.hpp> // ScopeExit
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

IocpContext::IocpContext() : mNt(ntdll()) {
    mIocpFd = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (!mIocpFd) {
        ILIAS_ERROR("IOCP", "Failed to create iocp: {}", ::GetLastError());
        ILIAS_THROW(std::system_error(SystemError::fromErrno()));
    }
    if (auto afd = afdOpenDevice(mNt); afd) {
        if (::CreateIoCompletionPort(*afd, mIocpFd, 0, 0) != mIocpFd) {
            ILIAS_WARN("IOCP", "Failed to add afd device handle to iocp: {}", ::GetLastError());
        }
        if (!::SetFileCompletionNotificationModes(*afd, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS | FILE_SKIP_SET_EVENT_ON_HANDLE)) {
            ILIAS_WARN("IOCP", "Failed to set completion notification modes: {}", ::GetLastError());
        }
        mAfdDevice = *afd;
    }
}

IocpContext::~IocpContext() {
    for (auto packet : mCompletionPackets) {
        if (!::CloseHandle(packet)) {
            ILIAS_WARN("IOCP", "Failed to close completion packet: {}", ::GetLastError());
        }
    }
    if (mAfdDevice) {
        if (!::CloseHandle(mAfdDevice)) {
            ILIAS_WARN("IOCP", "Failed to close afd handle: {}", ::GetLastError());
        }
    }
    if (mIocpFd) {
        if (!::CloseHandle(mIocpFd)) {
            ILIAS_WARN("IOCP", "Failed to close iocp handle: {}", ::GetLastError());
        }
    }
}

#pragma region Executor
auto IocpContext::post(void (*fn)(void *), void *args) -> void {
    ILIAS_ASSERT(fn);
    ::PostQueuedCompletionStatus(
        mIocpFd, 
        0x114514, 
        reinterpret_cast<ULONG_PTR>(fn), 
        reinterpret_cast<LPOVERLAPPED>(args)
    );
}

auto IocpContext::run(runtime::StopToken token) -> void {
    DWORD timeout = INFINITE;
    while (!token.stop_requested()) {
        auto nextTimepoint = mService.nextTimepoint();
        if (nextTimepoint) {
            auto diffRaw = *nextTimepoint - std::chrono::steady_clock::now();
            auto diffMs = std::chrono::duration_cast<std::chrono::milliseconds>(diffRaw).count();
            timeout = std::clamp<int64_t>(diffMs, 0, INFINITE - 1);
        }
        mService.updateTimers();
        processCompletion(timeout);
    }
}

// TODO: Optimize it, Dyn Select between GetQueuedCompletionStatus and GetQueuedCompletionStatusEx 
// TODO: based on the number of I/O operations within a specified time interval.
auto IocpContext::processCompletion(DWORD timeout) -> void {
    ULONG_PTR key = 0;
    DWORD bytesTransferred = 0;
    LPOVERLAPPED overlapped = nullptr;

    BOOL ok = ::GetQueuedCompletionStatus(mIocpFd, &bytesTransferred, &key, &overlapped, timeout);
    DWORD error = ERROR_SUCCESS;
    if (!ok) {
        error = ::GetLastError();
        if (error == WAIT_TIMEOUT) {
            return;
        }
    }

    if (key) {
        // When key is not 0, it means it is a function pointer
        ILIAS_TRACE("IOCP", "Call callback function ({}, {})", (void*)key, (void*)overlapped);
        ILIAS_ASSERT(bytesTransferred == 0x114514);
        auto fn = reinterpret_cast<void (*)(void *)>(key);
        fn(overlapped);
        return;
    }

    if (overlapped) {
        auto lap = static_cast<IocpOverlapped*>(overlapped);
        ILIAS_ASSERT(lap->checkMagic());                     
        lap->onCompleteCallback(lap, error, bytesTransferred);
    }
    else {
        ILIAS_WARN("IOCP", "GetQueuedCompletionStatus returned nullptr overlapped, Error {}", error);
    }
}

auto IocpContext::processCompletionEx(DWORD timeout) -> void {
    if (mEntriesIdx >= mEntriesSize) { // We need more entries
        if (!mEntries) [[unlikely]] {
            mEntries = std::make_unique<::OVERLAPPED_ENTRY[]>(mEntriesCapacity);
        }
        if (!::GetQueuedCompletionStatusEx(mIocpFd, mEntries.get(), mEntriesCapacity, &mEntriesSize, timeout, TRUE)) {
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
    for (; mEntriesIdx < mEntriesSize; ++mEntriesIdx) {
        const auto &bytesTransferred = mEntries[mEntriesIdx].dwNumberOfBytesTransferred;
        const auto &overlapped = mEntries[mEntriesIdx].lpOverlapped;
        const auto &key = mEntries[mEntriesIdx].lpCompletionKey;
        if (key) {
            // When key is not 0, it means it is a function pointer
            ILIAS_TRACE("IOCP", "Call callback function ({}, {})", (void*)key, (void*)overlapped);
            ILIAS_ASSERT(bytesTransferred == 0x114514);
            auto fn = reinterpret_cast<void (*)(void *)>(key);
            fn(overlapped);
            continue;
        }
        if (overlapped) {
            auto lap = static_cast<IocpOverlapped*>(overlapped);
            auto status = overlapped->Internal; //< Acroding to Microsoft, it stores the error code, BUT NTSTATUS
            auto error = mNt.RtlNtStatusToDosError(status);
            ILIAS_ASSERT(lap->checkMagic());
            lap->onCompleteCallback(lap, error, bytesTransferred);
        }
        else {
            ILIAS_WARN("IOCP", "GetQueuedCompletionStatusEx returned nullptr overlapped, idx {}", mEntriesIdx);
        }
    }
}

auto IocpContext::sleep(uint64_t ms) -> Task<void> {
    co_return co_await mService.sleep(ms);
}

#pragma region Context
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
        if (::CreateIoCompletionPort(fd, mIocpFd, 0, 0) != mIocpFd) {
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
        if (auto ret = WSAGetExtensionFnPtr(nfd->sockfd, WSAID_CONNECTEX, &nfd->sock.ConnectEx); !ret) {
            return Err(ret.error());
        }
        if (auto ret = WSAGetExtensionFnPtr(nfd->sockfd, WSAID_ACCEPTEX, &nfd->sock.AcceptEx); !ret) {
            return Err(ret.error());
        }
        if (auto ret = WSAGetExtensionFnPtr(nfd->sockfd, WSAID_DISCONNECTEX, &nfd->sock.DisconnectEx); !ret) {
            return Err(ret.error());
        }
        if (auto ret = WSAGetExtensionFnPtr(nfd->sockfd, WSAID_TRANSMITFILE, &nfd->sock.TransmitFile); !ret) {
            return Err(ret.error());
        }
        if (auto ret = WSAGetExtensionFnPtr(nfd->sockfd, WSAID_GETACCEPTEXSOCKADDRS, &nfd->sock.GetAcceptExSockaddrs); !ret) {
            return Err(ret.error());
        }
        if (auto ret = WSAGetExtensionFnPtr(nfd->sockfd, WSAID_TRANSMITPACKETS,&nfd->sock.TransmitPackets); !ret) {
            return Err(ret.error());
        }
        if (auto ret = WSAGetExtensionFnPtr(nfd->sockfd, WSAID_WSARECVMSG, &nfd->sock.WSARecvMsg); !ret) {
            return Err(ret.error());
        }
        if (auto ret = WSAGetExtensionFnPtr(nfd->sockfd, WSAID_WSASENDMSG, &nfd->sock.WSASendMsg); !ret) {
            return Err(ret.error());
        }

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
    auto _ = cancel(nfd);
    delete nfd;
    return {};
}

auto IocpContext::cancel(IoDescriptor *fd) -> IoResult<void> {
    auto nfd = static_cast<IocpDescriptor*>(fd);
    ILIAS_TRACE("IOCP", "Cancelling fd: {}", nfd->handle);
    if (!CancelIoEx(nfd->handle, nullptr)) {
        auto err = ::GetLastError();
        if (err != ERROR_NOT_FOUND) { //< It's ok if the Io is not found, no any pending IO
            ILIAS_WARN("IOCP", "Failed to cancel Io on fd: {}, error: {}", nfd->handle, err);
            return Err(SystemError(err));
        }
    }
    return {};
}

#pragma region Fs
auto IocpContext::read(IoDescriptor *fd, MutableBuffer buffer, std::optional<size_t> offset) -> IoTask<size_t> {
    auto nfd = static_cast<IocpDescriptor*>(fd);
    if (nfd->type == IoDescriptor::Tty) { //< MSDN says console only can use blocking IO, use we use threadpool to execute it
        auto token = co_await this_coro::stopToken();
        auto val = co_await blocking([&]() {
            return ioCall(token, [&]() -> IoResult<size_t> {
                ::DWORD readed = 0;
                if (::ReadFile(nfd->handle, buffer.data(), buffer.size(), &readed, nullptr)) {
                    return readed;
                }
                return Err(SystemError::fromErrno());
            });
        });
        if (val == Err(SystemError::Canceled)) {
            co_await this_coro::stopped(); // Try set the context to stopped
        }
        co_return val;
    }
    co_return co_await IocpReadAwaiter(nfd->handle, buffer, offset);
}


auto IocpContext::write(IoDescriptor *fd, Buffer buffer, std::optional<size_t> offset) -> IoTask<size_t> {
    auto nfd = static_cast<IocpDescriptor*>(fd);
    if (nfd->type == IoDescriptor::Tty) { //< MSDN says console only can use blocking IO, use we use threadpool to execute it
        auto token = co_await this_coro::stopToken();
        auto val = co_await blocking([&]() {
            return ioCall(token, [&]() -> IoResult<size_t> {
                ::DWORD written = 0;
                if (::WriteFile(nfd->handle, buffer.data(), buffer.size(), &written, nullptr)) {
                    return written;
                }
                return Err(SystemError::fromErrno());
            });
        });
        if (val == Err(SystemError::Canceled)) {
            co_await this_coro::stopped(); // Try set the context to stopped
        }
        co_return val;
    }
    co_return co_await IocpWriteAwaiter(nfd->handle, buffer, offset);
}

#pragma region Net
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

#pragma region Poll
auto IocpContext::poll(IoDescriptor *fd, uint32_t events) -> IoTask<uint32_t> {
    auto nfd = static_cast<IocpDescriptor*>(fd);
    if (nfd->type != IoDescriptor::Socket || mAfdDevice == INVALID_HANDLE_VALUE) {
        co_return Err(IoError::OperationNotSupported);
    }
    co_return co_await AfdPollAwaiter(mAfdDevice, nfd->sockfd, events);
}

#pragma region NamedPipe
auto IocpContext::connectNamedPipe(IoDescriptor *fd) -> IoTask<void> {
    auto nfd = static_cast<IocpDescriptor*>(fd);
    if (nfd->type != IoDescriptor::Pipe) {
        co_return Err(IoError::OperationNotSupported);
    }
    co_return co_await IocpConnectPipeAwaiter(nfd->handle);
}

#pragma region WaitObject
auto IocpContext::waitObject(HANDLE object) -> IoTask<void> {
    do {
        if (!mNt.hasWaitCompletionPacket()) { // NtCreateWaitCompletionPacket, available in Windows 8
            break;
        }
        // Try to get a completion packet from the pool, if not, create a new one
        HANDLE packet = nullptr;
        if (mCompletionPackets.empty()) {
            auto status = mNt.NtCreateWaitCompletionPacket(&packet, GENERIC_ALL, nullptr);
            if (FAILED(status)) {
                ILIAS_ERROR("Win32", "NtCreateWaitCompletionPacket failed: {}", SystemError(mNt.RtlNtStatusToDosError(status)));
                break;
            }
        }
        else {
            packet = mCompletionPackets.front();
            mCompletionPackets.pop_front();
        }
        ILIAS_ASSERT(packet);

        // Create an guard
        auto guard = ScopeExit([packet, this]() {
            if (mCompletionPackets.size() < mCompletionPacketsPoolSize) {
                mCompletionPackets.push_back(packet);
                return;
            }
            ::CloseHandle(packet);
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
                if (alreadySignaled) {
                    return false; // If already signaled, we don't need to wait, just resume
                }
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
                auto status = nt->NtCancelWaitCompletionPacket(packet, FALSE);
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
                        ILIAS_ERROR("Win32", "NtCancelWaitCompletionPacket failed: {}", SystemError(nt->RtlNtStatusToDosError(status)));
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
            NtDll *nt;

            // Status
            runtime::CoroHandle handle;
            runtime::StopRegistration reg;
            DWORD error = 0;
        };

        Awaiter awaiter;
        awaiter.iocp = mIocpFd;
        awaiter.packet = packet;
        awaiter.object = object;
        awaiter.nt = &mNt;
        co_return co_await awaiter;
    }
    while (false);
    // Fallback to win32 API
    co_return co_await IoContext::waitObject(object);
}

} // namespace win32


#pragma region Default IoContext
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

auto IoContext::connectNamedPipe(IoDescriptor *fd) -> IoTask<void> {
    co_return Err(IoError::OperationNotSupported);
}

ILIAS_NS_END