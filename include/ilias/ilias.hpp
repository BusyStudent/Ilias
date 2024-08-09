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

// --- config
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

// --- cpp version check 
#if  defined(__cpp_lib_format)
    #include <format>
#endif

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


ILIAS_NS_BEGIN

// --- Basic platform types
using fd_t     = ILIAS_FD_T;
using error_t  = ILIAS_ERROR_T;
using socket_t = ILIAS_SOCKET_T;

// --- Forward declaration for Task<T>
template <typename T = void>
class Task;

ILIAS_NS_END