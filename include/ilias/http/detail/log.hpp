/**
 * @file log.hpp
 * @author llhsdmd (llhsdmd@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-07-10
 *
 * @copyright Copyright (c) 2024
 *
 */
#pragma once

#if !defined(NDEBUG) && !defined(HPACK_NDEBUG)
#define HPACK_DEBUG
#endif

#if !defined(HPACK_NLOG)
#define HPACK_LOG
#endif

#if defined(HPACK_DEBUG) && defined(HPACK_LOG_CONTEXT)
#include <fmt/format.h>
#define HPACK_DEBUG(fmt, ...) printf("Debug -- [%s:%s] %s", __FILE__, __LINE__, std::format(fmt, ##__VA_ARGS__).c_str())
#define HPACK_ASSERT(cond, fmt, ...)                                                                                   \
    if (!(cond)) {                                                                                                     \
        printf("Assert -- [%s:%s] %s", __FILE__, __LINE__, std::format(fmt, ##__VA_ARGS__).c_str());                   \
        abort();                                                                                                       \
    }
#elif defined(HPACK_DEBUG)
#include <fmt/format.h>
#define HPACK_DEBUG(...) printf("Debug -- [%s:%s] %s", __FILE__, __LINE__, std::format(##__VA_ARGS__).c_str())
#define HPACK_ASSERT(cond, ...)                                                                                        \
    if (!(cond)) {                                                                                                     \
        printf("Assert -- [%s:%s] %s", __FILE__, __LINE__, std::format(fmt, ##__VA_ARGS__).c_str());                   \
        abort();                                                                                                       \
    }
#else
#define HPACK_ASSERT(cond, fmt, ...)
#define HPACK_DEBUG(...)
#endif

#if defined(HPACK_LOG) && defined(HPACK_LOG_CONTEXT)
#include <fmt/format.h>
#define HPACK_LOG_INFO(fmt, ...)                                                                                       \
    printf("Info -- [%s:%s] %s", __FILE__, __LINE__, std::format(fmt, ##__VA_ARGS__).c_str())
#define HPACK_LOG_WARN(fmt, ...)                                                                                       \
    printf("Warn -- [%s:%s] %s", __FILE__, __LINE__, std::format(fmt, ##__VA_ARGS__).c_str())
#define HPACK_LOG_ERROR(fmt, ...)                                                                                      \
    printf("Error -- [%s:%s] %s", __FILE__, __LINE__, std::format(fmt, ##__VA_ARGS__).c_str())
#define HPACK_LOG_FATAL(fmt, ...)                                                                                      \
    printf("Fata -- [%s:%s] %s", __FILE__, __LINE__, std::format(fmt, ##__VA_ARGS__).c_str());                         \
    abort();
#elif defined(HPACK_LOG)
#include <fmt/format.h>
#define HPACK_LOG_INFO(...) printf("Info -- %s", std::format(##__VA_ARGS__).c_str())
#define HPACK_LOG_WARN(...) printf("Warn -- %s", std::format(##__VA_ARGS__).c_str())
#define HPACK_LOG_ERROR(...) printf("Error -- %s", std::format(##__VA_ARGS__).c_str())
#define HPACK_LOG_FATAL(...)                                                                                           \
    printf("Fata -- %s", std::format(##__VA_ARGS__).c_str());                                                          \
    abort();
#else
#define HPACK_LOG_INFO(...)
#define HPACK_LOG_WARN(...)
#define HPACK_LOG_ERROR(...)
#define HPACK_LOG_FATAL(...)
#endif
