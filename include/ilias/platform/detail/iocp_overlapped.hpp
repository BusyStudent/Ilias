/**
 * @file iocp_overlapped.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Provides a public api set for IOCP Interop
 * @version 0.1
 * @date 2024-08-17
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/detail/win32defs.hpp>
#include <ilias/cancellation_token.hpp>
#include <ilias/io/system_error.hpp>
#include <ilias/task/task.hpp>
#include <ilias/log.hpp>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <MSWSock.h>

ILIAS_NS_BEGIN

namespace detail {

/**
 * @brief A Dispatchable IOCP Object, we add a callback to it
 * 
 */
class IocpOverlapped : public ::OVERLAPPED {
public:
    IocpOverlapped() {
        ::memset(static_cast<::OVERLAPPED*>(this), 0, sizeof(::OVERLAPPED));
    }

    /**
     * @brief Set the Overlapped Offset object
     * 
     * @param offset 
     */
    auto setOffset(uint64_t offset) -> void {
        ::ULARGE_INTEGER integer;
        integer.QuadPart = offset;
        Offset = integer.LowPart;
        OffsetHigh = integer.HighPart;
    }

    /**
     * @brief Get the Overlapped pointer
     * 
     * @return IOCPOverlapped* 
     */
    auto overlapped() -> IocpOverlapped * {
        return this;
    }

    /**
     * @brief Check the magic number, for checking if this pointer is valid
     * 
     * @return true 
     * @return false 
     */
    auto checkMagic() const -> bool {
        return magic == 0x0721;
    }

    /**
     * @brief The callback when the IOCP is completed
     * 
     * @param self The pointer to self
     * @param dwError The error code (ERROR_SUCCESS if success)
     * @param dwBytesTransferred The bytes transferred
     * 
     */
    void (*onCompleteCallback)(IocpOverlapped *self, DWORD dwError, DWORD dwBytesTransferred) = nullptr;

    /**
     * @brief The magic number to check if this is a valid IocpOverlapped
     * 
     */
    uint32_t magic = 0x0721;
};

/**
 * @brief The common part of Generic Iocp Awatier template
 * 
 */
class IocpAwaiterBase : public IocpOverlapped {
public:
    IocpAwaiterBase(SOCKET sockfd) {
        mSockfd = sockfd;
    }

    IocpAwaiterBase(HANDLE handle) {
        mHandle = handle;
    }

    auto await_suspend(CoroHandle caller) -> void {
        mCaller = caller;
        onCompleteCallback = completeCallback;
        mRegistration = mCaller.cancellationToken().register_(onCancel, this);
    }

    auto sockfd() const -> SOCKET {
        return mSockfd;
    }

    auto handle() const -> HANDLE {
        return mHandle;
    }

    auto bytesTransferred() -> DWORD & {
        return mBytesTransferred;
    }
private:
    static auto completeCallback(IocpOverlapped *_self, DWORD dwError, DWORD dwBytesTransferred) -> void {
        auto self = static_cast<IocpAwaiterBase*>(_self);
        ILIAS_TRACE("IOCP", "IOCP Compelete callbacked, Error: {}, Bytes Transferred: {}", err2str(dwError), dwBytesTransferred);
        self->mError = dwError;
        self->mBytesTransferred = dwBytesTransferred;
        self->mCaller.resume();
    }
    static auto onCancel(void *_self) -> void {
        auto self = static_cast<IocpAwaiterBase*>(_self);
        auto err = ::CancelIoEx(self->mHandle, self->overlapped());
        if (!err) {
            ILIAS_WARN("IOCP", "CancelIoEx failed, Error: {}", err2str(::GetLastError()));
        }
    }
#if !defined(ILIAS_NO_FORMAT)
    static auto err2str(DWORD err) -> std::string {
        if (err == ERROR_SUCCESS) {
            return "(0, OK)";
        }
        return fmtlib::format("({}, {})", err, SystemError(err).toString());
    }
#endif

    union {
        ::HANDLE mHandle;
        ::SOCKET mSockfd;
    };
    ::DWORD mError = 0;
    ::DWORD mBytesTransferred = 0;
    CoroHandle mCaller;
    CancellationToken::Registration mRegistration;
template <typename T>
friend class IocpAwaiter;
};

/**
 * @brief The Generic Iocp Awatier template, used for CRTP
 * 
 * @tparam T 
 */
template <typename T>
class IocpAwaiter : public IocpAwaiterBase {
public:
    IocpAwaiter(SOCKET sockfd) : IocpAwaiterBase(sockfd) { }

    IocpAwaiter(HANDLE handle) : IocpAwaiterBase(handle) { }

    auto await_ready() -> bool {
        if (static_cast<T*>(this)->onSubmit()) {
            return true;
        }
        mError = ::GetLastError();
        return mError != ERROR_IO_PENDING; //< Pending means this job is still in progress
    }

    auto await_resume() {
        return static_cast<T*>(this)->onComplete(mError, mBytesTransferred);
    }
};

} // namespace detail

ILIAS_NS_END