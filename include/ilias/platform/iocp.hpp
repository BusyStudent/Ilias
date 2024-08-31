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

        int family = 0;
        int stype = 0;
        int protocol = 0;
    };

    //< For console
    struct {
        HANDLE workThread = INVALID_HANDLE_VALUE;
    };
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

    // < For IoContext
    auto addDescriptor(fd_t fd, IoDescriptor::Type type) -> Result<IoDescriptor*> override;
    auto removeDescriptor(IoDescriptor* fd) -> Result<void> override;

    auto sleep(uint64_t ms) -> Task<void> override;

    auto read(IoDescriptor *fd, std::span<std::byte> buffer, std::optional<size_t> offset) -> Task<size_t> override;
    auto write(IoDescriptor *fd, std::span<const std::byte> buffer, std::optional<size_t> offset) -> Task<size_t> override;

    auto accept(IoDescriptor *fd, IPEndpoint *endpoint) -> Task<socket_t> override;
    auto connect(IoDescriptor *fd, const IPEndpoint &endpoint) -> Task<void> override;

    auto sendto(IoDescriptor *fd, std::span<const std::byte> buffer, int flags, const IPEndpoint *endpoint) -> Task<size_t> override;
    auto recvfrom(IoDescriptor *fd, std::span<std::byte> buffer, int flags, IPEndpoint *endpoint) -> Task<size_t> override;

    auto poll(IoDescriptor *fd, uint32_t event) -> Task<uint32_t> override;
private:
    auto processCompletion(DWORD timeout) -> void;
    auto processCompletionEx(DWORD timeout) -> void;

    SockInitializer mInit;
    HANDLE mIocpFd = INVALID_HANDLE_VALUE;
    detail::TimerService mService {*this};
    detail::AfdDevice    mAfdDevice;

    // NT Functions
    decltype(::RtlNtStatusToDosError) *mRtlNtStatusToDosError = nullptr;
};

