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

#include <ilias/io/system_error.hpp>
#include <ilias/task/executor.hpp>
#include <ilias/task/task.hpp>
#include <ilias/log.hpp>

ILIAS_NS_BEGIN

namespace win32 {

class WaitObject {
public:
    WaitObject(HANDLE handle) : mHandle(handle) { }

    auto await_ready() const noexcept {
        return false;
    }

    auto await_suspend(TaskView<> caller) -> bool {
        mCaller = caller;
        auto v = ::RegisterWaitForSingleObject(
            &mWaitHandle,
            mHandle,
            &WaitObject::completeCallback,
            this,
            INFINITE,
            WT_EXECUTEONLYONCE
        );
        if (!v) {
            mWaitHandle = nullptr;
            return false;
        }
        return true;
    }

    auto await_resume() -> Result<void> {
        if (!mWaitHandle) { //< FAiled to register
            return Unexpected(SystemError::fromErrno());
        }
        if (!::UnregisterWait(mWaitHandle)) {
            ILIAS_ERROR("Win32", "Failed to unregister wait handle {}", ::GetLastError());
        }
        return {};
    }
private:
    static auto CALLBACK completeCallback(void *_self, BOOLEAN waitOrTimeout) -> void {
        auto self = static_cast<WaitObject*>(_self);
        self->mCaller.executor()->schedule(self->mCaller);
    }

    HANDLE mHandle;     //< The handle we want to wait on
    HANDLE mWaitHandle = nullptr; // The handle returned by RegisterWaitForSingleObject
    TaskView<> mCaller;
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

    auto operator co_await() const noexcept {
        return WaitObject(hEvent);
    }
};

} // namespace win32

ILIAS_NS_END