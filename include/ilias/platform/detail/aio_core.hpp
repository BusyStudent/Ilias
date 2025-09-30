/**
 * @file aio_core.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Wrapping the POSIX aio
 * @version 0.1
 * @date 2024-10-30
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/detail/intrusive.hpp>
#include <ilias/io/system_error.hpp>
#include <ilias/task/task.hpp>
#include <ilias/log.hpp>
#include <optional>
#include <cstring>
#include <span>
#include <aio.h>

ILIAS_NS_BEGIN

namespace posix {

/**
 * @brief The common base for aio awaiter
 * 
 */
class AioAwaiterBase : public intrusive::Node<AioAwaiterBase>, 
                       public ::aiocb // The control block
{
public:
    AioAwaiterBase(int fd) : ::aiocb { .aio_fildes = fd } {}
    AioAwaiterBase(AioAwaiterBase &&) = default;

    // Always suspend
    auto await_ready() noexcept -> bool { 
        aio_sigevent.sigev_notify = SIGEV_THREAD; //< Use callback
        aio_sigevent.sigev_value.sival_ptr = this;
        aio_sigevent.sigev_notify_function = onNotifyEntry;
        return false; 
    }

    // Do the suspend
    template <typename T>
    auto suspend(runtime::CoroHandle caller) -> bool {
        mCaller = caller;
        if (!static_cast<T *>(this)->onSubmit()) { 
            // Error on submit, accroding to man, it will set errno
            mResult = Err(SystemError::fromErrno());
            return false;
        }
        mReg.register_<&AioAwaiterBase::cancel>(caller.stopToken(), this);
        return true;
    }

    // Try cancel the operation, note it will unlink self in the list
    auto cancel() -> void {
        unlink();
        auto ret = ::aio_cancel(this->aio_fildes, this);
        ILIAS_TRACE("POSIX::aio", "Cancel op on fd {}, res {}", this->aio_fildes, ret);
    }
protected:
    auto onNotify() -> void {
        // Get the result
        if (auto res = ::aio_return(this); res >= 0) {
            mResult = res;
        }
        else {
            mResult = Err(SystemError(::aio_error(this)));
        }
        ILIAS_TRACE("POSIX::aio", "Operation complete on fd {}, result {}", this->aio_fildes, mResult ? std::to_string(*mResult) : mResult.error().message());
        
        // Check the stop request
        if (mResult == Err(SystemError::Canceled) && mCaller.isStopRequested()) {
            // If the operation was canceled and the caller requested stop, mark the caller as stopped
            mCaller.executor().schedule([this]() { // Back to the executor thread
                mCaller.setStopped();
            });
            return;
        }

        // Check if stop request
        mCaller.schedule();
    }

    static auto onNotifyEntry(::sigval val) -> void {
        static_cast<AioAwaiterBase *>(val.sival_ptr)->onNotify();
    }

    // States
    runtime::CoroHandle mCaller;
    runtime::StopRegistration mReg;

    // Result of the io operation
    IoResult<size_t> mResult {0};
};

/**
 * @brief The CRTP for aio awaiter
 * 
 * @tparam T 
 */
template <typename T>
class AioAwaiter : public AioAwaiterBase {
public:
    using AioAwaiterBase::AioAwaiterBase;

    auto await_suspend(runtime::CoroHandle caller) -> bool {
        return suspend<T>(caller);
    }

    auto await_resume() {
        return static_cast<T *>(this)->onComplete(mResult);
    }

    // Default implementations
    auto onComplete(IoResult<size_t> res) -> IoResult<size_t> {
        return res;
    }
};

/**
 * @brief Wrapping aio_read
 * 
 */
class AioReadAwaiter final : public AioAwaiter<AioReadAwaiter> {
public:
    AioReadAwaiter(int fd, MutableBuffer buffer, std::optional<size_t> offset) : AioAwaiter(fd) {
        aio_offset = offset.value_or(0);
        aio_nbytes = buffer.size_bytes();
        aio_buf = buffer.data();
    }

    auto onSubmit() -> bool {
        ILIAS_TRACE("POSIX::aio", "Submit read {} bytes offset {} on fd {}", aio_nbytes, aio_offset, aio_fildes);
        return ::aio_read(this) == 0;
    }
};

/**
 * @brief Wrapping aio_write
 * 
 */
class AioWriteAwaiter final : public AioAwaiter<AioWriteAwaiter> {
public:
    AioWriteAwaiter(int fd, Buffer buffer, std::optional<size_t> offset) : AioAwaiter(fd) {
        aio_offset = offset.value_or(0);
        aio_nbytes = buffer.size_bytes();
        aio_buf = const_cast<std::byte*>(buffer.data());
    }

    auto onSubmit() -> bool {
        ILIAS_TRACE("Posix::aio", "Submit write {} bytes offset {} on fd {}", aio_nbytes, aio_offset, aio_fildes);
        return ::aio_write(this) == 0;
    }
};

class AioSyncAwaiter final : public AioAwaiter<AioSyncAwaiter> {
public:
    AioSyncAwaiter(int fd, int op) : AioAwaiter(fd), mOp(op) {}

    auto onSubmit() -> bool {
        return ::aio_fsync(mOp, this) == 0;
    }

    auto onComplete(IoResult<size_t> res) -> IoResult<void> {
        if (!res) {
            return Err(res.error());
        }
        return {};
    }
private:
    int mOp;
};


} // namespace posix

ILIAS_NS_END