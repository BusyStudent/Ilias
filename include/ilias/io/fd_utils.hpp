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
#include <string>

#if defined(_WIN32)
    #include <ilias/detail/win32defs.hpp> // win32 specific types && win32::pipe
    #include <atomic>
#else
    #include <sys/stat.h>
    #include <unistd.h>
    #include <fcntl.h>
#endif // defined(_WIN32)


ILIAS_NS_BEGIN

// Wrapping file descriptor api to cross-platform code.
namespace fd_utils {

/**
 * @brief Closes a file descriptor.
 * 
 * @param fd 
 * @return IoResult<void> 
 */
inline auto close(fd_t fd) -> IoResult<void> {

#if defined(_WIN32)
    if (::CloseHandle(fd)) {
        return {};
    }
#else
    if (::close(fd) == 0) {
        return {};
    }
#endif // defined(_WIN32)

    return Err(SystemError::fromErrno());
}

/**
 * @brief The pipe pair. User can write on the first fd and read from the second one.
 * 
 */
struct PipePair {
    fd_t write;
    fd_t read;
};

/**
 * @brief Creates a pipe pair.
 * 
 * @return IoResult<PipePair> 
 */
inline auto pipe() -> IoResult<PipePair> {

#if defined(_WIN32)
    HANDLE read, write;
    if (!win32::pipe(&read, &write)) {
        return Err(SystemError::fromErrno());
    }
    return PipePair{write, read};
#else
    int fds[2];
    if (::pipe(fds) == 0) {
        return PipePair{fds[1], fds[0]};
    }

    return Err(SystemError::fromErrno());
#endif // defined(_WIN32)

}

/**
 * @brief Checks if the file descriptor is a terminal.
 * 
 * @param fd 
 * @return true 
 * @return false 
 */
inline auto isatty(fd_t fd) -> bool {

#if defined(_WIN32)
    return ::GetFileType(fd) == FILE_TYPE_CHAR;
#else
    return ::isatty(fd) != 0;
#endif // defined(_WIN32)

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
 * @brief Open a file.
 * 
 * @param path the utf-8 encoded path to the file.
 * @param mode the mode to open the file. (c style mode string like fopen "r", "w", "a" etc.)
 * @return IoResult<fd_t> 
 */
inline auto open(const char *path, std::string_view mode) -> IoResult<fd_t> {

#if defined(_WIN32)
    ::DWORD access = 0;
    ::DWORD shareMode = 0;
    ::DWORD creationDisposition = 0;
    ::DWORD flagsAndAttributes = FILE_FLAG_OVERLAPPED;
    bool append = false;

    if (mode.find('r') != std::string_view::npos) {
        creationDisposition = OPEN_EXISTING;
        access |= GENERIC_READ;
    }

    if (mode.find('w') != std::string_view::npos) {
        creationDisposition = CREATE_ALWAYS;
        access |= GENERIC_WRITE;
    }

    if (mode.find('a') != std::string_view::npos) {
        creationDisposition = OPEN_ALWAYS;
        access |= GENERIC_WRITE;
        append = true;
    }

    if (mode.find('+') != std::string_view::npos) {
        access |= GENERIC_READ | GENERIC_WRITE;
    }

    HANDLE fd = ::CreateFileW(
        win32::toWide(path).c_str(),
        access,
        shareMode,
        nullptr,
        creationDisposition,
        flagsAndAttributes,
        nullptr
    );
    if (fd != INVALID_HANDLE_VALUE) {
        if (append) {
            ::SetFilePointer(fd, 0, nullptr, FILE_END);
        }
        return fd;
    }
#else
    // Mapping by man fopen
    // r  | O_RDONLY
    // w  | O_WRONLY | O_CREAT | O_TRUNC 
    // a  | O_WRONLY | O_CREAT | O_APPEND
    // r+ | O_RDWR                       
    // w+ | O_RDWR | O_CREAT | O_TRUNC   
    // a+ | O_RDWR | O_CREAT | O_APPEND  
    int flags = 0;

    if (mode.find('r') != std::string_view::npos) {
        flags |= O_RDONLY;
    }
    if (mode.find('w') != std::string_view::npos) {
        flags |= O_WRONLY | O_CREAT | O_TRUNC; 
    }
    if (mode.find('a') != std::string_view::npos) {
        flags |= O_WRONLY | O_CREAT | O_APPEND;
    }
    if (mode.find('+') != std::string_view::npos) {
        flags |= O_RDWR;
    }

    int fd = -1;

    if (flags & O_CREAT) {
        fd = ::open(path, flags, mode_t(S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH));
    }
    else {
        fd = ::open(path, flags);
    }
    if (fd >= 0) {
        return fd;
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