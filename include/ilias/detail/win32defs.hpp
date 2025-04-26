/**
 * @file win32defs.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The global common win32 definitions
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

#include <WinSock2.h> // It must be included before windows.h
#include <Windows.h>