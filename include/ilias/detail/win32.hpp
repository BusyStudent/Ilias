/**
 * @file win32.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Provides Windows-specific utilities
 * @version 0.1
 * @date 2024-08-22
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/detail/win32defs.hpp>
#include <ilias/io/system_error.hpp>
#include <ilias/task/executor.hpp>
#include <ilias/task/task.hpp>
#include <ilias/log.hpp>
#include <atomic>

ILIAS_NS_BEGIN

namespace win32 {

/**
 * @brief The default cancel tags for wait object, default cancel is unregister wait
 * 
 */
struct DefaultCancel {
    auto operator ()() const noexcept -> void; // No-implementation, never called
};

/**
 * @brief Waiting for a object to be signaled (wrapping RegisterWaitForSingleObject)
 * 
 */
template <std::invocable T = DefaultCancel>
class WaitObject {
public:
    WaitObject(HANDLE handle, ULONG timeout = INFINITE, T op = {}) : 
        mHandle(handle), mMillseconds(timeout), mCancelOperation(std::move(op))
    {

    }

    auto await_ready() const noexcept {
        return false;
    }

    auto await_suspend(CoroHandle caller) -> bool {
        mCaller = caller;
        auto v = ::RegisterWaitForSingleObject(
            &mWaitHandle,
            mHandle,
            &WaitObject::completeCallback,
            this,
            mMillseconds,
            WT_EXECUTEONLYONCE
        );
        if (!v) {
            mWaitHandle = nullptr;
            return false;
        }
        mRegistration = caller.cancellationToken().register_(&WaitObject::cancelCallback, this);
        return true;
    }

    auto await_resume() -> Result<void> {
        if (mCanceled) {
            return Unexpected(Error::Canceled);
        }
        if (!mWaitHandle) { // Failed to register
            return Unexpected(SystemError::fromErrno());
        }
        if (!::UnregisterWaitEx(mWaitHandle, nullptr)) {
            ILIAS_ERROR("Win32", "Failed to unregister wait handle {}", ::GetLastError());
        }
        if (mTimedout) {
            return Unexpected(Error::TimedOut);
        }
        return {};
    }
private:
    static auto CALLBACK completeCallback(void *_self, BOOLEAN waitOrTimeout) -> void {
        auto self = static_cast<WaitObject*>(_self);
        if (self->mFlag.test_and_set()) {
            return; // Cancel already
        }
        self->mTimedout = waitOrTimeout;
        self->mCaller.schedule();
    }
    static auto cancelCallback(void *_self) -> void {
        auto self = static_cast<WaitObject*>(_self);
        if constexpr (std::is_same_v<T, DefaultCancel>) {
            self->doUnregisterCancellation();
        }
        else {
            self->mCancelOperation();
        }
    }

    auto doUnregisterCancellation() -> void {
        if (mFlag.test_and_set()) {
            return; // Completed already
        }
        if (!::UnregisterWaitEx(mWaitHandle, nullptr)) {
            ILIAS_ERROR("Win32", "Failed to unregister wait handle {}", ::GetLastError());
        }
        mWaitHandle = nullptr;
        mCanceled = true;
        mCaller.schedule();
    }

    HANDLE mHandle;     //< The handle we want to wait on
    HANDLE mWaitHandle = nullptr; // The handle returned by RegisterWaitForSingleObject
    ULONG  mMillseconds = INFINITE; // The timeout for the wait
    BOOLEAN mTimedout = FALSE; // Whether the wait timed out
    BOOLEAN mCanceled = FALSE; // Whether the wait was canceled
    CoroHandle mCaller;
    std::atomic_flag mFlag = ATOMIC_FLAG_INIT; //< Does the operation complete?
    CancellationToken::Registration mRegistration;

#if defined(_MSC_VER) // https://en.cppreference.com/w/cpp/language/attributes/no_unique_address
    [[msvc::no_unique_address]] // MUST use it on msvc :(
#else
    [[no_unique_address]]
#endif
    T mCancelOperation; //< The cancel operation, default is unregister wait
};

class EventOverlapped : public ::OVERLAPPED {
public:
    EventOverlapped() {
        ::memset(static_cast<OVERLAPPED*>(this), 0, sizeof(OVERLAPPED));
        hEvent = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
    }

    EventOverlapped(const EventOverlapped &) = delete;

    ~EventOverlapped() {
        if (hEvent) {
            ::CloseHandle(hEvent);
        }
    }

    /**
     * @brief Make the awaiter with custom cancel operation
     * 
     * @tparam T 
     * @param cancelOperation 
     * @param timeout The timeout for the wait, default is INFINITE
     * @return auto 
     */
    template <std::invocable T>
    auto makeAwaiter(T cancelOperation, ULONG timeout = INFINITE) const noexcept {
        return WaitObject(hEvent, timeout, cancelOperation);
    }

    auto operator co_await() const noexcept {
        return WaitObject(hEvent);
    }
};

/**
 * @brief Converts a UTF-8 string to a wide string
 * 
 * @param u8 
 * @return std::wstring 
 */
inline auto toWide(std::string_view u8) -> std::wstring {
    int len = ::MultiByteToWideChar(CP_UTF8, 0, u8.data(), u8.size(), nullptr, 0);
    if (len <= 0) {
        return {};
    }
    std::wstring u16(len, L'\0');
    len = ::MultiByteToWideChar(CP_UTF8, 0, u8.data(), u8.size(), u16.data(), u16.size());
    return u16;
}

/**
 * @brief Converts a wide string to a UTF-8 string
 * 
 * @param u16 
 * @return std::string 
 */
inline auto toUtf8(std::wstring_view u16) -> std::string {
    int len = ::WideCharToMultiByte(CP_UTF8, 0, u16.data(), u16.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return {};
    }
    std::string u8(len, '\0');
    len = ::WideCharToMultiByte(CP_UTF8, 0, u16.data(), u16.size(), u8.data(), u8.size(), nullptr, nullptr);
    return u8;
}

} // namespace win32

ILIAS_NS_END