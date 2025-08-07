/**
 * @file pipe.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief pipe class
 * @version 0.1
 * @date 2024-08-30
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/io/system_error.hpp>
#include <ilias/io/fd_utils.hpp>
#include <ilias/io/context.hpp>
#include <ilias/io/method.hpp>
#include <ilias/io/fd.hpp>

ILIAS_NS_BEGIN

/**
 * @brief The pipe class
 * 
 */
class Pipe final : public StreamMethod<Pipe> {
public:
    Pipe() = default;
    Pipe(IoHandle<FileDescriptor> h) : mHandle(std::move(h)) {}

    auto close() { return mHandle.close(); }
    auto cancel() { return mHandle.cancel(); }

    /**
     * @brief Get the file descriptor
     * 
     * @return fd_t 
     */
    auto fd() const noexcept -> fd_t {
        return fd_t(mHandle.fd());
    }

    // Stream
    /**
     * @brief Write data to pipe
     * 
     * @param buffer 
     * @return IoTask<size_t> 
     */
    auto write(Buffer buffer) -> IoTask<size_t> {
        return mHandle.write(buffer, std::nullopt);
    }

    /**
     * @brief Read data from pipe
     * 
     * @param buffer 
     * @return IoTask<size_t> 
     */
    auto read(MutableBuffer buffer) -> IoTask<size_t> {

#if defined(_WIN32) // Windows named pipe spec
        auto val = co_await mHandle.read(buffer, std::nullopt);
        if (val == Err(SystemError(ERROR_BROKEN_PIPE))) {
            co_return 0; // Map closed to EOF(0)
        }
        co_return val;
#else
        return mHandle.read(buffer, std::nullopt);
#endif // defined(_WIN32)
    }

    /**
     * @brief Shutdown the pipe, no-op
     * 
     * @return IoTask<void> 
     */
auto shutdown() -> IoTask<void> {
        co_return {};
    }
    
    /**
     * @brief Flush the pipe, no-op
     * 
     * @return IoTask<void> 
     */
    auto flush() -> IoTask<void> {
        co_return {};
    }

#if defined(_WIN32) // Windows named pipe spec
    /**
     * @brief Wait for the named pipe to be connected (wrapping ConnectNamedPipe)
     * 
     * @return IoTask<void> 
     */
    auto connect() -> IoTask<void> {
        return mHandle.connectNamedPipe();
    }

    /**
     * 
     * @brief Disconnect the named pipe (wrapping DisconnectNamedPipe)
     * 
     * @return IoTask<void> 
     */
    auto disconnect() -> IoTask<void> {
        if (::DisconnectNamedPipe(fd())) {
            co_return {};
        }
        co_return Err(SystemError::fromErrno());
    }
#endif // defined(_WIN32)


    /**
     * @brief Check if the pipe is valid
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const {
        return bool(mHandle);
    }

    /**
     * @brief Create the pipe and return the pair of pipes
     * 
     * @return IoTask<std::pair<Pipe, Pipe> > 
     */
    static auto pair() -> IoTask<std::pair<Pipe, Pipe> > {
        auto pair = fd_utils::pipe();
        if (!pair) {
            co_return Err(pair.error());
        }
        FileDescriptor read(pair->read);
        FileDescriptor write(pair->write);

        auto read2 = IoHandle<FileDescriptor>::make(std::move(read), IoDescriptor::Pipe);
        if (!read2) {
            co_return Err(read2.error());
        }
        auto write2 = IoHandle<FileDescriptor>::make(std::move(write), IoDescriptor::Pipe);
        if (!write2) {
            co_return Err(write2.error());
        }
        co_return std::make_pair(Pipe(std::move(*read2)), Pipe(std::move(*write2)));
    }
private:
    IoHandle<FileDescriptor> mHandle;
};

ILIAS_NS_END