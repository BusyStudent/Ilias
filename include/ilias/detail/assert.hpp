/**
 * @file assert.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Internal used assertion
 * @version 0.1
 * @date 2026-08-15
 * 
 * @copyright Copyright (c) 2026
 * 
 */
// INTERNAL !!!
#pragma once

#include <source_location>
#include <string_view>
#include <cstdlib>
#include <cstdio>

// Take format
#include "format.hpp"

// Assertion
#if defined(NDEBUG)
    #define ILIAS_ASSERT(x, ...) do { } while (false)
#else
    #define ILIAS_ASSERT(x, ...) do {            \
        if (!(x)) [[unlikely]] {                 \
            ::ilias::assertion::handler(         \
                #x,                              \
                std::source_location::current(), \
                ##__VA_ARGS__                    \
            );                                   \
        }                                        \
    } while (false)
    #if defined(__cpp_lib_stacktrace)
        #include <stacktrace>
    #endif
#endif

// For old code compatibility
#define ILIAS_ASSERT_MSG(x, msg) ILIAS_ASSERT(x, msg) 

ILIAS_NS_BEGIN

// LCOV_EXCL_START
// MARK: Assertion
namespace assertion {

[[noreturn]]
inline auto handlerImpl(std::string_view expr, std::source_location where, std::string_view msg = {}) {
    std::fprintf(stderr, "\033[1;31m[!!! ASSERTION FAILED !!!]\033[0m\n");
    std::fprintf(stderr, "  at: %s:%d:%d\n", where.file_name(), where.line(), where.column());
    std::fprintf(stderr, "  func: %s\n", where.function_name());
    std::fprintf(stderr, "  expr: %s\n", expr.data());
    if (!msg.empty()) {
        std::fprintf(stderr, "  msg: %s\n", msg.data());
    }

#if defined(__cpp_lib_stacktrace) && defined(_MSC_VER) && !defined(NDEBUG) // Because the stacktrace require link additional library on libstdc++, so temporarily enable only on MSVC
    std::fprintf(stderr, "  stacktrace:\n");
    auto stacktrace = std::stacktrace::current();
    auto idx = 0;
    for (auto &frame : stacktrace) {
        std::fprintf(stderr, "    #%d  %s\n", idx, frame.description().c_str());
        if (frame.source_line() != 0) {
            std::fprintf(stderr, "      at %s:%d\n", frame.source_file().c_str(), frame.source_line());
        }
        idx += 1;
    }
#endif // __cpp_lib_stacktrace
    
    // Raise the debugger first
    ILIAS_TRAP();
    std::abort();
}

// Impl the assert(cond)
[[noreturn]]
inline auto handler(std::string_view cond, std::source_location where) {
    handlerImpl(cond, where);
}

// Impl the assert(cond, fmt, ...)
#if !defined(ILIAS_NO_FORMAT)
template <typename ...Args>
[[noreturn]]
inline auto handler(std::string_view cond, std::source_location where, fmtlib::format_string<Args...> fmt, Args &&...args) {
    handlerImpl(cond, where, fmtlib::format(fmt, std::forward<Args>(args)...));
}
#else // No format support
template <typename ...Args>
[[noreturn]]
inline auto handler(std::string_view cond, std::source_location where, Args &&...) {
    handlerImpl(cond, where);
}
#endif // ILIAS_FMT_NAMESPACE

} // namespace assertion
// LCOV_EXCL_STOP

ILIAS_NS_END