/**
 * @file iocp.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Impl the iocp asyncio on the windows platform
 * @version 0.1
 * @date 2024-08-12
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#pragma once

#include <ilias/platform/detail/iocp_sock.hpp>
#include <ilias/platform/detail/iocp_afd.hpp>
#include <ilias/platform/detail/iocp_fs.hpp>
#include <ilias/cancellation_token.hpp>
#include <ilias/detail/timer.hpp>
#include <ilias/task/executor.hpp>
#include <ilias/task/task.hpp>
#include <ilias/net/endpoint.hpp>
#include <ilias/net/system.hpp>
#include <ilias/net/sockfd.hpp>
#include <ilias/net/msg.hpp>
#include <ilias/io/fd_utils.hpp>
#include <ilias/io/context.hpp>
#include <ilias/log.hpp>
#include <algorithm>
#include <array>

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <Windows.h>
#include <winternl.h>

#if !defined(FILE_SKIP_COMPLETION_PORT_ON_SUCCESS)
    #error "ilias requires FILE_SKIP_COMPLETION_PORT_ON_SUCCESS support"
#endif


ILIAS_NS_BEGIN

namespace detail {

/**
 * @brief The IOCP descriptor, if alloc is too frequently, maybe we can use memory pool
 * 
 */
class IocpDescriptor final : public IoDescriptor {
public:
    union {
        ::SOCKET sockfd;
        ::HANDLE handle;
    };

    IoDescriptor::Type type = Unknown;

    // < For Socket data
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

} // namespace detail

/**
 * @brief The iocp implementation of the io context
 * 
 */
class IocpContext final : public IoContext {
public:
    IocpContext();
    IocpContext(const IocpContext &) = delete;
    ~IocpContext();

    //< For Executor
    auto post(void (*fn)(void *), void *args) -> void override;
    auto run(CancellationToken &token) -> void override;

    //< For IoContext
    auto addDescriptor(fd_t fd, IoDescriptor::Type type) -> Result<IoDescriptor*> override;
    auto removeDescriptor(IoDescriptor* fd) -> Result<void> override;
    auto cancel(IoDescriptor *fd) -> Result<void> override;

    auto sleep(uint64_t ms) -> IoTask<void> override;

    auto read(IoDescriptor *fd, std::span<std::byte> buffer, std::optional<size_t> offset) -> IoTask<size_t> override;
    auto write(IoDescriptor *fd, std::span<const std::byte> buffer, std::optional<size_t> offset) -> IoTask<size_t> override;

    auto accept(IoDescriptor *fd, MutableEndpointView endpoint) -> IoTask<socket_t> override;
    auto connect(IoDescriptor *fd, EndpointView endpoint) -> IoTask<void> override;

    auto sendto(IoDescriptor *fd, std::span<const std::byte> buffer, int flags, EndpointView endpoint) -> IoTask<size_t> override;
    auto recvfrom(IoDescriptor *fd, std::span<std::byte> buffer, int flags, MutableEndpointView endpoint) -> IoTask<size_t> override;

    auto sendmsg(IoDescriptor *fd, const MsgHdr &msg, int flags) -> IoTask<size_t> override;
    auto recvmsg(IoDescriptor *fd, MsgHdr &msg, int flags) -> IoTask<size_t> override;

    auto poll(IoDescriptor *fd, uint32_t event) -> IoTask<uint32_t> override;
    auto connectNamedPipe(IoDescriptor *fd) -> IoTask<void> override;
private:
    auto processCompletion(DWORD timeout) -> void;
    auto processCompletionEx(DWORD timeout) -> void;

    SockInitializer mInit;
    HANDLE mIocpFd = INVALID_HANDLE_VALUE;
    detail::TimerService mService;
    detail::AfdDevice    mAfdDevice;

    // Batching
    ULONG mEntriesIdx  = 0; // The index of the current entry (for dispatch)
    ULONG mEntriesSize = 0; // The number of entries valid in the mBatchEntries array
    ULONG mEntriesCapacity = 64; // The size of the mBatchEntries array
    std::unique_ptr<::OVERLAPPED_ENTRY[]> mEntries;

    // NT Functions
    decltype(::RtlNtStatusToDosError) *mRtlNtStatusToDosError = nullptr;
};

