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
 * @brief The IoDescriptor class, user should only take the pointer to this class
 * 
 */
class IoDescriptor {
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
     * @param flags 
     * @return Result<IoDescriptor*> 
     */
    virtual auto addDescriptor(fd_t fd, int flags) -> Result<IoDescriptor*> = 0;

    /**
     * @brief Remove a descriptor from the context
     * 
     * @param desc 
     * @return Result<void> 
     */
    virtual auto removeDescriptor(IoDescriptor* desc) -> Result<void> = 0;

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
     * @param offset 
     * @return Task<size_t> 
     */
    virtual auto read(IoDescriptor *fd, std::span<std::byte> buffer, std::optional<size_t> offset) -> Task<size_t> = 0;

    /**
     * @brief Write to a descriptor
     * 
     * @param fd 
     * @param buffer 
     * @param offset 
     * @return Task<size_t> 
     */
    virtual auto write(IoDescriptor *fd, std::span<const std::byte> buffer, std::optional<size_t> offset) -> Task<size_t> = 0;

    /**
     * @brief Connect to a remote endpoint
     * 
     * @param fd 
     * @param endpoint 
     * @return Task<void> 
     */
    virtual auto connect(IoDescriptor *fd, const IPEndpoint &endpoint) -> Task<void> = 0;

    /**
     * @brief Accept a connection
     * 
     * @param fd The fd must be a listening socket
     * @return Task<socket_t> 
     */
    virtual auto accept(IoDescriptor *fd) -> Task<socket_t> = 0;

    /**
     * @brief Send data to a remote endpoint
     * 
     * @param fd The fd must be a socket
     * @param buffer The buffer to send
     * @param endpoint The endpoint to send to, if nullptr, the fd must be connected
     * @return Task<size_t> 
     */
    virtual auto sendto(IoDescriptor *fd, std::span<const std::byte> buffer, const IPEndpoint *endpoint) -> Task<size_t> = 0;

    /**
     * @brief Receive data from a remote endpoint
     * 
     * @param fd The fd must be a socket
     * @param buffer The buffer to receive into
     * @param endpoint The endpoint to receive from
     * @return Task<size_t> 
     */
    virtual auto recvfrom(IoDescriptor *fd, std::span<std::byte> buffer, IPEndpoint *endpoint) -> Task<size_t> = 0;
};


ILIAS_NS_END