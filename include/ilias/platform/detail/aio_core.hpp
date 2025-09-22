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

#include <ilias/io/system_error.hpp>
#include <ilias/task/task.hpp>
#include <ilias/log.hpp>
#include <optional>
#include <cstring>
#include <span>
#include <aio.h>

ILIAS_NS_BEGIN

namespace posix {

template <typename T>
class AioAwaiter : public ::aiocb {
public:
    AioAwaiter(int fd) {
        ::memset(static_cast<::aiocb*>(this), 0, sizeof(::aiocb)); //< Zero the control block
        aio_fildes = fd;
    }

    auto await_ready() const -> bool { return false; }

    auto await_suspend(runtime::CoroHandle caller) -> bool {
        aio_sigevent.sigev_notify = SIGEV_THREAD; //< Use callback
        aio_sigevent.sigev_value.sival_ptr = this;
        aio_sigevent.sigev_notify_function = [](::sigval val) {
            auto self = static_cast<AioAwaiter *>(val.sival_ptr);
            ILIAS_TRACE("POSIX::aio", "Operation complete on fd {}", self->aio_fildes);
            self->mCaller.schedule();
        };
        mCaller = caller;
        if (!static_cast<T*>(this)->onSubmit()) {
            return false;
        }
        mReg.register_<&AioAwaiter::onStopRequested>(caller.stopToken(), this);
        return true;
    }

    auto await_resume() {
        return static_cast<T*>(this)->onComplete(::aio_return(this));
    }
private:
    auto onStopRequested() -> void {
        auto ret = ::aio_cancel(this->aio_fildes, this);
        ILIAS_TRACE("POSIX::aio", "Cancel op on fd {}, res {}", this->aio_fildes, ret);
    }

    runtime::CoroHandle mCaller;
    runtime::StopRegistration mReg;
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

    auto onComplete(ssize_t bytes) -> IoResult<size_t> {
        ILIAS_TRACE("POSIX::aio", "Read complete on fd {}, bytes {}", aio_fildes, bytes);
        if (bytes < 0) {
            return Err(SystemError(::aio_error(this)));
        }
        return bytes;
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
        aio_buf = (void*) buffer.data();
    }

    auto onSubmit() -> bool {
        ILIAS_TRACE("Posix::aio", "Submit write {} bytes offset {} on fd {}", aio_nbytes, aio_offset, aio_fildes);
        return ::aio_write(this) == 0;
    }

    auto onComplete(ssize_t bytes) -> IoResult<size_t> {
        ILIAS_TRACE("Posix::aio", "Write complete on fd {}, bytes {}", aio_fildes, bytes);
        if (bytes < 0) {
            return Err(SystemError(::aio_error(this)));
        }
        return bytes;
    }
};

class AioSyncAwaiter final : public AioAwaiter<AioSyncAwaiter> {
public:
    AioSyncAwaiter(int fd, int op) : AioAwaiter(fd), mOp(op) {}

    auto onSubmit() -> bool {
        return ::aio_fsync(mOp, this) == 0;
    }

    auto onComplete(ssize_t ret) -> IoResult<void> {
        if (ret < 0) {
            return Err(SystemError(::aio_error(this)));
        }
        return {};
    }
private:
    int mOp;
};


} // namespace posix

ILIAS_NS_END