inline IocpContext::IocpContext() {
    mIocpFd = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (mAfdDevice.isOpen()) {
        if (::CreateIoCompletionPort(mAfdDevice.handle(), mIocpFd, 0, 0) != mIocpFd) {
            ILIAS_WARN("IOCP", "Failed to add afd device handle to iocp: {}", ::GetLastError());
        }
        if (!::SetFileCompletionNotificationModes(mAfdDevice.handle(), FILE_SKIP_COMPLETION_PORT_ON_SUCCESS | FILE_SKIP_SET_EVENT_ON_HANDLE)) {
            ILIAS_WARN("IOCP", "Failed to set completion notification modes: {}", ::GetLastError());
        }
    }
    if (auto mod = ::GetModuleHandleW(L"ntdll.dll"); mod) {
        mRtlNtStatusToDosError = reinterpret_cast<decltype(mRtlNtStatusToDosError)>(::GetProcAddress(mod, "RtlNtStatusToDosError"));
    }
}

inline IocpContext::~IocpContext() {
    if (mIocpFd != INVALID_HANDLE_VALUE) {
        if (!::CloseHandle(mIocpFd)) {
            ILIAS_WARN("IOCP", "Failed to close iocp handle: {}", ::GetLastError());
        }
    }
}

#pragma region Executor
inline auto IocpContext::post(void (*fn)(void *), void *args) -> void {
    if (!fn) [[unlikely]] {
        return;
    }
    ::PostQueuedCompletionStatus(
        mIocpFd, 
        0x114514, 
        reinterpret_cast<ULONG_PTR>(fn), 
        reinterpret_cast<LPOVERLAPPED>(args)
    );
}

inline auto IocpContext::run(CancellationToken &token) -> void {
    DWORD timeout = INFINITE;
    while (!token.isCancellationRequested()) {
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
inline auto IocpContext::processCompletion(DWORD timeout) -> void {
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
        auto lap = static_cast<detail::IocpOverlapped*>(overlapped);
        ILIAS_ASSERT(lap->checkMagic());                     
        lap->onCompleteCallback(lap, error, bytesTransferred);
    }
    else {
        ILIAS_WARN("IOCP", "GetQueuedCompletionStatus returned nullptr overlapped, Error {}", error);
    }
}

inline auto IocpContext::processCompletionEx(DWORD timeout) -> void {
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
            auto lap = static_cast<detail::IocpOverlapped*>(overlapped);
            auto status = overlapped->Internal; //< Acroding to Microsoft, it stores the error code, BUT NTSTATUS
            auto error = mRtlNtStatusToDosError(status);
            ILIAS_ASSERT(lap->checkMagic());
            lap->onCompleteCallback(lap, error, bytesTransferred);
        }
        else {
            ILIAS_WARN("IOCP", "GetQueuedCompletionStatusEx returned nullptr overlapped, idx {}", mEntriesIdx);
        }
    }
}

inline auto IocpContext::sleep(uint64_t ms) -> IoTask<void> {
    return mService.sleep(ms);
}

