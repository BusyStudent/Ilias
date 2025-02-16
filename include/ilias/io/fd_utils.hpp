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

#include <ilias/detail/expected.hpp>
#include <ilias/io/system_error.hpp>
#include <ilias/io/context.hpp> // For IoDescriptor::Type
#include <string>

#if defined(_WIN32)
    #include <ilias/detail/win32.hpp>
    #include <Windows.h>
    #include <atomic>
#else
    #include <sys/stat.h>
    #include <unistd.h>
    #include <fcntl.h>
#endif // defined(_WIN32)


ILIAS_NS_BEGIN

/**
 * @brief Wrapping file descriptor api to cross-platform code.
 * 
 */
namespace fd_utils {

/**
 * @brief Closes a file descriptor.
 * 
 * @param fd 
 * @return Result<void> 
 */
inline auto close(fd_t fd) -> Result<void> {

#if defined(_WIN32)
    if (::CloseHandle(fd)) {
        return {};
    }
#else
    if (::close(fd) == 0) {
        return {};
    }
#endif // defined(_WIN32)

    return Unexpected(SystemError::fromErrno());
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
 * @return Result<PipePair> 
 */
inline auto pipe() -> Result<PipePair> {

#if defined(_WIN32)
    // MSDN says anymous pipe does not support overlapped I/O, so we have to create a named pipe.
    static std::atomic<int> counter{0};
    wchar_t name[256] {0};
    ::swprintf(
        name, 
        sizeof(name), 
        L"\\\\.\\Pipe\\IliasPipe_%d_%d_%d",
        counter.fetch_add(1),
        int(::time(nullptr)),
        int(::GetCurrentThreadId())
    );

    HANDLE readPipe = ::CreateNamedPipeW(
        name,
        PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_WAIT,
        1, // < Only one instance of the pipe, for our write pipe
        65535, //< 64KB buffer size, as same as linux
        65535,
        NMPWAIT_USE_DEFAULT_WAIT,
        nullptr
    );

    if (readPipe == INVALID_HANDLE_VALUE) {
        return Unexpected(SystemError::fromErrno());
    }

    HANDLE writePipe = ::CreateFileW(
        name,
        GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        nullptr
    );

    if (writePipe == INVALID_HANDLE_VALUE) {
        ::CloseHandle(readPipe);
        return Unexpected(SystemError::fromErrno());
    }
    return PipePair{writePipe, readPipe};
#else
    int fds[2];
    if (::pipe(fds) == 0) {
        return PipePair{fds[1], fds[0]};
    }

    return Unexpected(SystemError::fromErrno());
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
 * @return Result<fd_t> 
 */
inline auto dup(fd_t fd) -> Result<fd_t> {

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

    return Unexpected(SystemError::fromErrno());
}

/**
 * @brief Open a file.
 * 
 * @param path the utf-8 encoded path to the file.
 * @param mode the mode to open the file. (c style mode string like fopen "r", "w", "a" etc.)
 * @return Result<fd_t> 
 */
inline auto open(const char *path, std::string_view mode) -> Result<fd_t> {

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

    if (mode.find('+' != std::string_view::npos)) {
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
    if (mode.find('+')) {
        flags |= O_RDWR;
    }

    int fd = ::open(path, flags);
    if (fd >= 0) {
        return fd;
    }
#endif // defined(_WIN32)

    return Unexpected(SystemError::fromErrno());
}

/**
 * @brief Get the file descriptor type.
 * 
 * @param fd The os file descriptor.
 * @return Result<IoDescriptor::Type> 
 */
inline auto type(fd_t fd) -> Result<IoDescriptor::Type> {

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
    }
#endif // defined(_WIN32)

    return Unexpected(SystemError::fromErrno());
}

/**
 * @brief Get the file size.
 * 
 * @param fd The os file descriptor to a file.
 * @return Result<uint64_t> 
 */
inline auto size(fd_t fd) -> Result<uint64_t> {

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

    return Unexpected(SystemError::fromErrno());
}

}

ILIAS_NS_END