#pragma once

#include <ilias/io/system_error.hpp>
#include <ilias/io/error.hpp>
#include <ilias/io/context.hpp>
#include <ilias/io/fd_utils.hpp>

ILIAS_NS_BEGIN

/**
 * @brief RAII Wrapper for file descriptors. (fd on linux, HANDLE on windows). it take the ownership of the file descriptor.
 * 
 */
class FileDescriptor {
public:
    explicit FileDescriptor(fd_t fd) : mFd(fd) {}
    FileDescriptor(const FileDescriptor &) = delete;
    FileDescriptor(FileDescriptor &&other) noexcept : mFd(other.release()) {}
    FileDescriptor() = default;
    ~FileDescriptor() { auto _ = close(); }

    /**
     * @brief Close the file descriptor.
     * 
     * @return IoResult<void> 
     */
    auto close() -> IoResult<void> {
        auto fd = release();
        if (fd != fd_t(-1)) {
            return fd_utils::close(fd);
        }
        return {};
    }

    /**
     * @brief Release the ownership of the file descriptor.
     * 
     * @return fd_t 
     */
    auto release() noexcept -> fd_t {
        return std::exchange(mFd, fd_t(-1));
    }

    /**
     * @brief Get the internal file descriptor.
     * 
     * @return fd_t 
     */
    auto get() const noexcept -> fd_t { return mFd; }

    auto operator <=>(const FileDescriptor &other) const noexcept = default;
    auto operator =(FileDescriptor &&other) noexcept -> FileDescriptor & {
        close();
        mFd = other.release();
        return *this;
    }

    explicit operator fd_t() const noexcept { return mFd; }
private:
    fd_t mFd = fd_t(-1);
};

ILIAS_NS_END