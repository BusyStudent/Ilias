/**
 * @file ilias.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Import all modules of ilias
 * @version 0.1
 * @date 2025-10-11
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#pragma once

#include <ilias/platform.hpp>
#include <ilias/process.hpp>
#include <ilias/console.hpp>
#include <ilias/buffer.hpp>
#include <ilias/signal.hpp>
#include <ilias/fiber.hpp>
#include <ilias/task.hpp>
#include <ilias/sync.hpp>
#include <ilias/net.hpp>
#include <ilias/tls.hpp>
#include <ilias/fs.hpp>
#include <ilias/io.hpp>

// Platform extra headers
#if defined(_WIN32)
    #include <ilias/platform/winmsg.hpp>
#endif // _WIN32