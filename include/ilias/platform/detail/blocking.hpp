/**
 * @file blocking.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Some blocking IO operations. submit in threadpool.
 * @version 0.1
 * @date 2026-06-27
 * 
 * @copyright Copyright (c) 2026
 * 
 */
#pragma once

#include <ilias/task/task.hpp>
#include <ilias/io/error.hpp>
#include <ilias/buffer.hpp>
#include <optional>
#include <cstddef>

ILIAS_NS_BEGIN

namespace runtime::threadpool {
    extern auto ILIAS_API read(fd_t fd, MutableBuffer buffer, std::optional<size_t> offset) -> IoTask<size_t>;
    extern auto ILIAS_API write(fd_t fd, Buffer buffer, std::optional<size_t> offset) -> IoTask<size_t>;
} // namespace runtime::threadpool

ILIAS_NS_END