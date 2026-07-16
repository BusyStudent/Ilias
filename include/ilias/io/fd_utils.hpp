/**
 * @file fd.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Provides a set of functions for system file descriptors.
 * @version 0.1
 * @date 2024-08-30
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/io/system_error.hpp>
#include <ilias/io/context.hpp> // For IoDescriptor::Type
#include <ilias/io/fd.hpp> // For FileDescriptor
#include <string>

#if defined(_WIN32)
    #include <ilias/detail/win32defs.hpp> // win32 specific types && win32::pipe
#else
    #include <sys/stat.h>
    #include <unistd.h>
    #include <fcntl.h>
#endif // defined(_WIN32)


ILIAS_NS_BEGIN

// Wrapping file descriptor api to cross-platform code.
namespace fd_utils {

/**
 * @brief The pipe pair. User can write on the first fd and read from the second one.
 * 
 */
struct PipePair {
    FileDescriptor writer;
    FileDescriptor reader;
};

/**
 * @brief Creates a pipe pair.
 * 
 * @return IoResult<PipePair> 
 */
inline auto pipe() -> IoResult<PipePair> {

#if defined(_WIN32)
    HANDLE reader, writer;
    if (!win32::pipe(&reader, &writer)) {
        return Err(SystemError::fromErrno());
    }
#else
    int fds[2];
    if (::pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0) {
        return Err(SystemError::fromErrno());
    }
    int reader = fds[0];
    int writer = fds[1];
#endif // defined(_WIN32)

    return PipePair {
        .writer = FileDescriptor{writer},
        .reader = FileDescriptor{reader}
    };
}

/**
 * @brief Duplicates a file descriptor.
 * 
 * @param fd 
 * @return IoResult<fd_t> 
 */
inline auto dup(fd_t fd) -> IoResult<fd_t> {

#if defined(_WIN32)
    HANDLE newFd = nullptr;
    auto v = ::DuplicateHandle(
        ::GetCurrentProcess(),
        fd,
        ::GetCurrentProcess(),
        &newFd,
        0,
        FALSE,
        DUPLICATE_SAME_ACCESS
    );

    if (v) {
        return newFd;
    }
#else
    int newFd = ::dup(fd);
    if (newFd != -1) {
        return newFd;
    }
#endif // defined(_WIN32)

    return Err(SystemError::fromErrno());
}

/**
 * @brief Get the file descriptor type.
 * 
 * @param fd The os file descriptor.
 * @return IoResult<IoDescriptor::Type> 
 */
inline auto type(fd_t fd) -> IoResult<IoDescriptor::Type> {

#if defined(_WIN32)
    ::DWORD type = ::GetFileType(fd);
    switch (type) {
        default: break;
        case FILE_TYPE_CHAR: return IoDescriptor::Tty;
        case FILE_TYPE_DISK: return IoDescriptor::File;
        case FILE_TYPE_PIPE: {
            // Check if it's a pipe or a socket
            ::DWORD flags = 0;
            if (!::GetNamedPipeInfo(fd, &flags, nullptr, nullptr, nullptr)) {
                return IoDescriptor::Socket;
            }
            return IoDescriptor::Pipe;
        }
    }
#else
    struct ::stat st;
    if (::fstat(fd, &st) == 0) {
        if (S_ISCHR(st.st_mode) && ::isatty(fd)) {
            return IoDescriptor::Tty;
        }
        else if (S_ISREG(st.st_mode)) {
            return IoDescriptor::File;
        }
        else if (S_ISFIFO(st.st_mode)) {
            return IoDescriptor::Pipe;
        }
        else if (S_ISSOCK(st.st_mode)) {
            return IoDescriptor::Socket;
        }
        return IoDescriptor::Unknown;
    }
#endif // defined(_WIN32)

    return Err(SystemError::fromErrno());
}

/**
 * @brief Get the file size.
 * 
 * @param fd The os file descriptor to a file.
 * @return IoResult<uint64_t> 
 */
inline auto size(fd_t fd) -> IoResult<uint64_t> {

#if defined(_WIN32)
    ::LARGE_INTEGER size;
    if (::GetFileSizeEx(fd, &size)) {
        return size.QuadPart;
    }
#else
    struct ::stat st;
    if (::fstat(fd, &st) == 0) {
        return st.st_size;
    }
#endif // defined(_WIN32)

    return Err(SystemError::fromErrno());
}

/**
 * @brief Truncate the file with specified size.
 * 
 * @param fd 
 * @param size 
 * @return IoResult<void> 
 */
inline auto truncate(fd_t fd, uint64_t size) -> IoResult<void> {

#if defined(_WIN32)
    ::LARGE_INTEGER currentPos { };
    ::LARGE_INTEGER newSize { .QuadPart = LONGLONG(size) };
    auto ok = ::SetFilePointerEx(fd, newSize, &currentPos, FILE_BEGIN) && ::SetEndOfFile(fd);
    ::SetFilePointerEx(fd, currentPos, nullptr, FILE_BEGIN);
    if (ok) {
        return {};
    }
#else
    if (::ftruncate(fd, size) == 0) {
        return {};
    }
#endif // defined(_WIN32)

    return Err(SystemError::fromErrno());
}

} // namespace fd_utils

ILIAS_NS_END