# FMT
if(ILIAS_USE_FMT)
    find_package(fmt 8.0 REQUIRED)
endif()

# Spdlog
if(ILIAS_USE_SPDLOG)
    if(NOT ILIAS_USE_LOG)
        message(FATAL_ERROR "ILIAS_USE_SPDLOG requires ILIAS_USE_LOG to be ON")
    endif()
    find_package(spdlog 1.8 REQUIRED)
endif()

# OpenSSL
if(ILIAS_TLS)
    if(NOT WIN32) # We use schannel on Windows
        find_package(OpenSSL REQUIRED)
    endif()
endif()

# IO_uring
if(ILIAS_USE_IO_URING)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "io_uring is only supported on Linux")
    endif()
    
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBURING REQUIRED liburing>=2.0)
endif()
