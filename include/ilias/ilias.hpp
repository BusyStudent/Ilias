#pragma once

/**
 * @file ilias.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief for defined some macros and basic platform detection
 * @version 0.1
 * @date 2024-07-18
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#include <concepts>
#include <cstdint>
#include <string>
#include <array>

// --- Config
#if __has_include(<ilias/detail/config.hpp>)
    #include <ilias/detail/config.hpp>
#endif

#if !defined(ILIAS_NAMESPACE)
    #define ILIAS_NAMESPACE ilias
#endif

#if !defined(ILIAS_ASSERT)
    #define ILIAS_ASSERT(x) assert(x)
    #include <cassert>
#endif

#if !defined(ILIAS_CHECK)
    #define ILIAS_CHECK(x) if (!(x)) { ILIAS_ASSERT(x); ::abort(); } ILIAS_ASSUME(x)
#endif

#if !defined(ILIAS_MALLOC)
    #define ILIAS_REALLOC(x, y) ::realloc(x, y)
    #define ILIAS_MALLOC(x) ::malloc(x)
    #define ILIAS_FREE(x) ::free(x)
    #include <cstdlib>
#endif

// --- Format library check 
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

// --- Exception check
#if !defined(__cpp_exceptions)
    #define ILIAS_THROW(x) ::abort()
#else
    #define ILIAS_THROW(x) throw x
#endif

// --- Platform detection
#if   defined(_WIN32)
    #define ILIAS_EXPORT   ILIAS_ATTRIBUTE(dllexport)
    #define ILIAS_IMPORT   ILIAS_ATTRIBUTE(dllimport)
    #define ILIAS_ERROR_T  ::uint32_t
    #define ILIAS_SOCKET_T ::uintptr_t
    #define ILIAS_FD_T      void *
#elif defined(__linux__)
    #define ILIAS_EXPORT   // no-op
    #define ILIAS_IMPORT   // no-op
    #define ILIAS_ERROR_T   int
    #define ILIAS_SOCKET_T  int
    #define ILIAS_FD_T      int
#else
    #error "Unsupported platform"
#endif

// --- Compiler check
#if   defined(_MSC_VER)
    #define ILIAS_ATTRIBUTE(x)  __declspec(x)
    #define ILIAS_UNREACHABLE() __assume(0)
    #define ILIAS_ASSUME(...)   __assume(__VA_ARGS__)
#elif defined(__GNUC__)
    #define ILIAS_ATTRIBUTE(x) __attribute__((x))
    #define ILIAS_UNREACHABLE() __builtin_unreachable()
    #define ILIAS_ASSUME(...) if (!(__VA_ARGS__)) __builtin_unreachable()
#else
    #define ILIAS_ATTRIBUTE(x) // no-op
    #define ILIAS_UNREACHABLE() // no-op
    #define ILIAS_ASSUME(...) // no-op
#endif

// --- Standard library check
#if defined(__cpp_lib_unreachable)
    #undef ILIAS_UNREACHABLE
    #define ILIAS_UNREACHABLE() std::unreachable()
    #include <utility>
#endif

#if defined(__cpp_lib_assume)
    #undef ILIAS_ASSUME
    #define ILIAS_ASSUME(...) [[assume(__VA_ARGS__)]]
#endif

// --- Library mode
// TODO:


// --- Version macro
#define ILIAS_VERSION_MAJOR 0
#define ILIAS_VERSION_MINOR 2
#define ILIAS_VERSION_PATCH 1
#define ILIAS_VERSION_AT_LEAST(major, minor, patch)                  \
    (ILIAS_VERSION_MAJOR > major ||                                  \
    (ILIAS_VERSION_MAJOR == major && ILIAS_VERSION_MINOR > minor) || \
    (ILIAS_VERSION_MAJOR == major && ILIAS_VERSION_MINOR == minor && ILIAS_VERSION_PATCH >= patch))
#define ILIAS_VERSION_STRING                                         \
    ILIAS_STRINGIFY(ILIAS_VERSION_MAJOR) "."                         \
    ILIAS_STRINGIFY(ILIAS_VERSION_MINOR) "."                         \
    ILIAS_STRINGIFY(ILIAS_VERSION_PATCH)

// --- Utils macro
#define ILIAS_GLUE_(x, y) x##y
#define ILIAS_GLUE(x, y) ILIAS_GLUE_(x, y)
#define ILIAS_STRINGIFY_(x) #x
#define ILIAS_STRINGIFY(x) ILIAS_STRINGIFY_(x)
#define ILIAS_ASSERT_MSG(x, msg) ILIAS_ASSERT((x) && (msg))
#define ILIAS_CHECK_MSG(x, msg) ILIAS_CHECK((x) && (msg))
#define ILIAS_NS_BEGIN namespace ILIAS_NAMESPACE {
#define ILIAS_NS_END }

// --- Formatter macro
#define ILIAS_FORMATTER(type)                                      \
    template <>                                                    \
    struct ILIAS_FMT_NAMESPACE::formatter<ILIAS_NAMESPACE::type> : \
        ILIAS_NAMESPACE::detail::DefaultFormatter

#define ILIAS_FORMATTER_T(T, type)                                 \
    template <T>                                                   \
    struct ILIAS_FMT_NAMESPACE::formatter<ILIAS_NAMESPACE::type> : \
        ILIAS_NAMESPACE::detail::DefaultFormatter


ILIAS_NS_BEGIN

// --- Basic platform types
using fd_t     = ILIAS_FD_T;
using error_t  = ILIAS_ERROR_T;
using socket_t = ILIAS_SOCKET_T;

// --- Forward declaration for Error
class Error;
class SystemError;

// --- Forward declaration for Task<T>
template <typename T = void>
class Task;

// --- Forward declaration for Generator<T>
template <typename T>
class Generator;

// --- Forward declaration for Result<T, E>
template <typename T = void, typename E = Error>
class Result;

// --- Types for io operation task
template <typename T = void, typename E = Error>
using IoTask = Task<Result<T, Error> >;

// --- Types for io operation generator
template <typename T>
using IoGenerator = Generator<Result<T> >;

// --- Formatting namespace
#if defined(ILIAS_FMT_NAMESPACE)
namespace fmtlib = ILIAS_FMT_NAMESPACE;

// --- Formatter with default parse and redirect some formatting function to the fmtlib namespace
namespace detail {

// --- The Helper class for formatting (compatible with fmtlib::formatter and std::formatter)
struct DefaultFormatter {
    using format_parse_context = fmtlib::format_parse_context;
    using format_context = fmtlib::format_context;

    constexpr auto parse(auto &ctxt) {
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

ILIAS_NS_END