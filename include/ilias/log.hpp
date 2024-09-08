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

#include <ilias/ilias.hpp>

// --- Check headers
#if !defined(__cpp_lib_format)
    #undef ILIAS_ENABLE_LOG
#endif


// --- Begin declaration
#if defined(ILIAS_ENABLE_LOG)

// --- Prefer print over format
#if defined(__cpp_lib_print)
    #include <print>
#endif

#include <ilias/detail/mem.hpp>
#include <format>
#include <string>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <set>

#define ILIAS_LOG_MAKE_LEVEL(name) ::ILIAS_NAMESPACE::detail::LogLevel::name
#define ILIAS_LOG_SET_LEVEL(level_) ::ILIAS_NAMESPACE::detail::GetLogContext().level = level_
#define ILIAS_LOG_ADD_WHITELIST(mod) ::ILIAS_NAMESPACE::detail::GetLogContext().whitelist.insert(mod)
#define ILIAS_LOG_ADD_BLACKLIST(mod) ::ILIAS_NAMESPACE::detail::GetLogContext().blacklist.insert(mod)
#define ILIAS_LOG(level, mod, ...) ::ILIAS_NAMESPACE::detail::Log(stderr, level, mod, __VA_ARGS__)

ILIAS_NS_BEGIN

namespace detail {

/**
 * @brief The Log level
 * 
 */
enum class LogLevel {
    Trace,
    Info,
    Warn,
    Error,
};

struct LogContext {
    LogLevel level = LogLevel::Info;
    std::set<std::string, mem::CaseCompare> whitelist;
    std::set<std::string, mem::CaseCompare> blacklist;
};

/**
 * @brief Get the Log Context object
 * 
 * @return LogContext& 
 */
inline auto GetLogContext() -> LogContext & {
    static LogContext ctxt;
    return ctxt;
}

inline auto GetLevelString(LogLevel level) -> std::string_view {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        default: return "UNKNOWN";
    }
}

inline auto GetLevelColor(LogLevel level) -> const char * {
    switch (level) {
        case LogLevel::Trace: return "\033[1;34m";
        case LogLevel::Info:  return "\033[1;32m";
        case LogLevel::Warn:  return "\033[1;33m";
        case LogLevel::Error: return "\033[1;31m";
        default: return "\033[1;30m";
    }
}

/**
 * @brief Format and write to the stream
 * 
 * @tparam Args 
 * @param stream 
 * @param args 
 */
template <typename ...Args>
inline auto Print(FILE *stream, std::format_string<Args...> fmt, Args &&...args) -> void {
#if defined(__cpp_lib_print)
    std::print(stream, fmt, std::forward<Args>(args)...);
#else
    std::fputs(std::format(fmt, std::forward<Args>(args)...).c_str(), stream);
#endif
}

/**
 * @brief Begin the Logging
 * 
 * @param stream 
 * @param level 
 * @param mod 
 * @return true 
 * @return false On this log is flitered
 */
inline auto BeginLog(FILE *stream, LogLevel level, std::string_view mod) -> bool {
    auto &ctxt = GetLogContext();
    if (int(level) < int(ctxt.level)) {
        return false;
    }
    if (ctxt.blacklist.contains(mod)) {
        return false;
    }
    if (!ctxt.whitelist.empty() && !ctxt.whitelist.contains(mod)) {
        return false;
    }

    // Set the color
    std::fputs(GetLevelColor(level), stream);

    // Print the body
    auto now = std::chrono::system_clock::now();
    Print(stream, "[{}] [{}/{}]: ", now, mod, GetLevelString(level));
    return true;
}

inline auto EndLog(FILE *stream, LogLevel level) -> void {
    // Clear the color
    std::fputs("\033[0m\n", stream);
}

/**
 * @brief Do the Log
 * 
 * @tparam Args 
 * @param stream 
 * @param level
 * @param mod The module name
 * @param args 
 */
template <typename ...Args>
auto Log(FILE *stream, LogLevel level, std::string_view mod, std::format_string<Args...> fmt, Args &&...args) -> void {
    if (!BeginLog(stream, level, mod)) {
        return;
    }
    Print(stream, fmt, std::forward<Args>(args)...);
    EndLog(stream, level);
}

} // namespace detail

ILIAS_NS_END

#else
    // --- Disable log
    #define ILIAS_LOG_MAKE_LEVEL(name) 0
    #define ILIAS_LOG_SET_LEVEL(level) do { } while(0)
    #define ILIAS_LOG_ADD_WHITELIST(mod) do { } while(0)
    #define ILIAS_LOG_ADD_BLACKLIST(mod) do { } while(0)
    #define ILIAS_LOG(level, mod, ...) do { } while(0)
#endif


// --- Define macros for common part
#define ILIAS_WARN(mod, ...) ILIAS_LOG(ILIAS_WARN_LEVEL, mod, __VA_ARGS__)
#define ILIAS_ERROR(mod, ...) ILIAS_LOG(ILIAS_ERROR_LEVEL, mod, __VA_ARGS__)
#define ILIAS_INFO(mod, ...) ILIAS_LOG(ILIAS_INFO_LEVEL, mod, __VA_ARGS__)
#define ILIAS_TRACE(mod, ...) ILIAS_LOG(ILIAS_TRACE_LEVEL, mod, __VA_ARGS__)

#define ILIAS_TRACE_LEVEL ILIAS_LOG_MAKE_LEVEL(Trace)
#define ILIAS_INFO_LEVEL  ILIAS_LOG_MAKE_LEVEL(Info)
#define ILIAS_WARN_LEVEL  ILIAS_LOG_MAKE_LEVEL(Warn)
#define ILIAS_ERROR_LEVEL ILIAS_LOG_MAKE_LEVEL(Error)