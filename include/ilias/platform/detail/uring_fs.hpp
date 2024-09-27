/**
 * @file uring_fs.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Wrapping read write and other based on fs
 * @version 0.1
 * @date 2024-09-26
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/platform/detail/uring_core.hpp>
#include <ilias/io/system_error.hpp>
#include <optional>
#include <span>

ILIAS_NS_BEGIN

namespace detail {

class UringWriteAwaiter final : public UringAwaiter<UringWriteAwaiter> {
public:
    UringWriteAwaiter(::io_uring &ring, int fd, std::span<const std::byte> buffer, std::optional<size_t> offset) :
        UringAwaiter(ring), mFd(fd), mBuffer(buffer), mOffset(offset)
    {

    }

    auto onSubmit() {
        ILIAS_TRACE("Uring", "Prep write for fd {}, {} bytes", mFd, mBuffer.size());
        __u64 offset = mOffset ? mOffset.value() : __u64(-1);
        ::io_uring_prep_write(sqe(), mFd, mBuffer.data(), mBuffer.size(), offset);
    }

    auto onComplete(int64_t ret) -> Result<size_t> {
        if (ret < 0) {
            return Unexpected(SystemError(ret));
        }
        return size_t(ret);
    }
private:
    int mFd;
    std::span<const std::byte> mBuffer;
    std::optional<size_t> mOffset;    
};

class UringReadAwaiter final : public UringAwaiter<UringReadAwaiter> {
public:
    UringReadAwaiter(::io_uring &ring, int fd, std::span<std::byte> buffer, std::optional<size_t> offset) :
        UringAwaiter(ring), mFd(fd), mBuffer(buffer), mOffset(offset)
    {

    }

    auto onSubmit() {
        ILIAS_TRACE("Uring", "Prep read for fd {}, {} bytes", mFd, mBuffer.size());
        __u64 offset = mOffset ? mOffset.value() : __u64(-1);
        ::io_uring_prep_read(sqe(), mFd, mBuffer.data(), mBuffer.size(), offset);
    }

    auto onComplete(int64_t ret) -> Result<size_t> {
        if (ret < 0) {
            return Unexpected(SystemError(ret));
        }
        return size_t(ret);
    }
private:
    int mFd;
    std::span<std::byte> mBuffer;
    std::optional<size_t> mOffset;    
};

} // namespace detail

ILIAS_NS_END