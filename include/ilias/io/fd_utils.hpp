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
#include <ilias/detail/expected.hpp>

#if defined(_WIN32)
    #include <ilias/detail/win32.hpp>
    #include <Windows.h>
    #include <atomic>
#else
    #include <unistd.h>
    #include <fcntl.h>
#endif


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
#endif

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
        4096,
        4096,
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
#endif

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
#endif

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
#endif

    return Unexpected(SystemError::fromErrno());
}


}

ILIAS_NS_END