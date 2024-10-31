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

#include <ilias/cancellation_token.hpp>
#include <ilias/io/system_error.hpp>
#include <ilias/task/task.hpp>
#include <ilias/log.hpp>
#include <optional>
#include <cstring>
#include <span>
#include <aio.h>

ILIAS_NS_BEGIN

namespace detail {

template <typename T>
class AioAwaiter : public ::aiocb {
public:
    AioAwaiter(int fd) {
        ::memset(static_cast<::aiocb*>(this), 0, sizeof(::aiocb)); //< Zero the control block
        aio_fildes = fd;
    }

    auto await_ready() const -> bool { return false; }

    auto await_suspend(TaskView<> caller) -> bool {
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
        mReg = caller.cancellationToken().register_([](void *_self) {
            auto self = static_cast<AioAwaiter *>(_self);
            auto ret = ::aio_cancel(self->aio_fildes, self);
            ILIAS_TRACE("POSIX::aio", "Cancel op on fd {}, res {}", self->aio_fildes, ret);
        }, this);
        return true;
    }

    auto await_resume() {
        return static_cast<T*>(this)->onComplete(::aio_return(this));
    }
private:
    TaskView<> mCaller;
    CancellationToken::Registration mReg;
};

/**
 * @brief Wrapping aio_read
 * 
 */
class AioReadAwaiter final : public AioAwaiter<AioReadAwaiter> {
public:
    AioReadAwaiter(int fd, std::span<std::byte> buffer, std::optional<size_t> offset) : AioAwaiter(fd) {
        aio_lio_opcode = LIO_READ;
        aio_offset = offset.value_or(0);
        aio_nbytes = buffer.size_bytes();
        aio_buf = buffer.data();
    }

    auto onSubmit() -> bool {
        ILIAS_TRACE("POSIX::aio", "Submit read {} bytes offset {} on fd {}", aio_nbytes, aio_offset, aio_fildes);
        return ::aio_read(this) == 0;
    }

    auto onComplete(ssize_t bytes) -> Result<size_t> {
        ILIAS_TRACE("POSIX::aio", "Read complete on fd {}, bytes {}", aio_fildes, bytes);
        if (bytes < 0) {
            return Unexpected(SystemError(::aio_error(this)));
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
    AioWriteAwaiter(int fd, std::span<const std::byte> buffer, std::optional<size_t> offset) : AioAwaiter(fd) {
        aio_lio_opcode = LIO_WRITE;
        aio_offset = offset.value_or(0);
        aio_nbytes = buffer.size_bytes();
        aio_buf = (void*) buffer.data();
    }

    auto onSubmit() -> bool {
        ILIAS_TRACE("Posix::aio", "Submit write {} bytes offset {} on fd {}", aio_nbytes, aio_offset, aio_fildes);
        return ::aio_write(this) == 0;
    }

    auto onComplete(ssize_t bytes) -> Result<size_t> {
        ILIAS_TRACE("Posix::aio", "Write complete on fd {}, bytes {}", aio_fildes, bytes);
        if (bytes < 0) {
            return Unexpected(SystemError(::aio_error(this)));
        }
        return bytes;
    }
};


} // namespace detail

ILIAS_NS_END