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
#else
    #define ILIAS_STDIN_FILENO  STDIN_FILENO
    #define ILIAS_STDOUT_FILENO STDOUT_FILENO
    #define ILIAS_STDERR_FILENO STDERR_FILENO
#endif



ILIAS_NS_BEGIN

/**
 * @brief The Class for console, async read write (stdin, stdout, stderr)
 * 
 */
class Console final : public StreamMethod<Console> {
public:
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
     * @return Task<size_t> 
     */
    auto read(std::span<std::byte> buffer) -> Task<size_t> {
        return mCtxt->read(mDesc, buffer, std::nullopt);
    }

    /**
     * @brief Write to the console
     * 
     * @param buffer 
     * @return Task<size_t> 
     */
    auto write(std::span<const std::byte> buffer) -> Task<size_t> {
        return mCtxt->write(mDesc, buffer, std::nullopt);
    }

    /**
     * @brief Print to the console
     * 
     * @param fmt 
     * @param ... 
     * @return Task<size_t> 
     */
    auto printf(const char *fmt, ...) -> Task<size_t> {
        std::string buffer;
        MemWriter writer(buffer);

        va_list args;
        va_start(args, fmt);
        writer.printf(fmt, args);
        va_end(args);

        return writeString(std::move(buffer)); //< Move the buffer to the co frame
    }

    /**
     * @brief Put String to the console
     * 
     * @param str 
     * @return Task<size_t> 
     */
    auto puts(std::string_view str) -> Task<size_t> {
        return writeAll(makeBuffer(str));
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
private:
    auto writeString(std::string str) -> Task<size_t> {
        co_return co_await writeAll(makeBuffer(str));
    }

    IoContext    *mCtxt = nullptr;
    IoDescriptor *mDesc = nullptr;
    fd_t          mFd {};
};



ILIAS_NS_END