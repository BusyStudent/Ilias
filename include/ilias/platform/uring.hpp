/**
 * @file uring.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Impl the io uring Context on the linux
 * @version 0.1
 * @date 2024-09-15
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/task/executor.hpp>
#include <ilias/task/task.hpp>
#include <ilias/net/endpoint.hpp>
#include <ilias/net/system.hpp>
#include <ilias/net/sockfd.hpp>
#include <ilias/io/context.hpp>
#include <liburing.h>

ILIAS_NS_BEGIN

/**
 * @brief The io context by using io_uring
 * 
 */
class UringContext : public IoContext {
public:

private:
    
};

ILIAS_NS_END