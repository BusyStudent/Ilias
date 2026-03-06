/**
 * @file cstdio.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Wrapper for the standard input/output/error streams.
 * @version 0.1
 * @date 2026-03-01
 * 
 * @copyright Copyright (c) 2026
 * 
 */
#pragma once

#include <ilias/io/fd_utils.hpp>
#include <ilias/io/context.hpp>
#include <ilias/io/method.hpp>
#include <ilias/io/error.hpp>
#include <ilias/io/fd.hpp>

#if defined(_WIN32)
    #define ILIAS_STDIN_FILENO  ::GetStdHandle(STD_INPUT_HANDLE)
    #define ILIAS_STDOUT_FILENO ::GetStdHandle(STD_OUTPUT_HANDLE)
    #define ILIAS_STDERR_FILENO ::GetStdHandle(STD_ERROR_HANDLE)
#else
    #define ILIAS_STDIN_FILENO  STDIN_FILENO
    #define ILIAS_STDOUT_FILENO STDOUT_FILENO
    #define ILIAS_STDERR_FILENO STDERR_FILENO
#endif

ILIAS_NS_BEGIN

// Internal...
enum class StdioKind {
    In,
    Out,
    Err,
};

/**
 * @brief The implmentation of the standard input/output/error streams.
 * 
 * @tparam Kind 
 */
template <StdioKind Kind>
class StdioWrapper final : public StreamExt<StdioWrapper<Kind> > {
public:
    StdioWrapper(StdioWrapper &&) = default;
    StdioWrapper() {
        fd_t fd {};
        switch (Kind) {
            case StdioKind::In:  fd = ILIAS_STDIN_FILENO;  break;
            case StdioKind::Out: fd = ILIAS_STDOUT_FILENO; break;
            case StdioKind::Err: fd = ILIAS_STDERR_FILENO; break;
        }
        if (fd == fd_t(-1)) { // Invalid file descriptor
            return;
        }
        if (auto res = IoHandle<fd_t>::make(fd, IoDescriptor::Tty); res) {
            mHandle = std::move(*res);
        }
    }

    // Readable
    auto read(MutableBuffer buffer) -> IoTask<size_t> requires(Kind == StdioKind::In) {
        if (!mHandle) {
            co_return Err(IoError::BadFileDescriptor);
        }
        co_return co_await mHandle.read(buffer, std::nullopt);
    }

    // Writable
    auto write(Buffer buffer) -> IoTask<size_t> requires(Kind != StdioKind::In) {
        if (!mHandle) {
            co_return Err(IoError::BadFileDescriptor);
        }
        co_return co_await mHandle.write(buffer, std::nullopt);
    }

    auto shutdown() -> IoTask<void> requires(Kind != StdioKind::In) {
        co_return {}; // No-op
    }

    auto flush() -> IoTask<void> requires(Kind != StdioKind::In) {

#if defined(_WIN32)
        // https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers
        if (!::FlushFileBuffers(mHandle.fd())) {
            auto err = ::GetLastError();
            if (err == ERROR_INVALID_HANDLE && ::GetFileType(mHandle.fd()) == FILE_TYPE_CHAR) {
                // We can't flush a 'real' console handle, so we treat it as success
                co_return {};
            }
            co_return Err(SystemError(err));
        }
#endif
        co_return {}; // No-op
    }

    // Operators
    auto operator <=>(const StdioWrapper &) const noexcept = default;
    auto operator =(StdioWrapper &&) -> StdioWrapper & = default;

    explicit operator bool() const noexcept {
        return bool(mHandle);
    }

    static auto make() -> StdioWrapper {
        return {};
    }
private:
    IoHandle<fd_t> mHandle;
};


// Standard streams
using Stdin  = StdioWrapper<StdioKind::In>;
using Stdout = StdioWrapper<StdioKind::Out>;
using Stderr = StdioWrapper<StdioKind::Err>;

ILIAS_NS_END