inline IocpContext::IocpContext() {
    mIocpFd = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (mAfdDevice.isOpen()) {
        if (::CreateIoCompletionPort(mAfdDevice.handle(), mIocpFd, 0, 0) != mIocpFd) {
            ILIAS_WARN("IOCP", "Failed to add afd device handle to iocp: {}", ::GetLastError());
        }
        ::SetFileCompletionNotificationModes(mAfdDevice.handle(), FILE_SKIP_COMPLETION_PORT_ON_SUCCESS | FILE_SKIP_SET_EVENT_ON_HANDLE);
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
    while (!token.isCancelled()) {
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
    std::array<OVERLAPPED_ENTRY, 64> entries;
    ULONG count = 0;
    BOOL ok = ::GetQueuedCompletionStatusEx(mIocpFd, entries.data(), entries.size(), &count, timeout, TRUE);
    if (!ok) {
        auto error = ::GetLastError();
        if (error == WAIT_TIMEOUT) {
            return;
        }
        ILIAS_WARN("IOCP", "GetQueuedCompletionStatusEx failed, Error {}", error);
        return;
    }
    for (ULONG i = 0; i < count; ++i) {
        const auto &bytesTransferred = entries[i].dwNumberOfBytesTransferred;
        const auto &overlapped = entries[i].lpOverlapped;
        const auto &key = entries[i].lpCompletionKey;
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
            ILIAS_WARN("IOCP", "GetQueuedCompletionStatusEx returned nullptr overlapped, idx {}", i);
        }
    }
}

inline auto IocpContext::sleep(uint64_t ms) -> Task<void> {
    return mService.sleep(ms);
}

#pragma region Conttext
inline auto IocpContext::addDescriptor(fd_t fd, IoDescriptor::Type type) -> Result<IoDescriptor*> {
    if (fd == nullptr || fd == INVALID_HANDLE_VALUE) {
        return Unexpected(Error::InvalidArgument);
    }
    if (type == IoDescriptor::Unknown) {
        switch (::GetFileType(fd)) {
            case FILE_TYPE_CHAR: type = IoDescriptor::Tty;
            case FILE_TYPE_DISK: type = IoDescriptor::File;
            case FILE_TYPE_PIPE: type = IoDescriptor::Socket;
            case FILE_TYPE_UNKNOWN: return Unexpected(SystemError::fromErrno());
            default: return Unexpected(Error::InvalidArgument);
        }
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
        if (auto ret = detail::WSAGetExtensionFnPtr(nfd->sockfd, WSAID_CONNECTEX, &nfd->ConnectEx); !ret) {
            return Unexpected(ret.error());
        }
        if (auto ret = detail::WSAGetExtensionFnPtr(nfd->sockfd, WSAID_ACCEPTEX, &nfd->AcceptEx); !ret) {
            return Unexpected(ret.error());
        }
        if (auto ret = detail::WSAGetExtensionFnPtr(nfd->sockfd, WSAID_DISCONNECTEX, &nfd->DisconnectEx); !ret) {
            return Unexpected(ret.error());
        }
        if (auto ret = detail::WSAGetExtensionFnPtr(nfd->sockfd, WSAID_TRANSMITFILE, &nfd->TransmitFile); !ret) {
            return Unexpected(ret.error());
        }
        if (auto ret = detail::WSAGetExtensionFnPtr(nfd->sockfd, WSAID_GETACCEPTEXSOCKADDRS, &nfd->GetAcceptExSockaddrs); !ret) {
            return Unexpected(ret.error());
        }

        ::WSAPROTOCOL_INFOW info {};
        ::socklen_t infoSize = sizeof(info);
        if (::getsockopt(nfd->sockfd, SOL_SOCKET, SO_PROTOCOL_INFOW, reinterpret_cast<char*>(&info), &infoSize) == SOCKET_ERROR) {
            return Unexpected(SystemError::fromErrno());
        }
        nfd->family = info.iAddressFamily;
        nfd->stype = info.iSocketType;
        nfd->protocol = info.iProtocol;
    }
    ILIAS_TRACE("IOCP", "Adding fd: {} to completion port", fd);
    return nfd.release();
}

inline auto IocpContext::removeDescriptor(IoDescriptor *descriptor) -> Result<void> {
    auto nfd = static_cast<detail::IocpDescriptor*>(descriptor);
    ILIAS_TRACE("IOCP", "Removing fd: {} from completion port", nfd->handle);
    delete nfd;
    return {};
}

#pragma region Fs
inline auto IocpContext::read(IoDescriptor *fd, std::span<std::byte> buffer, std::optional<size_t> offset) -> Task<size_t> {
    auto nfd = static_cast<detail::IocpDescriptor*>(fd);
    co_return co_await detail::IocpReadAwaiter(nfd->handle, buffer, offset);
}


inline auto IocpContext::write(IoDescriptor *fd, std::span<const std::byte> buffer, std::optional<size_t> offset) -> Task<size_t> {
    auto nfd = static_cast<detail::IocpDescriptor*>(fd);
    co_return co_await detail::IocpWriteAwaiter(nfd->handle, buffer, offset);
}

#pragma region Net
inline auto IocpContext::accept(IoDescriptor *fd, IPEndpoint *endpoint) -> Task<socket_t> {
    auto nfd = static_cast<detail::IocpDescriptor*>(fd);
    if (nfd->type != IoDescriptor::Socket) {
        co_return Unexpected(Error::OperationNotSupported);
    }
    co_return co_await detail::IocpAcceptAwaiter(nfd->sockfd, endpoint, nfd->AcceptEx, nfd->GetAcceptExSockaddrs);
}

inline auto IocpContext::connect(IoDescriptor *fd, const IPEndpoint &endpoint) -> Task<void> {
    auto nfd = static_cast<detail::IocpDescriptor*>(fd);
    if (nfd->type != IoDescriptor::Socket) {
        co_return Unexpected(Error::OperationNotSupported);
    }
    if (!endpoint.isValid()) {
        co_return Unexpected(Error::InvalidArgument);
    }
    co_return co_await detail::IocpConnectAwaiter(nfd->sockfd, endpoint, nfd->ConnectEx);
}

inline auto IocpContext::sendto(IoDescriptor *fd, std::span<const std::byte> buffer, int flags, const IPEndpoint *endpoint) -> Task<size_t> {
    auto nfd = static_cast<detail::IocpDescriptor*>(fd);
    if (nfd->type != IoDescriptor::Socket) {
        co_return Unexpected(Error::OperationNotSupported);
    }
    if (endpoint && !endpoint->isValid()) {
        co_return Unexpected(Error::InvalidArgument);
    }
    co_return co_await detail::IocpSendtoAwaiter(nfd->sockfd, buffer, flags, endpoint);
}

inline auto IocpContext::recvfrom(IoDescriptor *fd, std::span<std::byte> buffer, int flags, IPEndpoint *endpoint) -> Task<size_t> {
    auto nfd = static_cast<detail::IocpDescriptor*>(fd);
    if (nfd->type != IoDescriptor::Socket) {
        co_return Unexpected(Error::OperationNotSupported);
    }
    co_return co_await detail::IocpRecvfromAwaiter(nfd->sockfd, buffer, flags, endpoint);
}

#pragma region Poll
inline auto IocpContext::poll(IoDescriptor *fd, uint32_t events) -> Task<uint32_t> {
    auto nfd = static_cast<detail::IocpDescriptor*>(fd);
    if (nfd->type != IoDescriptor::Socket || !mAfdDevice.isOpen()) {
        co_return Unexpected(Error::OperationNotSupported);
    }
    co_return co_await detail::AfdPollAwaiter(mAfdDevice, nfd->sockfd, events);
}

ILIAS_NS_END