/**
 * @file win32defs.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The global common win32 definitions & some win32 utils
 * @version 0.1
 * @date 2025-04-26
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#pragma once

#if !defined(NOMINMAX)
    #define NOMINMAX 1
#endif // defined(NOMINMAX)

#if !defined(WIN32_LEAN_AND_MEAN)
    #define WIN32_LEAN_AND_MEAN 1
#endif // defined(WIN32_LEAN_AND_MEAN)

#include <ilias/defines.hpp>
#include <WinSock2.h> // It must be included before windows.h
#include <Windows.h>

ILIAS_NS_BEGIN

namespace win32 {

struct NtDll; // Internal use struct

extern auto ILIAS_API toWide(std::string_view str) -> std::wstring;
extern auto ILIAS_API toUtf8(std::wstring_view str) -> std::string;

extern auto ILIAS_API pipe(HANDLE *read, HANDLE *write, SECURITY_ATTRIBUTES *attr = nullptr) -> bool;

extern auto ILIAS_API setThreadName(HANDLE thread, std::string_view name) -> bool;
extern auto ILIAS_API threadName(HANDLE thread) -> std::string;

} // namespace win32

ILIAS_NS_END