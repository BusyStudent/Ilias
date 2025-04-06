/**
 * @file iocp_fs.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief impl of file system operations using IOCP
 * @version 0.1
 * @date 2024-08-18
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/platform/detail/iocp_overlapped.hpp>
#include <ilias/io/system_error.hpp>
#include <ilias/log.hpp>
#include <optional>
#include <atomic>
#include <latch>
#include <span>

ILIAS_NS_BEGIN

namespace detail {

/**
 * @brief Wrapping the iocp async read operations
 * 
 */
class IocpReadAwaiter final : public IocpAwaiter<IocpReadAwaiter> {
public:
    IocpReadAwaiter(HANDLE handle, std::span<std::byte> buffer, std::optional<size_t> offset) :
        IocpAwaiter(handle), mBuffer(buffer) 
    {
        if (offset) {
            overlapped()->setOffset(offset.value());
        }
    }

    auto onSubmit() -> bool {
        ILIAS_TRACE("IOCP", "ReadFile {} bytes on handle {}", mBuffer.size(), handle());
        return ::ReadFile(handle(), mBuffer.data(), mBuffer.size(), &bytesTransferred(), overlapped());
    }

    auto onComplete(DWORD error, DWORD bytesTransferred) -> Result<size_t> {
        ILIAS_TRACE("IOCP", "ReadFile {} bytes on handle {} completed, Error {}", bytesTransferred, handle(), error);
        if (error != ERROR_SUCCESS) {
            return Unexpected(SystemError(error));
        }
        return bytesTransferred;
    }
private:
    std::span<std::byte> mBuffer;
};

/**
 * @brief Wrapping the iocp async write operations
 * 
 */
class IocpWriteAwaiter final : public IocpAwaiter<IocpWriteAwaiter> {
public:
    IocpWriteAwaiter(HANDLE handle, std::span<const std::byte> buffer, std::optional<size_t> offset) :
        IocpAwaiter(handle), mBuffer(buffer)
    {
        if (offset) {
            overlapped()->setOffset(offset.value());
        }
    }

    auto onSubmit() -> bool {
        ILIAS_TRACE("IOCP", "WriteFile {} bytes on handle {}", mBuffer.size(), handle());
        return ::WriteFile(handle(), mBuffer.data(), mBuffer.size(), &bytesTransferred(), overlapped());
    }

    auto onComplete(DWORD error, DWORD bytesTransferred) -> Result<size_t> {
        ILIAS_TRACE("IOCP", "WriteFile {} bytes on handle {} completed, Error {}", bytesTransferred, handle(), error);
        if (error != ERROR_SUCCESS) {
            return Unexpected(SystemError(error));
        }
        return bytesTransferred;
    }
private:
    std::span<const std::byte> mBuffer;
};

/**
 * @brief Wrapping the ConnectNamedPipe async operations
 * 
 */
class IocpConnectPipeAwaiter final : public IocpAwaiter<IocpConnectPipeAwaiter> {
public:
    IocpConnectPipeAwaiter(HANDLE handle) : IocpAwaiter(handle) { }

    auto onSubmit() -> bool {
        ILIAS_TRACE("IOCP", "ConnectNamedPipe on handle {}", handle());
        return ::ConnectNamedPipe(handle(), overlapped());
    }

    auto onComplete(DWORD error, DWORD bytesTransferred) -> Result<void> {
        ILIAS_TRACE("IOCP", "ConnectNamedPipe on handle {} completed, Error {}", handle(), error);
        if (error != ERROR_SUCCESS) {
            return Unexpected(SystemError(error));
        }
        return {};
    }
};

class IocpDeviceIoControlAwaiter final : public IocpAwaiter<IocpDeviceIoControlAwaiter> {
public:
    IocpDeviceIoControlAwaiter(HANDLE handle, DWORD controlCode, std::span<std::byte> inBuffer, std::span<std::byte> outBuffer) :
        IocpAwaiter(handle), mControlCode(controlCode), mInBuffer(inBuffer), mOutBuffer(outBuffer) 
    {
        
    }

    auto onSubmit() -> bool {
        return ::DeviceIoControl(handle(), mControlCode, mInBuffer.data(), mInBuffer.size(), mOutBuffer.data(), mOutBuffer.size(), &bytesTransferred(), overlapped());
    }

    auto onComplete(DWORD error, DWORD bytesTransferred) -> Result<size_t> {
        if (error != ERROR_SUCCESS) {
            return Unexpected(SystemError(error));
        }
        return bytesTransferred;
    }
private:
    DWORD mControlCode;
    std::span<std::byte> mInBuffer;
    std::span<std::byte> mOutBuffer;
};


/**
 * @brief Wrapping the Thread async operations, based on thread pool
 * 
 * @tparam T 
 */
template <typename T>
class IocpThreadAwaiter {
public:
    IocpThreadAwaiter() = default;

    IocpThreadAwaiter(const IocpThreadAwaiter&) = delete;

    ~IocpThreadAwaiter() {
        if (mThreadHandle != INVALID_HANDLE_VALUE) [[likely]] {
            ::CloseHandle(mThreadHandle);
        }
    }

    auto await_ready() const noexcept -> bool { return false; }

