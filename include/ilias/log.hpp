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

// --- Check headers
#if defined(ILIAS_NO_FORMAT)
    #undef ILIAS_ENABLE_LOG
#endif


// --- Begin declaration
#if defined(ILIAS_ENABLE_LOG)

#include <ilias/detail/mem.hpp>
#include <source_location>
#include <string>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <set>

#define ILIAS_LOG_MAKE_LEVEL(name) ::ILIAS_NAMESPACE::detail::LogLevel::name
#define ILIAS_LOG_SET_LEVEL(level_) ::ILIAS_NAMESPACE::detail::getLogContext().level = level_
#define ILIAS_LOG_ADD_WHITELIST(mod) ::ILIAS_NAMESPACE::detail::getLogContext().whitelist.insert(mod)
#define ILIAS_LOG_ADD_BLACKLIST(mod) ::ILIAS_NAMESPACE::detail::getLogContext().blacklist.insert(mod)
#define ILIAS_LOG(level, mod, ...) ::ILIAS_NAMESPACE::detail::log(stderr, level, mod, std::source_location::current(), __VA_ARGS__)

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
    bool notime = ::getenv("ILIAS_LOG_NOTIME");    //< Disable the show time in the log
    bool nolocation = ::getenv("ILIAS_LOG_NOLOC"); //< Disable the show location in the log
};

/**
 * @brief Get the Log Context object
 * 
 * @return LogContext& 
 */
inline auto getLogContext() -> LogContext & {
    static LogContext ctxt;
    return ctxt;
}

inline auto getLevelString(LogLevel level) -> std::string_view {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        default: return "UNKNOWN";
    }
}

inline auto getLevelColor(LogLevel level) -> const char * {
    switch (level) {
        case LogLevel::Trace: return "\033[1;34m";
        case LogLevel::Info:  return "\033[1;32m";
        case LogLevel::Warn:  return "\033[1;33m";
        case LogLevel::Error: return "\033[1;31m";
        default: return "\033[1;30m";
    }
}

/**
 * @brief Begin the Logging
 * 
 * @param buf 
 * @param level 
 * @param mod 
 * @param loc
 * @return true 
 * @return false On this log is flitered
 */
inline auto beginLog(std::string &buf, LogLevel level, std::string_view mod, std::source_location loc) -> bool {
    auto &ctxt = getLogContext();
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
    buf += getLevelColor(level);

    // Print the body
    auto now = std::chrono::system_clock::now();
    auto file = std::string_view(loc.file_name());
    auto func = std::string_view(loc.function_name());
    auto line = loc.line();

    // Remove the path, only keep the file name
    if (auto pos = file.find_last_of('/'); pos != std::string_view::npos) {
        file = file.substr(pos + 1);
    }
    if (auto pos = file.find_last_of('\\'); pos != std::string_view::npos) {
        file = file.substr(pos + 1);
    }
    if (!ctxt.notime) {
        fmtlib::format_to(std::back_inserter(buf), "[{}] ", now);
    }
    fmtlib::format_to(std::back_inserter(buf), "[{}/{}]: ", getLevelString(level), mod);
    if (!ctxt.nolocation) {
        fmtlib::format_to(std::back_inserter(buf), "[{}:{}] ", file, line);
    }
    // Clear the color
    buf += "\033[0m";
    return true;
}

inline auto endLog(std::string &buf, LogLevel level) -> void {
    buf += "\n";
}

/**
 * @brief Do the Log
 * 
 * @tparam Args 
 * @param stream 
 * @param level
 * @param mod The module name
 * @param loc The source location
 * @param args 
 */
template <typename ...Args>
auto log(FILE *stream, LogLevel level, std::string_view mod, std::source_location loc, fmtlib::format_string<Args...> fmt, Args &&...args) -> void {
    std::string buf;
    if (!beginLog(buf, level, mod, loc)) {
        return;
    }
    fmtlib::format_to(std::back_inserter(buf), fmt, std::forward<Args>(args)...);
    endLog(buf, level);
    ::fputs(buf.c_str(), stream);
    ::fflush(stream);
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