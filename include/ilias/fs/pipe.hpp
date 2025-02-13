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

ILIAS_NS_BEGIN

/**
 * @brief The pipe class
 * 
 */
class Pipe final : public StreamMethod<Pipe> {
public:
    Pipe() = default;

    /**
     * @brief Construct a new Pipe object from file descriptor (take the ownership)
     * 
     * @param ctxt 
     * @param fd 
     */
    Pipe(IoContext &ctxt, fd_t fd) : mCtxt(&ctxt), mFd(fd) {
        mDesc = mCtxt->addDescriptor(fd, IoDescriptor::Pipe).value_or(nullptr);
        if (!mDesc) {
            fd_utils::close(fd);
            mCtxt = nullptr;
            mFd = {};
        }
    }

    Pipe(const Pipe &) = delete;

    /**
     * @brief Construct a new Pipe object by move
     * 
     * @param other 
     */
    Pipe(Pipe &&other) : mCtxt(other.mCtxt), mDesc(other.mDesc), mFd(other.mFd) {
        other.mCtxt = nullptr;
        other.mDesc = nullptr;
        other.mFd = {};
    }

    ~Pipe() {
        close();
    }

    /**
     * @brief Check if the pipe is valid
     * 
     * @return true 
     * @return false 
     */
    auto isValid() const -> bool {
        return mDesc != nullptr;
    }

    /**
     * @brief Get the file descriptor
     * 
     * @return fd_t 
     */
    auto fd() const -> fd_t {
        return mFd;
    }

    /**
     * @brief Get the io context
     * 
     * @return IoContext* 
     */
    auto context() const -> IoContext * {
        return mCtxt;
    }

    /**
     * @brief Close the pipe
     * 
     */
    auto close() -> void {
        if (mDesc) {
            mCtxt->removeDescriptor(mDesc);
            mDesc = nullptr;
            mCtxt = nullptr;
            fd_utils::close(mFd);
            mFd = {};
        }
    }

    /**
     * @brief Clone the pipe, by using dup
     * 
     * @return Result<Pipe> 
     */
    auto clone() -> Result<Pipe> {
        auto newFd = fd_utils::dup(mFd);
        if (!newFd) {
            return Unexpected(newFd.error());
        }
        return Pipe(*mCtxt, newFd.value());
    }

    // Stream
    /**
     * @brief Write data to pipe
     * 
     * @param buffer 
     * @return IoTask<size_t> 
     */
    auto write(std::span<const std::byte> buffer) -> IoTask<size_t> {
        return mCtxt->write(mDesc, buffer, std::nullopt);
    }

    /**
     * @brief Read data from pipe
     * 
     * @param buffer 
     * @return IoTask<size_t> 
     */
    auto read(std::span<std::byte> buffer) -> IoTask<size_t> {
        return mCtxt->read(mDesc, buffer, std::nullopt);
    }

    /**
     * @brief Shutdown the pipe, only do the close
     * 
     * @return IoTask<void> 
     */
    auto shutdown() -> IoTask<void> {
        close();
        co_return {};
    }

#if defined(_WIN32) // Windows named pipe spec
    /**
     * @brief Wait for the named pipe to be connected (wrapping ConnectNamedPipe)
     * 
     * @return IoTask<void> 
     */
    auto connect() -> IoTask<void> {
        return mCtxt->connectNamedPipe(mDesc);
    }

    /**
     * 
     * @brief Disconnect the named pipe (wrapping DisconnectNamedPipe)
     * 
     * @return Result<void> 
     */
    auto disconnect() -> Result<void> {
        if (::DisconnectNamedPipe(mFd)) {
            return {};
        }
        return Unexpected(SystemError::fromErrno());
    }
#endif // defined(_WIN32)


    // Move only
    auto operator =(const Pipe &) = delete;

    auto operator =(Pipe &&other) -> Pipe & {
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
     * @brief Check if the pipe is valid
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const {
        return mDesc != nullptr;
    }

    /**
     * @brief Create the pipe and return the pair of pipes
     * 
     * @param ctxt 
     * @return Result<std::pair<Pipe, Pipe> > (write -> read)
     */
    static auto pair(IoContext &ctxt) -> Result<std::pair<Pipe, Pipe> > {
        auto pipes = fd_utils::pipe();
        if (!pipes) {
            return Unexpected(pipes.error());
        }
        Pipe write(ctxt, pipes->write);
        Pipe read(ctxt, pipes->read);
        return std::make_pair(std::move(write), std::move(read));
    }

    /**
     * @brief Create the pipe and return the pair of pipes
     * 
     * @return IoTask<std::pair<Pipe, Pipe> > 
     */
    static auto pair() -> IoTask<std::pair<Pipe, Pipe> > {
        co_return pair(co_await currentIoContext());
    }
private:
    IoContext    *mCtxt = nullptr;
    IoDescriptor *mDesc = nullptr;
    fd_t          mFd {};
};

ILIAS_NS_END