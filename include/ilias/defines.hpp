#pragma once

/**
 * @file defines.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief for defined some macros and basic platform detection
 * @version 0.3
 * @date 2024-07-18
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#include <ilias/detail/config.hpp>
#include <source_location>
#include <concepts>
#include <version>
#include <cstdlib>
#include <cstdio>
#include <string>

#define ILIAS_NAMESPACE ilias

// Format check
#if   defined(ILIAS_USE_FMT)
    #define ILIAS_FMT_NAMESPACE fmt
    #include <fmt/format.h>
    #include <fmt/chrono.h>
#elif defined(__cpp_lib_format)
    #define ILIAS_FMT_NAMESPACE std
    #include <format>
#else
    #define ILIAS_NO_FORMAT
#endif

// Exception check
#if !defined(__cpp_exceptions)
    #define ILIAS_THROW(x) ::abort()
    #define ILIAS_TRY if constexpr(true)
    #define ILIAS_CATCH(x) if constexpr(false)
#else
    #define ILIAS_THROW(x) throw x
    #define ILIAS_TRY try
    #define ILIAS_CATCH(x) catch(x)
#endif

// Assertion
#if defined(NDEBUG)
    #define ILIAS_ASSERT(x, ...) do { } while (0)
#else
    #define ILIAS_ASSERT(x, ...) do {            \
        if (!(x)) {                              \
            ::ilias::assertion::handler(         \
                ILIAS_STRINGIFY(x),              \
                std::source_location::current(), \
                ##__VA_ARGS__                    \
            );                                   \
        }                                        \
    } while (0)
    #if defined(__cpp_lib_stacktrace)
        #include <stacktrace>
    #endif
#endif

// Platform detection
#if   defined(_WIN32)
    #define ILIAS_EXPORT   ILIAS_ATTRIBUTE(dllexport)
    #define ILIAS_IMPORT   ILIAS_ATTRIBUTE(dllimport)
    #define ILIAS_ERROR_T  ::uint32_t
    #define ILIAS_SOCKET_T ::uintptr_t
    #define ILIAS_FD_T      void *
#elif defined(__linux__)
    #define ILIAS_EXPORT   ILIAS_ATTRIBUTE(visibility("default"))
    #define ILIAS_IMPORT   // no-op
    #define ILIAS_ERROR_T   int
    #define ILIAS_SOCKET_T  int
    #define ILIAS_FD_T      int
#else
    #error "Unsupported platform"
#endif

// Compiler check
#if   defined(_MSC_VER)
    #define ILIAS_NO_UNIQUE_ADDRESS no_unique_address, msvc::no_unique_address
    #define ILIAS_ATTRIBUTE(x)  __declspec(x)
    #define ILIAS_UNREACHABLE() __assume(0)
    #define ILIAS_ASSUME(...)   __assume(__VA_ARGS__)
    #define ILIAS_TRAP()        __debugbreak()
#elif defined(__GNUC__)
    #define ILIAS_ATTRIBUTE(x)  __attribute__((x))
    #define ILIAS_UNREACHABLE() __builtin_unreachable()
    #define ILIAS_ASSUME(...)   if (!(__VA_ARGS__)) __builtin_unreachable()
    #define ILIAS_TRAP()        __builtin_trap()
#else
    #define ILIAS_ATTRIBUTE(x) // no-op
    #define ILIAS_UNREACHABLE() // no-op
    #define ILIAS_ASSUME(...) // no-op
    #define ILIAS_TRAP() // no-op
#endif

#if  !defined(ILIAS_NO_UNIQUE_ADDRESS)
    #define ILIAS_NO_UNIQUE_ADDRESS no_unique_address
#endif

// Library mode
#if   defined(ILIAS_STATIC) // Static library, no-op
    #define ILIAS_API
#elif defined(ILIAS_DLL)    // Dynamic library
    #if defined(_ILIAS_SOURCE) // Dynamic library
        #define ILIAS_API ILIAS_EXPORT
    #else
        #define ILIAS_API ILIAS_IMPORT
    #endif
#else
    #error "Library mode not specified"
#endif // ILIAS_STATIC

// Utils macro
#define ILIAS_ASSERT_MSG(x, msg) ILIAS_ASSERT(x, msg) // For old code
#define ILIAS_STRINGIFY_(x) #x
#define ILIAS_STRINGIFY(x) ILIAS_STRINGIFY_(x)
#define ILIAS_NS_BEGIN namespace ILIAS_NAMESPACE {
#define ILIAS_NS_END }

// Version helper
#define ILIAS_VERSION_AT_LEAST(major, minor, patch)                  \
    (ILIAS_VERSION_MAJOR > major ||                                  \
    (ILIAS_VERSION_MAJOR == major && ILIAS_VERSION_MINOR > minor) || \
    (ILIAS_VERSION_MAJOR == major && ILIAS_VERSION_MINOR == minor && ILIAS_VERSION_PATCH >= patch))
#define ILIAS_VERSION_STRING                                         \
    ILIAS_STRINGIFY(ILIAS_VERSION_MAJOR) "."                         \
    ILIAS_STRINGIFY(ILIAS_VERSION_MINOR) "."                         \
    ILIAS_STRINGIFY(ILIAS_VERSION_PATCH)

// Formatter macro
#define ILIAS_FORMATTER(type)                              \
    template <>                                            \
    struct ILIAS_FMT_NAMESPACE::formatter<::ilias::type> : \
        ::ilias::detail::DefaultFormatter

ILIAS_NS_BEGIN

// Basic platform types
using fd_t     = ILIAS_FD_T;
using error_t  = ILIAS_ERROR_T;
using socket_t = ILIAS_SOCKET_T;

// Forward declaration
template <typename T>
class Task;

template <typename T>
class Fiber;

template <typename T>
class Generator;

// Common Concepts
template <typename T>
concept IntoString = requires (const T &t) {
    { toString(t) } -> std::convertible_to<std::string_view>;
};

// Formatting namespace
#if defined(ILIAS_FMT_NAMESPACE)
namespace fmtlib = ILIAS_FMT_NAMESPACE;

// Formatter with default parse and redirect some formatting function to the fmtlib namespace
namespace detail {

// The Helper class for formatting (compatible with fmtlib::formatter and std::formatter)
struct DefaultFormatter {
    using format_parse_context = fmtlib::format_parse_context;
    using format_context = fmtlib::format_context;

    constexpr auto parse(auto &ctxt) const noexcept {
        return ctxt.begin();
    }

    // Redirect the format_to
    template <typename It, typename ...Args>
    static auto format_to(It &&it, fmtlib::format_string<Args...> fmt, Args &&...args) {
        return fmtlib::format_to(it, fmt, std::forward<Args>(args)...);
    }

    // Redirect the format
    template <typename ...Args>
    static auto format(fmtlib::format_string<Args...> fmt, Args &&...args) {
        return fmtlib::format(fmt, std::forward<Args>(args)...);
    }
};

} // namespace detail
#endif // ILIAS_FMT_NAMESPACE

// LCOV_EXCL_START
// Namespace handling the assertion
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

#if defined(__cpp_lib_stacktrace)
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
#if defined(ILIAS_FMT_NAMESPACE)
template <typename ...Args>
[[noreturn]]
inline auto handler(std::string_view cond, std::source_location where, fmtlib::format_string<Args...> fmt, Args &&...args) {
    handlerImpl(cond, where, fmtlib::format(fmt, std::forward<Args>(args)...));
}
#else
template <typename ...Args>
[[noreturn]]
inline auto handler(std::string_view cond, std::source_location where, Args &&...) {
    handlerImpl(cond, where);
}
#endif // ILIAS_FMT_NAMESPACE

} // namespace assertion
// LCOV_EXCL_STOP

// Utils
template <typename T> requires 
    requires(const T &t) { t.toString(); } // Make sure the t has the toString() method
inline auto toString(const T &t) {
    return t.toString();
}

template <typename Stream, IntoString T> requires 
    requires(Stream &stream) { stream << std::string_view{}; } // Make sure the stream can output string_view, like std::ostream
inline auto operator <<(Stream &stream, const T &t) -> decltype(auto) {
    return stream << toString(t);
}

ILIAS_NS_END

// Make formatter for all type with InfoString concept
#if !defined(ILIAS_NO_FORMAT)
template <ilias::IntoString T>
struct ilias::fmtlib::formatter<T> : ilias::detail::DefaultFormatter {
    auto format(const auto &value, auto &ctxt) const {
        return format_to(ctxt.out(), "{}", toString(value));
    }
};
#endif // ILIAS_NO_FORMAT