#pragma region Context
inline auto IocpContext::addDescriptor(fd_t fd, IoDescriptor::Type type) -> Result<IoDescriptor*> {
    if (fd == nullptr || fd == INVALID_HANDLE_VALUE) {
        ILIAS_ERROR("IOCP", "Invalid file descriptor in addDescriptor, fd = {}, type = {}", fd, type);
        return Unexpected(Error::InvalidArgument);
    }
    if (type == IoDescriptor::Unknown) {
        auto ret = fd_utils::type(fd);
        if (!ret) {
            return Unexpected(ret.error());
        }
        type = *ret;
    }

    // Try add it to the completion port
    if (type != IoDescriptor::Tty) {
        if (::CreateIoCompletionPort(fd, mIocpFd, 0, 0) != mIocpFd) {
            return Unexpected(SystemError::fromErrno());
        }

        if (!::SetFileCompletionNotificationModes(fd, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS | FILE_SKIP_SET_EVENT_ON_HANDLE)) {
            return Unexpected(SystemError::fromErrno());
        }
    }

    auto nfd = std::make_unique<detail::IocpDescriptor>();
    nfd->handle = fd;
    nfd->type = type;

    if (nfd->type == IoDescriptor::Socket) {
        // Get The ext function ptr
        if (auto ret = detail::WSAGetExtensionFnPtr(nfd->sockfd, WSAID_CONNECTEX, &nfd->sock.ConnectEx); !ret) {
            return Unexpected(ret.error());
        }
        if (auto ret = detail::WSAGetExtensionFnPtr(nfd->sockfd, WSAID_ACCEPTEX, &nfd->sock.AcceptEx); !ret) {
            return Unexpected(ret.error());
        }
        if (auto ret = detail::WSAGetExtensionFnPtr(nfd->sockfd, WSAID_DISCONNECTEX, &nfd->sock.DisconnectEx); !ret) {
            return Unexpected(ret.error());
        }
        if (auto ret = detail::WSAGetExtensionFnPtr(nfd->sockfd, WSAID_TRANSMITFILE, &nfd->sock.TransmitFile); !ret) {
            return Unexpected(ret.error());
        }
        if (auto ret = detail::WSAGetExtensionFnPtr(nfd->sockfd, WSAID_GETACCEPTEXSOCKADDRS, &nfd->sock.GetAcceptExSockaddrs); !ret) {
            return Unexpected(ret.error());
        }
        if (auto ret = detail::WSAGetExtensionFnPtr(nfd->sockfd, WSAID_TRANSMITPACKETS,&nfd->sock.TransmitPackets); !ret) {
            return Unexpected(ret.error());
        }
        if (auto ret = detail::WSAGetExtensionFnPtr(nfd->sockfd, WSAID_WSARECVMSG, &nfd->sock.WSARecvMsg); !ret) {
            return Unexpected(ret.error());
        }
        if (auto ret = detail::WSAGetExtensionFnPtr(nfd->sockfd, WSAID_WSASENDMSG, &nfd->sock.WSASendMsg); !ret) {
            return Unexpected(ret.error());
        }

        ::WSAPROTOCOL_INFOW info {};
        ::socklen_t infoSize = sizeof(info);
        if (::getsockopt(nfd->sockfd, SOL_SOCKET, SO_PROTOCOL_INFOW, reinterpret_cast<char*>(&info), &infoSize) == SOCKET_ERROR) {
            return Unexpected(SystemError::fromErrno());
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

inline auto IocpContext::removeDescriptor(IoDescriptor *descriptor) -> Result<void> {
    auto nfd = static_cast<detail::IocpDescriptor*>(descriptor);
    ILIAS_TRACE("IOCP", "Removing fd: {} from completion port", nfd->handle);
    if (!CancelIoEx(nfd->handle, nullptr)) {
        auto err = ::GetLastError();
        if (err != ERROR_NOT_FOUND) { //< It's ok if the Io is not found, no any pending IO
            ILIAS_WARN("IOCP", "Failed to cancel Io on fd: {}, error: {}", nfd->handle, err);
        }
    }
    delete nfd;
    return {};
}

inline auto IocpContext::cancel(IoDescriptor *fd) -> Result<void> {
    auto nfd = static_cast<detail::IocpDescriptor*>(fd);
    ILIAS_TRACE("IOCP", "Cancelling fd: {}", nfd->handle);
    if (!CancelIoEx(nfd->handle, nullptr)) {
        auto err = ::GetLastError();
        if (err != ERROR_NOT_FOUND) { //< It's ok if the Io is not found, no any pending IO
            return Unexpected(SystemError(err));
        }
    }
    return {};
}

#pragma region Fs
inline auto IocpContext::read(IoDescriptor *fd, std::span<std::byte> buffer, std::optional<size_t> offset) -> IoTask<size_t> {
    auto nfd = static_cast<detail::IocpDescriptor*>(fd);
    if (nfd->type == IoDescriptor::Tty) { //< MSDN says console only can use blocking IO, use we use thread pool to do it
        co_return co_await detail::IocpThreadReadAwaiter(nfd->handle, buffer);
    }
    co_return co_await detail::IocpReadAwaiter(nfd->handle, buffer, offset);
}


inline auto IocpContext::write(IoDescriptor *fd, std::span<const std::byte> buffer, std::optional<size_t> offset) -> IoTask<size_t> {
    auto nfd = static_cast<detail::IocpDescriptor*>(fd);
    if (nfd->type == IoDescriptor::Tty) { //< MSDN says console only can use blocking IO, use we use thread pool to do it
        co_return co_await detail::IocpThreadWriteAwaiter(nfd->handle, buffer);
    }
    co_return co_await detail::IocpWriteAwaiter(nfd->handle, buffer, offset);
}

#pragma region Net
inline auto IocpContext::accept(IoDescriptor *fd, MutableEndpointView endpoint) -> IoTask<socket_t> {
    auto nfd = static_cast<detail::IocpDescriptor*>(fd);
    if (nfd->type != IoDescriptor::Socket) {
        co_return Unexpected(Error::OperationNotSupported);
    }
    co_return co_await detail::IocpAcceptAwaiter(nfd->sockfd, endpoint, nfd->sock.AcceptEx, nfd->sock.GetAcceptExSockaddrs);
}

inline auto IocpContext::connect(IoDescriptor *fd, EndpointView endpoint) -> IoTask<void> {
    auto nfd = static_cast<detail::IocpDescriptor*>(fd);
    if (nfd->type != IoDescriptor::Socket) {
        co_return Unexpected(Error::OperationNotSupported);
    }
    if (!endpoint) {
        co_return Unexpected(Error::InvalidArgument);
    }
    co_return co_await detail::IocpConnectAwaiter(nfd->sockfd, endpoint, nfd->sock.ConnectEx);
}

inline auto IocpContext::sendto(IoDescriptor *fd, std::span<const std::byte> buffer, int flags, EndpointView endpoint) -> IoTask<size_t> {
    auto nfd = static_cast<detail::IocpDescriptor*>(fd);
    if (nfd->type != IoDescriptor::Socket) {
        co_return Unexpected(Error::OperationNotSupported);
    }
    co_return co_await detail::IocpSendtoAwaiter(nfd->sockfd, buffer, flags, endpoint);
}

inline auto IocpContext::recvfrom(IoDescriptor *fd, std::span<std::byte> buffer, int flags, MutableEndpointView endpoint) -> IoTask<size_t> {
    auto nfd = static_cast<detail::IocpDescriptor*>(fd);
    if (nfd->type != IoDescriptor::Socket) {
        co_return Unexpected(Error::OperationNotSupported);
    }
    co_return co_await detail::IocpRecvfromAwaiter(nfd->sockfd, buffer, flags, endpoint);
}

#pragma region Advance Net
inline auto IocpContext::sendmsg(IoDescriptor *fd, const MsgHdr &msg, int flags) -> IoTask<size_t> {
    auto nfd = static_cast<detail::IocpDescriptor*>(fd);
    if (nfd->type != IoDescriptor::Socket) {
        co_return Unexpected(Error::OperationNotSupported);
    }
    co_return co_await detail::IocpSendmsgAwaiter(nfd->sockfd, msg, flags, nfd->sock.WSASendMsg);
}

inline auto IocpContext::recvmsg(IoDescriptor *fd, MsgHdr &msg, int flags) -> IoTask<size_t> {
    auto nfd = static_cast<detail::IocpDescriptor*>(fd);
    if (nfd->type != IoDescriptor::Socket) {
        co_return Unexpected(Error::OperationNotSupported);
    }
    co_return co_await detail::IocpRecvmsgAwaiter(nfd->sockfd, msg, flags, nfd->sock.WSARecvMsg);
} 

#pragma region Poll
inline auto IocpContext::poll(IoDescriptor *fd, uint32_t events) -> IoTask<uint32_t> {
    auto nfd = static_cast<detail::IocpDescriptor*>(fd);
    if (nfd->type != IoDescriptor::Socket || !mAfdDevice.isOpen()) {
        co_return Unexpected(Error::OperationNotSupported);
    }
    co_return co_await detail::AfdPollAwaiter(mAfdDevice, nfd->sockfd, events);
}

#pragma region NamedPipe
inline auto IocpContext::connectNamedPipe(IoDescriptor *fd) -> IoTask<void> {
    auto nfd = static_cast<detail::IocpDescriptor*>(fd);
    if (nfd->type != IoDescriptor::Pipe) {
        co_return Unexpected(Error::OperationNotSupported);
    }
    co_return co_await detail::IocpConnectPipeAwaiter(nfd->handle);
}

ILIAS_NS_END