/**
 * @file tls.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Import all tls headers
 * @version 0.1
 * @date 2025-08-12
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#pragma once

#include <ilias/defines.hpp>

#if defined(ILIAS_TLS)
    #include <ilias/tls/tls_basic.hpp>
#else
    #define ILIAS_NO_TLS
#endif // ILIAS_TLS