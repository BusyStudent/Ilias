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
#include <span>

ILIAS_NS_BEGIN

namespace detail {

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


} // namespace detail

ILIAS_NS_END