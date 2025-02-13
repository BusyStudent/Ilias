/**
 * @file context.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The IoContext class, provides the context for io operations, such as file io, socket io, timer, etc.
 * @version 0.1
 * @date 2024-08-11
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/task/executor.hpp>
#include <ilias/task/task.hpp>
#include <optional>
#include <cstddef>
#include <span>

ILIAS_NS_BEGIN

class EndpointView;
class MutableEndpointView;

/**
 * @brief The IoDescriptor class, user should only take the pointer to this class, it is opaque to the user
 * 
 */
class IoDescriptor {
public:
    /**
     * @brief The type of the descriptor, used on addDescriptor
     * 
     */
    enum Type {
        Socket,  //< Socket descriptor
        File,    //< Generic file descriptor
        Tty,     //< TTY descriptor
        Pipe,    //< Pipe descriptor
        Unknown, //< Unknown type, let the backend decide by os api
        User,    //< User defined type, used for custom more type for backend
    };
protected:
    IoDescriptor() = default;
    ~IoDescriptor() = default;
};

/**
 * @brief The IoContext class, provides the context for io operations, such as file io, socket io, timer, etc.
 * 
 */
class IoContext : public Executor {
public:
    /**
     * @brief Add a new system descriptor to the context
     * 
     * @param fd 
     * @param type 
     * @return Result<IoDescriptor*> 
     */
    virtual auto addDescriptor(fd_t fd, IoDescriptor::Type type) -> Result<IoDescriptor*> = 0;

    /**
     * @brief Remove a descriptor from the context, it will cancel all async operations on this descriptor
     * 
     * @param fd
     * @return Result<void> 
     */
    virtual auto removeDescriptor(IoDescriptor *fd) -> Result<void> = 0;

    /**
     * @brief Read from a descriptor
     * 
     * @param fd 
     * @param buffer 
     * @param offset The offset in the file, std::nullopt means on ignore
     * @return IoTask<size_t> 
     */
    virtual auto read(IoDescriptor *fd, std::span<std::byte> buffer, std::optional<size_t> offset) -> IoTask<size_t> = 0;

    /**
     * @brief Write to a descriptor
     * 
     * @param fd 
     * @param buffer 
     * @param offset The offset in the file, std::nullopt means on ignore
     * @return IoTask<size_t> 
     */
    virtual auto write(IoDescriptor *fd, std::span<const std::byte> buffer, std::optional<size_t> offset) -> IoTask<size_t> = 0;

    /**
     * @brief Connect to a remote endpoint
     * 
     * @param fd The fd must be a socket
     * @param endpoint 
     * @return IoTask<void> 
     */
    virtual auto connect(IoDescriptor *fd, EndpointView endpoint) -> IoTask<void> = 0;

    /**
     * @brief Accept a connection
     * 
     * @param fd The fd must be a listening socket
     * @param remoteEndpoint The endpoint of the remote, if nullptr, the endpoint will be ignored
     * @return IoTask<socket_t> 
     */
    virtual auto accept(IoDescriptor *fd, MutableEndpointView remoteEndpoint) -> IoTask<socket_t> = 0;

    /**
     * @brief Send data to a remote endpoint
     * 
     * @param fd The fd must be a socket
     * @param buffer The buffer to send
     * @param flags The flags to use, like MSG_DONTWAIT
     * @param endpoint The endpoint to send to, if nullptr, the fd must be connected
     * @return IoTask<size_t> 
     */
    virtual auto sendto(IoDescriptor *fd, std::span<const std::byte> buffer, int flags, EndpointView endpoint) -> IoTask<size_t> = 0;

    /**
     * @brief Receive data from a remote endpoint
     * 
     * @param fd The fd must be a socket
     * @param buffer The buffer to receive into
     * @param flags The flags to use, like MSG_DONTWAIT
     * @param endpoint The endpoint to receive from
     * @return IoTask<size_t> 
     */
    virtual auto recvfrom(IoDescriptor *fd, std::span<std::byte> buffer, int flags, MutableEndpointView endpoint) -> IoTask<size_t> = 0;

    /**
     * @brief Poll a descriptor for events
     * 
     * @param fd The fd must suport polling (like socket or pipe)
     * @param event The event to poll for (like PollEvent::In)
     * @return IoTask<uint32_t> The event we actualy got
     */
    virtual auto poll(IoDescriptor *fd, uint32_t event) -> IoTask<uint32_t> = 0;

    /**
     * @brief Get the current thread io context
     * 
     * @return IoContext* 
     */
    static auto currentThread() -> IoContext * {
        return static_cast<IoContext*>(Executor::currentThread());
    }

#if defined(_WIN32)
    /**
     * @brief Wrapping Win32 ConnectNamedPipe, The named pipe server wait for a client to connect
     * 
     * @param fd The fd must be a named pipe
     * @return IoTask<void> 
     */
    virtual auto connectNamedPipe(IoDescriptor *fd) -> IoTask<void> {
        co_return Unexpected(Error::OperationNotSupported); //< Default Not implemented
    }
#endif // defined(_WIN32)

};

/**
 * @brief Get the current io context in the coroutine
 * 
 * @return std::reference_wrapper<IoContext>
 */
inline auto currentIoContext() {
    struct Awaiter {
        auto await_ready() const -> bool { 
            return false; 
        }
        auto await_suspend(CoroHandle handle) -> bool {
            mCtxt = static_cast<IoContext*>(handle.executor());
#if defined(__cpp_rtti)
            ILIAS_ASSERT(dynamic_cast<IoContext*>(handle.executor())); //< Check that the executor impl the IoContext
#endif // defined(__cpp_rtti)
            return false;
        }
        auto await_resume() const -> std::reference_wrapper<IoContext> { 
            return *mCtxt; 
        }
        IoContext *mCtxt;
    };
    return Awaiter {};
}


ILIAS_NS_END