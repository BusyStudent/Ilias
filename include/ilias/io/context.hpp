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

#include <ilias/runtime/executor.hpp>
#include <ilias/task/task.hpp>
#include <ilias/io/error.hpp>
#include <ilias/buffer.hpp>
#include <cstddef>
#include <span>

ILIAS_NS_BEGIN

class IoContext;
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
class ILIAS_API IoContext : public runtime::Executor {
public:
    /**
     * @brief Add a new system descriptor to the context
     * 
     * @param fd 
     * @param type 
     * @return IoResult<IoDescriptor*> 
     */
    virtual auto addDescriptor(fd_t fd, IoDescriptor::Type type) -> IoResult<IoDescriptor*> = 0;

    /**
     * @brief Remove a descriptor from the context, it will cancel all async operations on this descriptor
     * 
     * @param fd
     * @return IoResult<void> 
     */
    virtual auto removeDescriptor(IoDescriptor *fd) -> IoResult<void> = 0;

    /**
     * @brief Cancel all pending Io operations on a descriptor
     * 
     * @param fd
     * @return IoResult<void> 
     */
    virtual auto cancel(IoDescriptor *fd) -> IoResult<void> = 0;

    /**
     * @brief Read from a descriptor
     * 
     * @param fd 
     * @param buffer 
     * @param offset The offset in the file, std::nullopt means on ignore
     * @return IoTask<size_t> 
     */
    virtual auto read(IoDescriptor *fd, MutableBuffer buffer, std::optional<size_t> offset) -> IoTask<size_t> = 0;

    /**
     * @brief Write to a descriptor
     * 
     * @param fd 
     * @param buffer 
     * @param offset The offset in the file, std::nullopt means on ignore
     * @return IoTask<size_t> 
     */
    virtual auto write(IoDescriptor *fd, Buffer buffer, std::optional<size_t> offset) -> IoTask<size_t> = 0;

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
    virtual auto sendto(IoDescriptor *fd, Buffer buffer, int flags, EndpointView endpoint) -> IoTask<size_t> = 0;

    /**
     * @brief Receive data from a remote endpoint
     * 
     * @param fd The fd must be a socket
     * @param buffer The buffer to receive into
     * @param flags The flags to use, like MSG_DONTWAIT
     * @param endpoint The endpoint to receive from
     * @return IoTask<size_t> 
     */
    virtual auto recvfrom(IoDescriptor *fd, MutableBuffer buffer, int flags, MutableEndpointView endpoint) -> IoTask<size_t> = 0;

    /**
     * @brief Poll a descriptor for events
     * 
     * @param fd The fd must be pollable (socket on most systems)
     * @param events The events to poll, like POLLIN, POLLOUT, etc
     * @return IoTask<uint32_t> 
     */
    virtual auto poll(IoDescriptor *fd, uint32_t events) -> IoTask<uint32_t> = 0;

    /**
     * @brief Get the current thread io context
     * 
     * @return IoContext* 
     */
    static auto currentThread() -> IoContext * {
        return static_cast<IoContext*>(Executor::currentThread());
    }

#if defined(_WIN32)
    // Win32 Specific Io Operations
    /**
     * @brief Wrapping Win32 ConnectNamedPipe, The named pipe server wait for a client to connect
     * 
     * @param fd The fd must be a named pipe
     * @return IoTask<void> 
     */
    virtual auto connectNamedPipe(IoDescriptor *fd) -> IoTask<void>;

    /**
     * @brief Wrapping Win32 WaitForSingleObject, wait for a object to be signaled
     * 
     * @param object 
     * @return IoTask<void> 
     */
    virtual auto waitObject(void *object) -> IoTask<void>;
#endif // defined(_WIN32)

};

/**
 * @brief Convert an IoDescriptor::Type to a string
 * 
 * @param type 
 * @return std::string_view 
 */
inline auto toString(IoDescriptor::Type type) -> std::string_view {
    using Type = IoDescriptor::Type;
    switch (type) {
        case Type::File: return "File"; 
        case Type::Socket: return "Socket"; 
        case Type::Pipe: return "Pipe"; 
        case Type::Tty: return "Tty"; 
        case Type::Unknown: return "Unknown"; 
        default: return "Unknown"; 
    }
}

ILIAS_NS_END