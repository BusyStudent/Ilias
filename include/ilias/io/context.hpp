#pragma once

#include <ilias/task/executor.hpp>
#include <ilias/task/task.hpp>
#include <optional>
#include <cstddef>
#include <span>

ILIAS_NS_BEGIN

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
    virtual auto addDescriptor(fd_t fd, int flags) -> Result<IoDescriptor*> = 0;
    virtual auto removeDescriptor(IoDescriptor* desc) -> Result<void> = 0;

    virtual auto read(IoDescriptor *fd, std::span<std::byte> buffer, std::optional<size_t> offset) -> Task<size_t> = 0;
    virtual auto write(IoDescriptor *fd, std::span<const std::byte> buffer, std::optional<size_t> offset) -> Task<size_t> = 0;
};


ILIAS_NS_END