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
#if !defined(ILIAS_NAMESPACE)
    #define ILIAS_NAMESPACE ilias
#endif

#if !defined(ILIAS_ASSERT)
    #define ILIAS_ASSERT(x) assert(x)
    #include <cassert>
#endif

#if !defined(ILIAS_CHECK)
    #define ILIAS_CHECK(x) if (!(x)) { ILIAS_ASSERT(x); ::abort(); }
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
    #define ILIAS_ERROR_T  ::uint32_t
    #define ILIAS_SOCKET_T ::uintptr_t
    #define ILIAS_FD_T      void *
#elif defined(__linux__)
    #define ILIAS_ERROR_T   int
    #define ILIAS_SOCKET_T  int
    #define ILIAS_FD_T      int
#else
    #error "Unsupported platform"
#endif

// --- Utils macro
#define ILIAS_ASSERT_MSG(x, msg) ILIAS_ASSERT((x) && (msg))
#define ILIAS_CHECK_MSG(x, msg) ILIAS_CHECK((x) && (msg))
#define ILIAS_NS ILIAS_NAMESPACE
#define ILIAS_NS_BEGIN namespace ILIAS_NAMESPACE {
#define ILIAS_NS_END }

// --- Formatter macro
#define ILIAS_FORMATTER(type)                                      \
    template <>                                                    \
    struct ILIAS_FMT_NAMESPACE::formatter<ILIAS_NAMESPACE::type> : \
        ILIAS_NAMESPACE::detail::DefaultFormatter


ILIAS_NS_BEGIN

// --- Basic platform types
using fd_t     = ILIAS_FD_T;
using error_t  = ILIAS_ERROR_T;
using socket_t = ILIAS_SOCKET_T;

// --- Forward declaration for Task<T>
template <typename T = void>
class Task;

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