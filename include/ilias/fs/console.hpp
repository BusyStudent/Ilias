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
#include <ilias/io/fd_utils.hpp>
#include <ilias/io/context.hpp>
#include <ilias/io/method.hpp>
#include <ilias/buffer.hpp>

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

    /**
     * @brief Construct a new empty Console object
     * 
     */
    Console(std::nullptr_t) { }

    /**
     * @brief Construct a new Console object from file descriptor (the class does not take the ownership of the file descriptor)
     * 
     * @param ctxt 
     * @param fd 
     */
    Console(IoContext &ctxt, fd_t fd) : mCtxt(&ctxt), mFd(fd) {
        // Using Unknown because the console maybe redirect to a file or pipe
        mDesc = mCtxt->addDescriptor(fd, IoDescriptor::Tty).value_or(nullptr);
    }

    Console(const Console &) = delete;

    /**
     * @brief Move constructor
     * 
     * @param other 
     */
    Console(Console &&other) : mCtxt(other.mCtxt), mDesc(other.mDesc), mFd(other.mFd) {
        other.mCtxt = nullptr;
        other.mDesc = nullptr;
        other.mFd = {};
    }

    ~Console() {
        close();
    }

    /**
     * @brief Read from the console
     * 
     * @param buffer 
     * @return IoTask<size_t> 
     */
    auto read(std::span<std::byte> buffer) -> IoTask<size_t> {
        return mCtxt->read(mDesc, buffer, std::nullopt);
    }

    /**
     * @brief Write to the console
     * 
     * @param buffer 
     * @return IoTask<size_t> 
     */
    auto write(std::span<const std::byte> buffer) -> IoTask<size_t> {
        return mCtxt->write(mDesc, buffer, std::nullopt);
    }

    /**
     * @brief Print to the console
     * 
     * @param fmt 
     * @param ... 
     * @return IoTask<size_t> 
     */
    auto printf(const char *fmt, ...) -> IoTask<size_t> {
        std::string buffer;

        va_list args;
        va_start(args, fmt);
        vsprintfTo(buffer, fmt, args);
        va_end(args);

        return writeString(std::move(buffer)); //< Move the buffer to the co frame
    }

    /**
     * @brief Put String to the console
     * 
     * @param str 
     * @return IoTask<size_t> 
     */
    auto puts(std::string_view str) -> IoTask<size_t> {
        return writeAll(makeBuffer(str));
    }

    /**
     * @brief Try read a line from the console by delimiter
     * 
     * @param delimiter 
     * @return IoTask<std::string> 
     */
    auto getline(std::string_view delimiter = LineDelimiter) -> IoTask<std::string> {
        std::string buffer(1024, '\0');
        auto n = co_await read(makeBuffer(buffer));
        if (!n) {
            co_return Unexpected(n.error());
        }
        buffer.resize(*n);
        if (buffer.ends_with(delimiter)) {
            buffer.resize(buffer.size() - delimiter.size());
        }
        co_return buffer;
    }

    /**
     * @brief Close the console
     * 
     */
    auto close() -> void {
        if (mDesc) {
            mCtxt->removeDescriptor(mDesc);
            mDesc = nullptr;
            mFd = {};
        }
    }

    /**
     * @brief Cancel all pending operations on the console
     * 
     * @return Result<void> 
     */
    auto cancel() -> Result<void> {
        return mCtxt->cancel(mDesc);
    }


    auto operator <=>(const Console &) const = default;

    auto operator =(const Console &) = delete;

    /**
     * @brief Move assignment operator
     * 
     * @param other 
     * @return Console& 
     */
    auto operator =(Console &&other) -> Console & {
        close();
        mCtxt = other.mCtxt;
        mDesc = other.mDesc;
        mFd = other.mFd;
        other.mCtxt = nullptr;
        other.mDesc = nullptr;
        other.mFd = {};
        return *this;
    }

    /**
     * @brief Check if the console is valid
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const { return mDesc != nullptr; }

    /**
     * @brief Create a console from file descriptor
     * 
     * @param ctxt The IoContext
     * @param fd The file descriptor
     * @return Result<Console> 
     */
    static auto fromFileDescriptor(IoContext &ctxt, fd_t fd) -> Result<Console> {
        auto desc = ctxt.addDescriptor(fd, IoDescriptor::Tty);
        if (!desc) {
            return Unexpected(desc.error());
        }
        Console c;
        c.mCtxt = &ctxt;
        c.mDesc = desc.value();
        c.mFd = fd;
        return c;
    }

    /**
     * @brief Create a console from file descriptor
     * 
     * @param fd The file descriptor
     * @return IoTask<Console> 
     */
    static auto fromFileDescriptor(fd_t fd) -> IoTask<Console> {
        auto &&ctxt = co_await currentIoContext();
        co_return fromFileDescriptor(ctxt, fd);
    }

    /**
     * @brief Get the context object of stdin
     * 
     * @param ctxt 
     * @return Result<Console> 
     */
    static auto fromStdin(IoContext &ctxt) -> Result<Console> {
        return fromFileDescriptor(ctxt, ILIAS_STDIN_FILENO);
    }

    /**
     * @brief Get the context object of stdout
     * 
     * @param ctxt 
     * @return Result<Console> 
     */
    static auto fromStdout(IoContext &ctxt) -> Result<Console> {
        return fromFileDescriptor(ctxt, ILIAS_STDOUT_FILENO);
    }

    /**
     * @brief Get the context object of stderr
     * 
     * @param ctxt 
     * @return Result<Console> 
     */
    static auto fromStderr(IoContext &ctxt) -> Result<Console> {
        return fromFileDescriptor(ctxt, ILIAS_STDERR_FILENO);
    }

    /**
     * @brief Get the context object of stdin
     * 
     * @return IoTask<Console> 
     */
    static auto fromStdin() -> IoTask<Console> {
        return fromFileDescriptor(ILIAS_STDIN_FILENO);
    }

    /**
     * @brief Get the context object of stdout
     * 
     * @return IoTask<Console> 
     */
    static auto fromStdout() -> IoTask<Console> {
        return fromFileDescriptor(ILIAS_STDOUT_FILENO);
    }

    /**
     * @brief Get the context object of stderr
     * 
     * @return IoTask<Console> 
     */
    static auto fromStderr() -> IoTask<Console> {
        return fromFileDescriptor(ILIAS_STDERR_FILENO);
    }
private:
    auto writeString(std::string str) -> IoTask<size_t> {
        co_return co_await writeAll(makeBuffer(str));
    }

    IoContext    *mCtxt = nullptr;
    IoDescriptor *mDesc = nullptr;
    fd_t          mFd {};
};



ILIAS_NS_END