    auto await_suspend(TaskView<> task) -> bool {
        mTask = task;
        // Start the thread
        if (!::QueueUserWorkItem(onThreadCallback, this, WT_EXECUTELONGFUNCTION)) {
            ILIAS_ERROR("IOCP", "QueueUserWorkItem failed, Error {}", GetLastError());
            mThreadError = ::GetLastError();
            return false;
        }
        mRegistration = task.cancellationToken().register_(onCancelCallback, this); // Register the cancelation
        return true;
    }

    auto await_resume() {
        using ReturnType = decltype(static_cast<T*>(this)->onComplete());
        if (mThreadError) {
            return ReturnType(Unexpected(SystemError(*mThreadError)));
        }
        return static_cast<T*>(this)->onComplete();
    }
private:
    auto onThread() -> void {
        auto v = ::DuplicateHandle(
            ::GetCurrentProcess(),
            ::GetCurrentThread(),
            ::GetCurrentProcess(),
            &mThreadHandle,
            0,
            FALSE,
            DUPLICATE_SAME_ACCESS
        );
        if (!v) [[unlikely]] {
            ILIAS_ERROR("IOCP", "DuplicateHandle failed, Error {}", ::GetLastError());
            mThreadHandle = INVALID_HANDLE_VALUE;
            mThreadError = ::GetLastError();
            mTask.schedule(); //< Directly schedule the task
            return;
        }
        if (!mFlags.test_and_set()) {
            // If flags is not set, meaning we first reach here
            ILIAS_TRACE("IOCP", "Thread I/O {} start", (void*) this);
            static_cast<T*>(this)->onSubmit();
        }
        else {
            ILIAS_TRACE("IOCP", "Thread I/O {} marked cancelled, skip the I/O operation", (void*) this);
            mThreadError = ERROR_OPERATION_ABORTED;
        }
        mTask.executor()->post(&onScheduleCallback, this);
        mCallbackLatch.wait(); //< Wait for the callback to be called, avoid the thread to run another I/O operation
        ILIAS_TRACE("IOCP", "Thread I/O {} done", (void*) this);
    }

    auto onCancel() -> void {
        if (!mFlags.test_and_set()) {
            // False means the operation is not started yet, just mark it as cancelled
            ILIAS_TRACE("IOCP", "Thread I/O {} not started, mark as cancelled", (void*) this);
            return;
        }
        ILIAS_TRACE("IOCP", "Thread I/O {} started, call CancelSynchronousIo", (void*) this);
        if (!::CancelSynchronousIo(mThreadHandle)) {
            ILIAS_ERROR("IOCP", "CancelSynchronousIo failed, Error {}", ::GetLastError());
        }
    }

    static auto CALLBACK onThreadCallback(LPVOID _self) -> DWORD {
        auto self = static_cast<T*>(_self);
        self->onThread();
        return 0;
    }

    static auto onCancelCallback(void *_self) -> void {
        auto self = static_cast<T*>(_self);
        self->onCancel();
    }

    static auto onScheduleCallback(void *_self) -> void {
        auto self = static_cast<T*>(_self);
        self->mCallbackLatch.count_down();
        self->mTask.schedule();
    }

    std::atomic_flag mFlags = ATOMIC_FLAG_INIT;
    std::latch mCallbackLatch {1};       //< The Wait for the callback invoke on the main thread
    TaskView<> mTask;                    //< The task suspended on
    std::optional<DWORD> mThreadError;   //< Did we get an system error from thread api
    HANDLE mThreadHandle = INVALID_HANDLE_VALUE;   //< The thread handle
    CancellationToken::Registration mRegistration; //< The registration for the cancelation
};

/**
 * @brief Thread I/O version for ReadFile
 * 
 */
class IocpThreadReadAwaiter final : public IocpThreadAwaiter<IocpThreadReadAwaiter> {
public:
    IocpThreadReadAwaiter(HANDLE file, std::span<std::byte> buffer) : mFile(file), mBuffer(buffer) {}

    auto onComplete() -> Result<size_t> {
        return mResult;
    }

    auto onSubmit() -> void {
        DWORD bytesTransferred = 0;
        if (!::ReadFile(mFile, mBuffer.data(), mBuffer.size(), &bytesTransferred, nullptr)) {
            mResult = Unexpected(SystemError::fromErrno());
            return;
        }
        mResult = bytesTransferred;
    }
private:
    HANDLE mFile;
    Result<size_t> mResult;
    std::span<std::byte> mBuffer;
};

/**
 * @brief Thread I/O version for WriteFile
 * 
 */
class IocpThreadWriteAwaiter final : public IocpThreadAwaiter<IocpThreadWriteAwaiter> {
public:
    IocpThreadWriteAwaiter(HANDLE file, std::span<const std::byte> buffer) : mFile(file), mBuffer(buffer) {}

    auto onComplete() -> Result<size_t> {
        return mResult;
    }

    auto onSubmit() -> void {
        DWORD bytesTransferred = 0;
        if (!::WriteFile(mFile, mBuffer.data(), mBuffer.size(), &bytesTransferred, nullptr)) {
            mResult = Unexpected(SystemError::fromErrno());
            return;
        }
        mResult = bytesTransferred;
    }
private:
    HANDLE mFile;
    Result<size_t> mResult;
    std::span<const std::byte> mBuffer;
};


} // namespace detail

ILIAS_NS_END