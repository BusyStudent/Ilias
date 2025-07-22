/**
 * @file iocp_afd.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Impl the iocp polling using the windows //Device/Afd device.
 * @version 0.1
 * @date 2024-08-17
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/io/system_error.hpp>
#include <ilias/net/system.hpp>
#include <ilias/log.hpp>
#include "overlapped.hpp"
#include "ntdll.hpp"

ILIAS_NS_BEGIN

namespace win32 {

enum AfdPoll {
    AFD_POLL_RECEIVE           = 0x0001,
    AFD_POLL_RECEIVE_EXPEDITED = 0x0002,
    AFD_POLL_SEND              = 0x0004,
    AFD_POLL_DISCONNECT        = 0x0008,
    AFD_POLL_ABORT             = 0x0010,
    AFD_POLL_LOCAL_CLOSE       = 0x0020,
    AFD_POLL_ACCEPT            = 0x0080,
    AFD_POLL_CONNECT_FAIL      = 0x0100,
};

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

// By wepoll implementation, using DeviceIoControl
#define AFD_POLL               9
#define IOCTL_AFD_POLL 0x00012024

/**
 * @brief The awaiter used to poll the fd
 * 
 */
class AfdPollAwaiter final : public IocpAwaiter<AfdPollAwaiter> {
public:
    AfdPollAwaiter(HANDLE device, SOCKET sock, uint32_t events) : IocpAwaiter(sock), mDevice(device) {
        // Fill the info
        mInfo.Exclusive = FALSE; //< Try false?
        mInfo.NumberOfHandles = 1; //< Only one socket
        mInfo.Timeout.QuadPart = INT64_MAX;
        mInfo.Handles[0].Handle = handle();
        mInfo.Handles[0].Status = 0;
        mInfo.Handles[0].Events = AFD_POLL_LOCAL_CLOSE; //< When socket was ::closesocket(sock)

        // Translate the events to AFD events
        if (events & PollEvent::In) {
            mInfo.Handles[0].Events |= (AFD_POLL_RECEIVE | AFD_POLL_DISCONNECT | AFD_POLL_ACCEPT | AFD_POLL_ABORT);
        }
        if (events & PollEvent::Out) {
            mInfo.Handles[0].Events |= (AFD_POLL_SEND | AFD_POLL_CONNECT_FAIL);
        }
        if (events & PollEvent::Pri) {
            mInfo.Handles[0].Events |= (AFD_POLL_ABORT | AFD_POLL_CONNECT_FAIL);
        }
    }

    auto onSubmit() -> bool {
        ILIAS_TRACE("IOCP", "Poll {} on sockfd {}", afdToString(mInfo.Handles[0].Events), sockfd());
        return ::DeviceIoControl(mDevice, IOCTL_AFD_POLL, &mInfo, sizeof(mInfo), &mRInfo, sizeof(mRInfo), nullptr, overlapped());
    }

    auto onComplete(DWORD error, DWORD bytesTransferred) -> IoResult<uint32_t> {
        ILIAS_TRACE("IOCP", "Poll {} on sockfd {} completed, Error {}", afdToString(mInfo.Handles[0].Events), sockfd(), error);
        if (error != ERROR_SUCCESS) {
            return Err(SystemError(error));
        }
        uint32_t revents = 0;
        ULONG afdEvents = mRInfo.Handles[0].Events;
        if (afdEvents & (AFD_POLL_LOCAL_CLOSE)) {
            // User close the socket
            return Err(IoError::Canceled);
        }
        if (afdEvents & (AFD_POLL_RECEIVE | AFD_POLL_DISCONNECT | AFD_POLL_ACCEPT | AFD_POLL_ABORT)) {
            revents |= PollEvent::In;
        }
        if (afdEvents & (AFD_POLL_SEND | AFD_POLL_CONNECT_FAIL)) {
            revents |= PollEvent::Out;
        }
        if (afdEvents & (AFD_POLL_ABORT | AFD_POLL_CONNECT_FAIL)) {
            revents |= PollEvent::Error;
        }
        // It think disconnect is Hup
        if (afdEvents & (AFD_POLL_DISCONNECT)) {
            revents |= PollEvent::Hup;
        }
        return revents;
    }

    // For debugging
    static auto afdToString(ULONG afdEvents) -> std::string {
        std::string ret;
        if (afdEvents & AFD_POLL_RECEIVE) {
            ret += "AFD_POLL_RECEIVE | ";
        }
        if (afdEvents & AFD_POLL_RECEIVE_EXPEDITED) {
            ret += "AFD_POLL_RECEIVE_EXPEDITED | ";
        }
        if (afdEvents & AFD_POLL_SEND) {
            ret += "AFD_POLL_SEND | ";
        }
        if (afdEvents & AFD_POLL_DISCONNECT) {
            ret += "AFD_POLL_DISCONNECT | ";
        }
        if (afdEvents & AFD_POLL_ABORT) {
            ret += "AFD_POLL_ABORT | ";
        }
        if (afdEvents & AFD_POLL_LOCAL_CLOSE) {
            ret += "AFD_POLL_LOCAL_CLOSE | ";
        }
        if (afdEvents & AFD_POLL_ACCEPT) {
            ret += "AFD_POLL_ACCEPT | ";
        }
        if (afdEvents & AFD_POLL_CONNECT_FAIL) {
            ret += "AFD_POLL_CONNECT_FAIL | ";
        }
        if (!ret.empty()) {
            ret.pop_back();
            ret.pop_back();
            ret.pop_back();
        }
        return ret;
    }
private:
    HANDLE mDevice;
    AFD_POLL_INFO mInfo;
    AFD_POLL_INFO mRInfo; //< Result
};

// Open the afd device for impl poll
inline auto afdOpenDevice(NtDll &dll) -> Result<HANDLE, SystemError> {
    // Open the afd device for impl poll
    wchar_t path [] = L"\\Device\\Afd\\Ilias";
    ::HANDLE device = nullptr;
    ::UNICODE_STRING deviceName {
        .Length = sizeof(path) - sizeof(path[0]),
        .MaximumLength = sizeof(path),
        .Buffer = path
    };
    ::OBJECT_ATTRIBUTES objAttr {
        .Length = sizeof(OBJECT_ATTRIBUTES),
        .RootDirectory = nullptr,
        .ObjectName = &deviceName,
        .Attributes = 0,
        .SecurityDescriptor = nullptr,
        .SecurityQualityOfService = nullptr
    };
    ::IO_STATUS_BLOCK statusBlock {};
    auto status = dll.NtCreateFile(
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
        auto winerr = dll.RtlNtStatusToDosError(status);
        return Err(SystemError(winerr));
    }
    return device;
}

} // namespace win32

ILIAS_NS_END