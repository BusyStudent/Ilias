#pragma once

#include "coro.hpp"
#include "task.hpp"
#include "net.hpp"

#if defined(_WIN32)
    #define ILIAS_PLATFORM_CONTEXT IOCPContext
    #include "net/iocp.hpp"
#else
    #define ILIAS_PLATFORM_CONTEXT PollContext
    #include "net/poll.hpp"
#endif


ILIAS_NS_BEGIN

using PlatformIoContext = ILIAS_PLATFORM_CONTEXT;

ILIAS_NS_END