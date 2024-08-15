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

#include <ilias/cancellation_token.hpp>
#include <ilias/detail/timer.hpp>
#include <ilias/task/executor.hpp>
#include <ilias/task/task.hpp>
#include <ilias/net/endpoint.hpp>
#include <ilias/net/system.hpp>
#include <ilias/net/sockfd.hpp>
#include <ilias/io/context.hpp>
#include <ilias/log.hpp>

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <Windows.h>

ILIAS_NS_BEGIN

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
    auto setOffset(size_t offset) -> void {
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
     * @brief The callback when the IOCP is completed
     * 
     * @param self The pointer to self
     * @param dwError The error code (ERROR_SUCCESS if success)
     * @param dwBytesTransferred The bytes transferred
     * 
     */
    void (*onCompleteCallback)(IocpOverlapped *self, DWORD dwError, DWORD dwBytesTransferred) = nullptr;
};

/**
 * @brief The IOCP descriptor
 * 
 */
class IocpDescriptor final : public IoDescriptor {
public:
    union {
        SOCKET sockfd;
        HANDLE handle;
    };

    HANDLE baseHandle = INVALID_HANDLE_VALUE; //< Get by ::WSAIoctl 

    DWORD fileType = 0; //< Get by ::GetFileType
};

/**
 * @brief The iocp implementation of the io context
 * 
 */
class IocpContext final : public IoContext {
public:
    IocpContext();
    IocpContext(const IocpContext &) = delete;
    ~IocpContext();
private:
    HANDLE mIocpFd = INVALID_HANDLE_VALUE;
    HANDLE mAfdDevice = INVALID_HANDLE_VALUE;
    detail::TimerService mService {*this};
};


ILIAS_NS_END