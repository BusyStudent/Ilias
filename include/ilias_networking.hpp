#pragma once

#include "ilias_backend.hpp"
#include "ilias_channel.hpp"
#include "ilias_async.hpp"
#include "ilias_await.hpp"
#include "ilias_task.hpp"
#include "ilias_ssl.hpp"

#if defined(_WIN32)
    #define ILIAS_PLATFORM_IO IOCPContext
    #include "ilias_iocp.hpp"
#else
    #define ILIAS_PLATFORM_IO PollContext
    #include "ilias_poll.hpp"
#endif

ILIAS_NS_BEGIN

using PlatformIoContext = ILIAS_PLATFORM_IO;

ILIAS_NS_END