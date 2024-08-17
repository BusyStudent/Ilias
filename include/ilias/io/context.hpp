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

class IPEndpoint;

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
        Socket, //< Socket descriptor
        File,   //< Generic file descriptor
        Tty,    //< TTY descriptor
        Pipe,   //< Pipe descriptor
        Unknown //< Unknown type, let the backend decide by os api
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
     * @brief Remove a descriptor from the context
     * 
     * @param fd
     * @return Result<void> 
     */
    virtual auto removeDescriptor(IoDescriptor *fd) -> Result<void> = 0;

    /**
     * @brief Sleep for a specified amount of time
     * 
     * @param ms 
     * @return Task<void> 
     */
    virtual auto sleep(uint64_t ms) -> Task<void> = 0;

    /**
     * @brief Read from a descriptor
     * 
     * @param fd 
     * @param buffer 
     * @param offset The offset in the file, std::nullopt means on ignore
     * @return Task<size_t> 
     */
    virtual auto read(IoDescriptor *fd, std::span<std::byte> buffer, std::optional<size_t> offset) -> Task<size_t> = 0;

    /**
     * @brief Write to a descriptor
     * 
     * @param fd 
     * @param buffer 
     * @param offset The offset in the file, std::nullopt means on ignore
     * @return Task<size_t> 
     */
    virtual auto write(IoDescriptor *fd, std::span<const std::byte> buffer, std::optional<size_t> offset) -> Task<size_t> = 0;

    /**
     * @brief Connect to a remote endpoint
     * 
     * @param fd The fd must be a socket
     * @param endpoint 
     * @return Task<void> 
     */
    virtual auto connect(IoDescriptor *fd, const IPEndpoint &endpoint) -> Task<void> = 0;

    /**
     * @brief Accept a connection
     * 
     * @param fd The fd must be a listening socket
     * @param remoteEndpoint The endpoint of the remote, if nullptr, the endpoint will be ignored
     * @return Task<socket_t> 
     */
    virtual auto accept(IoDescriptor *fd, IPEndpoint *remoteEndpoint) -> Task<socket_t> = 0;

    /**
     * @brief Send data to a remote endpoint
     * 
     * @param fd The fd must be a socket
     * @param buffer The buffer to send
     * @param flags The flags to use, like MSG_DONTWAIT
     * @param endpoint The endpoint to send to, if nullptr, the fd must be connected
     * @return Task<size_t> 
     */
    virtual auto sendto(IoDescriptor *fd, std::span<const std::byte> buffer, int flags, const IPEndpoint *endpoint) -> Task<size_t> = 0;

    /**
     * @brief Receive data from a remote endpoint
     * 
     * @param fd The fd must be a socket
     * @param buffer The buffer to receive into
     * @param flags The flags to use, like MSG_DONTWAIT
     * @param endpoint The endpoint to receive from
     * @return Task<size_t> 
     */
    virtual auto recvfrom(IoDescriptor *fd, std::span<std::byte> buffer, int flags, IPEndpoint *endpoint) -> Task<size_t> = 0;

    /**
     * @brief Poll a descriptor for events
     * 
     * @param fd The fd must suport polling (like socket or pipe)
     * @param event The event to poll for (like PollEvent::In)
     * @return Task<uint32_t> The event we actualy got
     */
    virtual auto poll(IoDescriptor *fd, uint32_t event) -> Task<uint32_t> = 0;

    /**
     * @brief Get the current thread io context
     * 
     * @return IoContext* 
     */
    static auto currentThread() -> IoContext * {
        return static_cast<IoContext*>(Executor::currentThread());
    }
};


ILIAS_NS_END