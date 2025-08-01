/**
 * @file console.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The console interface (stdout, stdin, stderr)
 * @version 0.1
 * @date 2024-09-04
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#pragma once

#include <ilias/io/system_error.hpp>
#include <ilias/io/context.hpp>
#include <ilias/io/method.hpp>
#include <ilias/io/fd.hpp>

#if defined(_WIN32)
    #define ILIAS_STDIN_FILENO  ::GetStdHandle(STD_INPUT_HANDLE)
    #define ILIAS_STDOUT_FILENO ::GetStdHandle(STD_OUTPUT_HANDLE)
    #define ILIAS_STDERR_FILENO ::GetStdHandle(STD_ERROR_HANDLE)
    #define ILIAS_CONSOLE_DELIMITER "\r\n"
#else
    #define ILIAS_STDIN_FILENO  STDIN_FILENO
    #define ILIAS_STDOUT_FILENO STDOUT_FILENO
    #define ILIAS_STDERR_FILENO STDERR_FILENO
    #define ILIAS_CONSOLE_DELIMITER "\n"
#endif


ILIAS_NS_BEGIN

/**
 * @brief The Class for console, async read write (stdin, stdout, stderr)
 * 
 */
class Console final : public StreamMethod<Console> {
public:
    /**
     * @brief The Platform specific console delimiter
     * 
     */
    static constexpr auto LineDelimiter = std::string_view(ILIAS_CONSOLE_DELIMITER);

    Console() = default;
    Console(IoHandle<fd_t> h) : mHandle(std::move(h)) {}

    auto close() { return mHandle.close(); }
    auto cancel() { return mHandle.cancel(); }

    /**
     * @brief Read from the console
     * 
     * @param buffer 
     * @return IoTask<size_t> 
     */
    auto read(MutableBuffer buffer) const -> IoTask<size_t> {
        return mHandle.read(buffer, std::nullopt);
    }

    /**
     * @brief Write to the console
     * 
     * @param buffer 
     * @return IoTask<size_t> 
     */
    auto write(Buffer buffer) const -> IoTask<size_t> {
        return mHandle.write(buffer, std::nullopt);
    }

    /**
     * @brief Shutdown the console (no-op)
     * 
     * @return IoTask<void> 
     */
    auto shutdown() const -> IoTask<void> {
        co_return {};
    }

    /**
     * @brief Flush the console
     * 
     * @return IoTask<void> 
     */
    auto flush() const -> IoTask<void> {
        
#if defined(_WIN32)
        if (mIsConsole) { // https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers
            // We can't flush a 'real' console handle, so we just return
            co_return {};
        }
        if (!::FlushFileBuffers(mHandle.fd())) {
            auto err = ::GetLastError();
            if (err == ERROR_INVALID_HANDLE && ::GetFileType(mHandle.fd()) == FILE_TYPE_CHAR) {
                mIsConsole = true;
                co_return {};
            }
            co_return Err(SystemError(err));
        }
#endif

        co_return {};
    }

    auto operator <=>(const Console &) const = default;

    /**
     * @brief Check if the console is valid
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const { return bool(mHandle); }

    /**
     * @brief Create a console from file descriptor
     * 
     * @param fd The file descriptor
     * @return IoTask<Console> 
     */
    static auto from(fd_t fd) -> IoTask<Console> {
        auto handle = IoHandle<fd_t>::make(fd, IoDescriptor::Tty);
        if (!handle) {
            co_return Err(handle.error());
        }
        co_return Console(std::move(*handle));
    }

    /**
     * @brief Get the context object of stdin
     * 
     * @return IoTask<Console> 
     */
    static auto fromStdin() -> IoTask<Console> {
        return from(ILIAS_STDIN_FILENO);
    }

    /**
     * @brief Get the context object of stdout
     * 
     * @return IoTask<Console> 
     */
    static auto fromStdout() -> IoTask<Console> {
        return from(ILIAS_STDOUT_FILENO);
    }

    /**
     * @brief Get the context object of stderr
     * 
     * @return IoTask<Console> 
     */
    static auto fromStderr() -> IoTask<Console> {
        return from(ILIAS_STDERR_FILENO);
    }
private:
    IoHandle<fd_t> mHandle;

#if defined(_WIN32)
    mutable bool mIsConsole = false; // Doesn the handle is a real console handle in win32?
#endif

};


ILIAS_NS_END