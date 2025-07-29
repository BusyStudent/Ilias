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
#elif defined(ILIAS_USE_IO_URING)
    #include <ilias/platform/uring.hpp>
    #define ILIAS_PLATFORM_CONTEXT UringContext
#else
    #include <ilias/platform/epoll.hpp>
    #define ILIAS_PLATFORM_CONTEXT EpollContext
#endif


/**
 * @brief Declare the coroutine main function.
 * 
 * @param ctxt The context type.
 * @param ... The parameters of the main function.
 */
#define ilias_main4(ctxt, ...)                                                  \
    _ilias_tags();                                                              \
    using ILIAS_NAMESPACE::Task;                                                \
    static auto _ilias_main(__VA_ARGS__) -> Task<decltype(_ilias_tags())>;      \
    auto main(int argc, char ** argv) -> int {                                  \
        ctxt context;                                                           \
        context.install();                                                      \
        auto makeTask = [&](auto callable) {                                    \
            if constexpr (std::invocable<decltype(callable)>) {                 \
                return callable();                                              \
            }                                                                   \
            else {                                                              \
                static_assert(                                                  \
                    std::invocable<decltype(callable), int, char **>,           \
                    "Bad main function signature"                               \
                );                                                              \
                return callable(argc, argv);                                    \
            }                                                                   \
        };                                                                      \
        auto invoke = [&](auto callable) {                                      \
            auto task = makeTask(callable);                                     \
            if constexpr (std::is_same_v<decltype(task), Task<void> >) {        \
                std::move(task).wait();                                         \
                return 0;                                                       \
            }                                                                   \
            else {                                                              \
                return std::move(task).wait();                                  \
            }                                                                   \
        };                                                                      \
        return invoke(_ilias_main);                                             \
    }                                                                           \
    static auto _ilias_main(__VA_ARGS__) -> Task<decltype(_ilias_tags())>       

/**
 * @brief Declare the coroutine main function.
 * 
 * @param ... The parameters of the main function.
 */
#define ilias_main(...) ilias_main4(ILIAS_NAMESPACE::PlatformContext, __VA_ARGS__)


ILIAS_NS_BEGIN

/**
 * @brief The Automatically selected platform context, IOCP on windows, epoll on linux
 * 
 */
using PlatformContext = ILIAS_PLATFORM_CONTEXT;

ILIAS_NS_END