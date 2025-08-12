/**
 * @file log.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The minimal logging system
 * @version 0.1
 * @date 2024-08-11
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/defines.hpp>

// Check headers
#if defined(ILIAS_NO_FORMAT)
    #undef ILIAS_USE_LOG
#endif

// Begin declaration
#if defined(ILIAS_USE_LOG)

#include <source_location>
#include <string>

#define ILIAS_LOG_MAKE_LEVEL(name) ::ILIAS_NAMESPACE::logging::LogLevel::name
#define ILIAS_LOG_SET_LEVEL(level_) ::ILIAS_NAMESPACE::logging::setLevel(level_)
#define ILIAS_LOG_ADD_WHITELIST(mod) ::ILIAS_NAMESPACE::logging::addWhitelist(mod)
#define ILIAS_LOG_ADD_BLACKLIST(mod) ::ILIAS_NAMESPACE::logging::addBlacklist(mod)
#define ILIAS_LOG(level, mod, ...)                                                                                                          \
    do {                                                                                                                                    \
        if (::ILIAS_NAMESPACE::logging::check(level, mod)) {                                                                                \
            ::ILIAS_NAMESPACE::logging::write(level, mod, std::source_location::current(), ::ILIAS_NAMESPACE::fmtlib::format(__VA_ARGS__)); \
        }                                                                                                                                   \
    }                                                                                                                                       \
    while (0)

ILIAS_NS_BEGIN

namespace logging {

/**
 * @brief The Log level
 * 
 */
enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Off
};

extern auto ILIAS_API write(LogLevel level, std::string_view mod, std::source_location where, std::string_view content) -> void;
extern auto ILIAS_API check(LogLevel level, std::string_view mod) -> bool;
extern auto ILIAS_API setLevel(LogLevel level) -> void;
extern auto ILIAS_API addWhitelist(std::string_view mod) -> void;
extern auto ILIAS_API addBlacklist(std::string_view mod) -> void;

} // namespace logging

ILIAS_NS_END

#else
    // Disable log
    #define ILIAS_LOG_MAKE_LEVEL(name) 0
    #define ILIAS_LOG_SET_LEVEL(level) do { } while(0)
    #define ILIAS_LOG_ADD_WHITELIST(mod) do { } while(0)
    #define ILIAS_LOG_ADD_BLACKLIST(mod) do { } while(0)
    #define ILIAS_LOG(level, mod, ...) do { } while(0)
#endif


// Define macros for common part
#define ILIAS_WARN(mod, ...) ILIAS_LOG(ILIAS_WARN_LEVEL, mod, __VA_ARGS__)
#define ILIAS_ERROR(mod, ...) ILIAS_LOG(ILIAS_ERROR_LEVEL, mod, __VA_ARGS__)
#define ILIAS_INFO(mod, ...) ILIAS_LOG(ILIAS_INFO_LEVEL, mod, __VA_ARGS__)
#define ILIAS_TRACE(mod, ...) ILIAS_LOG(ILIAS_TRACE_LEVEL, mod, __VA_ARGS__)
#define ILIAS_DEBUG(mod, ...) ILIAS_LOG(ILIAS_DEBUG_LEVEL, mod, __VA_ARGS__)

#define ILIAS_TRACE_LEVEL ILIAS_LOG_MAKE_LEVEL(Trace)
#define ILIAS_DEBUG_LEVEL ILIAS_LOG_MAKE_LEVEL(Debug)
#define ILIAS_INFO_LEVEL  ILIAS_LOG_MAKE_LEVEL(Info)
#define ILIAS_WARN_LEVEL  ILIAS_LOG_MAKE_LEVEL(Warn)
#define ILIAS_ERROR_LEVEL ILIAS_LOG_MAKE_LEVEL(Error)