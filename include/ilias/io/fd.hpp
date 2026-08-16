#pragma once

#include <ilias/io/system_error.hpp>
#include <ilias/io/context.hpp>
#include <ilias/io/error.hpp>
#include <utility> // std::exchange
#include <memory> // std::unique_ptr

#if defined(_WIN32)
    #include <ilias/detail/win32defs.hpp> // CloseHandle
    #define ILIAS_INVALID_FD INVALID_HANDLE_VALUE
    #define ILIAS_CLOSE(x) ::CloseHandle(x)
#else
    #include <unistd.h> // close
    #define ILIAS_INVALID_FD -1
    #define ILIAS_CLOSE(x) (::close(x) == 0)
#endif

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
    auto close() -> void { reset(); }

    /**
     * @brief Close the file descriptor and replace the internal fd to given fd.
     * 
     * @param newFd The new file descriptor. (default to invalid)
     */
    auto reset(fd_t newFd = invalid()) noexcept -> void {
        auto fd = std::exchange(mFd, newFd);
        if (fd == invalid()) {
            return;
        }
        if (!ILIAS_CLOSE(fd)) {
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
        return std::exchange(mFd, invalid());
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

    // Get the fd_t value, impl BorrowFileDescriptor concept
    explicit operator fd_t() const noexcept { return mFd; }

    // Check the fd is valid
    explicit operator bool() const noexcept { return mFd != invalid(); }

    // Os specific Invalid Handle
    static auto invalid() noexcept -> fd_t { return ILIAS_INVALID_FD; }
private:
    fd_t mFd = invalid();
};

// MARK: Win32 Handle
#if defined(_WIN32)
/**
 * @brief RAII wrapper for windows HANDLE. it take the ownership of the handle.
 * 
 */
class Win32Handle {
public:
    explicit Win32Handle(HANDLE handle) : mHandle(normalized(handle)) {}
    Win32Handle(std::nullptr_t) {}
    Win32Handle(Win32Handle &&) = default;
    Win32Handle() = default;

    // Get the handle value
    auto get() const noexcept -> HANDLE { return mHandle.get(); }
    auto reset(HANDLE newHandle = nullptr) -> void { mHandle.reset(normalized(newHandle)); }
    auto release() noexcept -> HANDLE { return mHandle.release(); }

    // Wait the handle to be signaled
    auto wait() const -> IoTask<void> { return win32::waitObject(mHandle.get()); }

    // Operator
    auto operator <=>(const Win32Handle &other) const noexcept = default;
    auto operator =(Win32Handle &&other) noexcept -> Win32Handle & = default;
    auto operator =(std::nullptr_t) noexcept -> Win32Handle & { reset(); return *this; }
    auto operator *() const noexcept -> HANDLE { return get(); }

    // Check if the handle is valid
    explicit operator bool() const noexcept { return bool(mHandle); };
private:
    // Normalized the handle to nullptr if it's invalid
    static auto normalized(HANDLE handle) -> HANDLE { return handle == INVALID_HANDLE_VALUE ? nullptr : handle; }

    struct Deleter {
        void operator()(HANDLE handle) const {
            ::CloseHandle(handle); // normalized, no-need to check
        }
    };
    std::unique_ptr<void, Deleter> mHandle;
};

// impl the co_await handle;
template <>
struct runtime::IntoRawAwaitableTrait<Win32Handle &> {
    static auto into(Win32Handle &h) -> IoTask<void> { return h.wait(); }
};
#endif // _WIN32

ILIAS_NS_END


// Impl formatter
#if !defined(NDEBUG)

#endif