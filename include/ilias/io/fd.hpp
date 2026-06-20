#pragma once

#include <ilias/io/system_error.hpp>
#include <ilias/io/fd_utils.hpp>
#include <ilias/io/context.hpp>
#include <ilias/io/error.hpp>
#include <memory>

ILIAS_NS_BEGIN

// MARK: File Descriptor
/**
 * @brief RAII Wrapper for file descriptors. (fd on linux, HANDLE on windows). it take the ownership of the file descriptor.
 * 
 */
class FileDescriptor {
public:
    explicit FileDescriptor(fd_t fd) : mFd(fd) {}
    FileDescriptor(FileDescriptor &&other) noexcept : mFd(other.release()) {}
    FileDescriptor() = default;
    ~FileDescriptor() { close(); }

    /**
     * @brief Close the file descriptor.
     * 
     */
    auto close() -> void {
        auto fd = release();
        if (fd == Invalid) {
            return;
        }
#if defined(_WIN32)
        auto ok = ::CloseHandle(fd);
#else
        auto ok = (::close(fd) == 0);
#endif
        if (!ok) {
            ILIAS_WARN("Io", "Failed to close file descriptor: {}, {}", fd, SystemError::fromErrno());
        }
    }

    /**
     * @brief Release the ownership of the file descriptor.
     * 
     * @return fd_t 
     */
    [[nodiscard]]
    auto release() noexcept -> fd_t {
        return std::exchange(mFd, Invalid);
    }

    /**
     * @brief Get the internal file descriptor.
     * 
     * @return fd_t 
     */
    auto get() const noexcept -> fd_t { return mFd; }

    // Swap
    auto swap(FileDescriptor &other) noexcept -> void { std::swap(mFd, other.mFd); }

    // Operator
    auto operator <=>(const FileDescriptor &other) const noexcept = default;
    auto operator =(FileDescriptor &&other) noexcept -> FileDescriptor & { swap(other); return *this; }

    // Get the fd_t value, impl IntoFileDescriptor concept
    explicit operator fd_t() const noexcept { return mFd; }

    // Check the fd is valid
    explicit operator bool() const noexcept { return mFd != Invalid; }

    // Os specific Invalid Handle
#if defined(_WIN32)
    static const inline HANDLE Invalid = INVALID_HANDLE_VALUE;
#else
    static constexpr int Invalid = -1;
#endif // _WIN32
private:
    fd_t mFd = Invalid;
};

// MARK: Win32 Handle
#if defined(_WIN32)
/**
 * @brief RAII wrapper for windows HANDLE. it take the ownership of the handle.
 * 
 */
class Win32Handle {
public:
    explicit Win32Handle(HANDLE handle) : mHandle(handle) {}
    Win32Handle(Win32Handle &&) = default;
    Win32Handle() = default;

    // Get the handle value
    auto get() const noexcept -> HANDLE { return mHandle.get(); }
    auto reset(HANDLE newHandle = nullptr) -> void { mHandle.reset(newHandle); }
    auto release() noexcept -> HANDLE { return mHandle.release(); }

    // Wait the handle to be signaled
    auto wait() const -> IoTask<void> { return win32::waitObject(mHandle.get()); }

    // Operator
    auto operator <=>(const Win32Handle &other) const noexcept = default;
    auto operator =(Win32Handle &&other) noexcept -> Win32Handle & = default;

    // Check if the handle is valid
    explicit operator bool() const noexcept { return mHandle && mHandle.get() != INVALID_HANDLE_VALUE; };
private:
    struct Deleter {
        void operator()(HANDLE handle) const {
            if (handle != INVALID_HANDLE_VALUE) {
                ::CloseHandle(handle);
            }
        }
    };
    std::unique_ptr<void, Deleter> mHandle;
};

// ADL, impl the co_await handle;
inline auto toAwaitable(Win32Handle &h) -> IoTask<void> { return h.wait(); }
#endif // _WIN32

ILIAS_NS_END