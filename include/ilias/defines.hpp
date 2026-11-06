#pragma once

/**
  ___ _ _             ____              _   _                
 |_ _| (_) __ _ ___  |  _ \ _   _ _ __ | |_(_)_ __ ___   ___ 
  | || | |/ _` / __| | |_) | | | | '_ \| __| | '_ ` _ \ / _ \
  | || | | (_| \__ \ |  _ <| |_| | | | | |_| | | | | | |  __/
 |___|_|_|\__,_|___/ |_| \_\\__,_|_| |_|\__|_|_| |_| |_|\___|

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
#define _ILIAS_DEFINES_H_

// GCC PR C++/124584:
// Destructors of tuple-protocol structured bindings may be skipped
// when they cross a coroutine suspension point.
// See more info: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=124584
#if defined(__GNUC__) && !defined(__clang__)
    #if (__GNUC__ == 15 && __GNUC_MINOR__ <= 3) || (__GNUC__ == 16 && __GNUC_MINOR__ < 2)
        #warning "ilias: Your GCC version is affected by GCC PR C++/124584.           \
            Coroutine structured bindings may have incorrect object lifetime cleanup, \
            potentially causing deadlocks and undefined behavior.                     \
            Please upgrade to GCC 16.2+ or use GCC 14.x."
        #define ILIAS_GCC_PR124584_AFFECTED 
    #endif // __GNUC__ == 15 && __GNUC_MINOR__ <= 3) || (__GNUC__ == 16 && __GNUC_MINOR__ < 2)
#endif // __GNUC__

// Platform detection
#if   defined(_WIN32)
    #define ILIAS_DLL_EXPORT ILIAS_ATTRIBUTE(dllexport)
    #define ILIAS_DLL_IMPORT ILIAS_ATTRIBUTE(dllimport)
    #define ILIAS_ERROR_T    std::uint32_t
    #define ILIAS_SOCKET_T   std::uintptr_t
    #define ILIAS_FD_T       void *
#elif defined(__linux__)
    #define ILIAS_DLL_EXPORT ILIAS_ATTRIBUTE(visibility("default"))
    #define ILIAS_DLL_IMPORT // no-op
    #define ILIAS_ERROR_T    int
    #define ILIAS_SOCKET_T   int
    #define ILIAS_FD_T       int
#else
    #error "Unsupported platform"
#endif // _WIN32

// Compiler check
#if   defined(_MSC_VER)
    #define ILIAS_NO_UNIQUE_ADDRESS msvc::no_unique_address
    #define ILIAS_ATTRIBUTE(x)  __declspec(x)
    #define ILIAS_UNREACHABLE() __assume(0)
    #define ILIAS_TRAP()        __debugbreak()
#elif defined(__GNUC__)
    #define ILIAS_ATTRIBUTE(x)  __attribute__((x))
    #define ILIAS_UNREACHABLE() __builtin_unreachable()
    #define ILIAS_TRAP()        __builtin_trap()
#else
    #define ILIAS_ATTRIBUTE(x)  // no-op
    #define ILIAS_UNREACHABLE() ::abort()
    #define ILIAS_TRAP()        ::abort()
#endif // _MSC_VER

#if  !defined(ILIAS_NO_UNIQUE_ADDRESS)
    #define ILIAS_NO_UNIQUE_ADDRESS no_unique_address
#endif // ILIAS_NO_UNIQUE_ADDRESS

// Because clang-cl may define _MSC_VER, so we check it on here
#if  !defined(__GNUC__)
    #define ILIAS_GNUC(...) // no-op
#else
    #define ILIAS_GNUC(...) __VA_ARGS__
#endif // __GNUC__

// Library mode
#if   defined(ILIAS_STATIC)    // Static library, no-op
    #define ILIAS_API
#elif defined(ILIAS_DLL)       // Dynamic library
    #if defined(_ILIAS_SOURCE) 
        #define ILIAS_API ILIAS_DLL_EXPORT
    #else
        #define ILIAS_API ILIAS_DLL_IMPORT
    #endif // _ILIAS_SOURCE
#else
    #error "Library mode not specified"
#endif // ILIAS_STATIC

// Module: export public declarations explicitly. A wholly public header can
// place ILIAS_EXPORT before ILIAS_NS_BEGIN; mixed headers export only their API
// declarations or public regions. Keep module export separate from ILIAS_API.
#if   defined(ILIAS_MODULE)
    #define ILIAS_EXPORT_BEGIN export {
    #define ILIAS_EXPORT_END }
    #define ILIAS_EXPORT export
#else
    #define ILIAS_EXPORT_BEGIN
    #define ILIAS_EXPORT_END
    #define ILIAS_EXPORT
#endif // ILIAS_MODULE

// Exception check
#if !defined(__cpp_exceptions) || defined(ILIAS_NO_EXCEPTIONS)
    #define ILIAS_TRY_EXCEPTION if constexpr(true)
    #define ILIAS_THROW(...) ::abort()
    #define ILIAS_CATCH(...) if constexpr(false)
#else
    #define ILIAS_TRY_EXCEPTION try
    #define ILIAS_THROW(...) throw(__VA_ARGS__)
    #define ILIAS_CATCH(...) catch(__VA_ARGS__)
#endif // __cpp_exceptions || ILIAS_NO_EXCEPTIONS

// Utils macro
#define ILIAS_NS_BEGIN namespace ilias {
#define ILIAS_NS_END }

// Assume macro
#define ILIAS_ASSUME(cond, ...) do {        \
        ILIAS_ASSERT(cond, ##__VA_ARGS__);  \
        if (!(cond)) {                      \
            ILIAS_UNREACHABLE();            \
        }                                   \
    } while (false)

// Import subpart
#include <ilias/detail/format.hpp>
#include <ilias/detail/assert.hpp>

// Import std headers
#if !defined(ILIAS_MODULE)
#include <cstdint>
#endif // ILIAS_MODULE

ILIAS_EXPORT
ILIAS_NS_BEGIN

// Basic platform types
using fd_t     = ILIAS_FD_T;
using error_t  = ILIAS_ERROR_T;
using socket_t = ILIAS_SOCKET_T;

// Forward declaration
template <typename T = void>
class Task;

template <typename T = void>
class Fiber;

template <typename T>
class Generator;

ILIAS_NS_END
