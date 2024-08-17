/**
 * @file platform.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief A helper header for auto selection of best platform context.
 * @version 0.1
 * @date 2024-08-18
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#if defined(_WIN32)
    #include <ilias/platform/iocp.hpp>
    #define ILIAS_PLATFORM_CONTEXT IocpContext
#else
    #error "Unsupported platform"
#endif

ILIAS_NS_BEGIN

using PlatformContext = ILIAS_PLATFORM_CONTEXT;

ILIAS_NS_END