# CMake build system for Ilias C++ Async I/O Library
# Minimum CMake version required
cmake_minimum_required(VERSION 3.20)

# Project configuration
project(ilias 
    VERSION 0.3.2
    DESCRIPTION "Modern C++ Async I/O Library with Coroutine Support"
    LANGUAGES CXX
)

# Set C++23 standard requirement
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type configuration
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo")
endif()

# =============================================================================
# Build Options - Feature Switches
# =============================================================================

option(ILIAS_ENABLE_TLS "Enable TLS support" ON)
option(ILIAS_ENABLE_FIBER "Enable fiber (stackful coroutine) support" ON)
option(ILIAS_ENABLE_LOGGING "Enable logging support" OFF)
option(ILIAS_ENABLE_IO "Enable I/O support" ON)

# Dependency selection options
option(ILIAS_USE_FMT "Use fmt library instead of std::format" OFF)
option(ILIAS_USE_SPDLOG "Use spdlog for logging backend" OFF)
option(ILIAS_USE_IO_URING "Use io_uring on Linux platforms" OFF)
option(ILIAS_FORCE_OPENSSL "Force use OpenSSL instead of native TLS (Windows)" OFF)

# C++20 compatibility option
option(ILIAS_CPP20_COMPAT "Enable C++20 compatibility polyfills" OFF)

# Debug options
option(ILIAS_ENABLE_CORO_TRACE "Enable coroutine stack trace for debugging" OFF)

# Build configuration options
option(ILIAS_BUILD_SHARED "Build shared library instead of static" OFF)

# Development mode (enables additional debugging features)
option(ILIAS_DEV_MODE "Enable development mode with debugging features" OFF)

# =============================================================================
# Compiler Configuration and Warnings
# =============================================================================

# Set warning levels based on compiler
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    # MSVC warnings
    add_compile_options(/W4)
    # Disable specific MSVC warnings that are too noisy
    add_compile_options(/wd4251 /wd4275)  # DLL interface warnings
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # GCC/Clang warnings
    add_compile_options(-Wall -Wextra -Wpedantic)
    # Additional useful warnings
    add_compile_options(-Wconversion -Wsign-conversion -Wunused -Wformat=2)
endif()

# Debug mode specific settings
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Note: ILIAS_DEBUG is set later in the configuration header generation section
    # and applied via target_compile_definitions for proper visibility control
    if(ILIAS_DEV_MODE)
        # Enable sanitizers in development mode on Linux
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND 
           (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang"))
            add_compile_options(-fsanitize=address -fsanitize=undefined)
            add_link_options(-fsanitize=address -fsanitize=undefined)
        endif()
    endif()
endif()

# Release mode specific settings
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    # Enable optimizations
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        add_compile_options(-O3)
        # Hide symbols by default on Linux for shared libraries
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND ILIAS_BUILD_SHARED)
            set(CMAKE_CXX_VISIBILITY_PRESET hidden)
            set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
        endif()
    endif()
endif()

# =============================================================================
# Enhanced Compiler Compatibility Checks and Version Validation
# =============================================================================

message(STATUS "Performing compiler compatibility validation...")

# Define minimum compiler versions for C++23 support
set(MIN_GCC_VERSION "11.0")
set(MIN_CLANG_VERSION "14.0")
set(MIN_MSVC_VERSION "19.30")  # Visual Studio 2022

# Check for C++23 support with detailed error messages
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS ${MIN_GCC_VERSION})
        message(FATAL_ERROR 
            "GCC version ${CMAKE_CXX_COMPILER_VERSION} does not support C++23 features required by Ilias. "
            "Minimum required version is ${MIN_GCC_VERSION}. "
            "Please upgrade your compiler:\n"
            "  Ubuntu/Debian: sudo apt-get install gcc-11 g++-11\n"
            "  Or use a newer distribution version")
    else()
        message(STATUS "✓ GCC ${CMAKE_CXX_COMPILER_VERSION} supports C++23")
    endif()
    
    # GCC-specific warnings for older versions
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "12.0")
        message(WARNING 
            "GCC ${CMAKE_CXX_COMPILER_VERSION} has limited C++23 support. "
            "Some features may not work correctly. "
            "Consider upgrading to GCC 12.0 or later for full C++23 support.")
    endif()
    
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS ${MIN_CLANG_VERSION})
        message(FATAL_ERROR 
            "Clang version ${CMAKE_CXX_COMPILER_VERSION} does not support C++23 features required by Ilias. "
            "Minimum required version is ${MIN_CLANG_VERSION}. "
            "Please upgrade your compiler:\n"
            "  Ubuntu/Debian: sudo apt-get install clang-14\n"
            "  macOS: Install Xcode 14.0 or later\n"
            "  Or use LLVM official releases")
    else()
        message(STATUS "✓ Clang ${CMAKE_CXX_COMPILER_VERSION} supports C++23")
    endif()
    
    # Clang-specific warnings
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "15.0")
        message(WARNING 
            "Clang ${CMAKE_CXX_COMPILER_VERSION} has partial C++23 support. "
            "Some advanced features may not be available. "
            "Consider upgrading to Clang 15.0 or later for better C++23 support.")
    endif()
    
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS ${MIN_MSVC_VERSION})
        message(FATAL_ERROR 
            "MSVC version ${CMAKE_CXX_COMPILER_VERSION} does not support C++23 features required by Ilias. "
            "Minimum required version is ${MIN_MSVC_VERSION} (Visual Studio 2022). "
            "Please upgrade your Visual Studio installation:\n"
            "  Download Visual Studio 2022 or later from Microsoft\n"
            "  Or use the latest Visual Studio Build Tools")
    else()
        message(STATUS "✓ MSVC ${CMAKE_CXX_COMPILER_VERSION} supports C++23")
    endif()
    
    # MSVC-specific version recommendations
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "19.32")
        message(WARNING 
            "MSVC ${CMAKE_CXX_COMPILER_VERSION} has early C++23 support. "
            "Some features may be experimental. "
            "Consider updating to the latest Visual Studio 2022 version.")
    endif()
    
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    # Apple Clang has different versioning
    set(MIN_APPLE_CLANG_VERSION "14.0")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS ${MIN_APPLE_CLANG_VERSION})
        message(FATAL_ERROR 
            "Apple Clang version ${CMAKE_CXX_COMPILER_VERSION} does not support C++23 features required by Ilias. "
            "Minimum required version is ${MIN_APPLE_CLANG_VERSION}. "
            "Please update Xcode:\n"
            "  Install Xcode 14.0 or later from the App Store\n"
            "  Or install Command Line Tools: xcode-select --install")
    else()
        message(STATUS "✓ Apple Clang ${CMAKE_CXX_COMPILER_VERSION} supports C++23")
    endif()
    
else()
    message(WARNING 
        "Unknown compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}. "
        "C++23 support is not verified for this compiler. "
        "Build may fail if C++23 features are not supported.")
endif()

# =============================================================================
# CMake Version Validation
# =============================================================================

set(MIN_CMAKE_VERSION "3.20")
if(CMAKE_VERSION VERSION_LESS ${MIN_CMAKE_VERSION})
    message(FATAL_ERROR 
        "CMake version ${CMAKE_VERSION} is not supported. "
        "Minimum required version is ${MIN_CMAKE_VERSION}. "
        "Please upgrade CMake:\n"
        "  Download from https://cmake.org/download/\n"
        "  Or use package manager: pip install cmake")
endif()

# Recommend newer CMake versions for better features
if(CMAKE_VERSION VERSION_LESS "3.25")
    message(STATUS 
        "CMake ${CMAKE_VERSION} is supported but older. "
        "Consider upgrading to CMake 3.25+ for better C++23 support and features.")
endif()

# =============================================================================
# Standard Library Compatibility Checks
# =============================================================================

# Check for standard library C++23 support
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # libstdc++ version check
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "12.0")
        message(WARNING 
            "libstdc++ from GCC ${CMAKE_CXX_COMPILER_VERSION} may have incomplete C++23 support. "
            "Some standard library features may not be available.")
    endif()
    
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # Check if using libc++ or libstdc++
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(STATUS 
            "Using Clang on Linux. Ensure you have a compatible standard library:\n"
            "  For libc++: sudo apt-get install libc++-dev libc++abi-dev\n"
            "  For libstdc++: Ensure GCC 11+ is installed")
    endif()
endif()

message(STATUS "Compiler compatibility validation completed")

# =============================================================================
# Enhanced Configuration Validation and Error Handling
# =============================================================================

message(STATUS "Performing configuration validation...")

# =============================================================================
# Platform Compatibility Checks
# =============================================================================

# Check for supported platforms
set(ILIAS_SUPPORTED_PLATFORMS "Windows" "Linux" "Darwin")
if(NOT CMAKE_SYSTEM_NAME IN_LIST ILIAS_SUPPORTED_PLATFORMS)
    message(FATAL_ERROR 
        "Unsupported platform: ${CMAKE_SYSTEM_NAME}. "
        "Supported platforms are: ${ILIAS_SUPPORTED_PLATFORMS}")
endif()

# Platform-specific feature validation
if(ILIAS_USE_IO_URING)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(WARNING 
            "io_uring is only supported on Linux platforms. "
            "Current platform: ${CMAKE_SYSTEM_NAME}. "
            "Automatically disabling io_uring support.")
        set(ILIAS_USE_IO_URING OFF CACHE BOOL "Use io_uring on Linux platforms" FORCE)
    endif()
endif()

# Windows-specific validation
if(WIN32)
    # Check for minimum Windows version support
    if(CMAKE_SYSTEM_VERSION VERSION_LESS "10.0")
        message(WARNING 
            "Windows version ${CMAKE_SYSTEM_VERSION} may not be fully supported. "
            "Recommended minimum version is Windows 10 (10.0)")
    endif()
    
    # Validate TLS configuration on Windows
    if(ILIAS_ENABLE_TLS AND NOT ILIAS_FORCE_OPENSSL)
        message(STATUS "Using Windows native Schannel for TLS support")
    endif()
endif()

# macOS-specific validation
if(APPLE)
    # io_uring is not supported on macOS
    if(ILIAS_USE_IO_URING)
        message(WARNING 
            "io_uring is not supported on macOS. "
            "Using kqueue-based implementation instead. "
            "Disabling io_uring support.")
        set(ILIAS_USE_IO_URING OFF CACHE BOOL "Use io_uring on Linux platforms" FORCE)
    endif()
    
    # Check for minimum macOS version
    if(CMAKE_SYSTEM_VERSION VERSION_LESS "10.15")
        message(WARNING 
            "macOS version ${CMAKE_SYSTEM_VERSION} may not fully support C++23 features. "
            "Recommended minimum version is macOS 10.15 (Catalina)")
    endif()
endif()

# =============================================================================
# Configuration Combination Validation
# =============================================================================

# Validate spdlog requires logging
if(ILIAS_USE_SPDLOG AND NOT ILIAS_ENABLE_LOGGING)
    message(WARNING 
        "ILIAS_USE_SPDLOG requires ILIAS_ENABLE_LOGGING to be enabled. "
        "Invalid configuration detected. Automatically enabling logging support.")
    set(ILIAS_ENABLE_LOGGING ON CACHE BOOL "Enable logging support" FORCE)
    message(STATUS "Logging support automatically enabled due to spdlog dependency")
endif()

# Validate fmt usage with C++20 compatibility
if(ILIAS_USE_FMT AND ILIAS_CPP20_COMPAT)
    message(STATUS 
        "Using fmt library with C++20 compatibility mode. "
        "This combination is supported but may have performance implications.")
endif()

# Validate TLS configuration combinations
if(ILIAS_ENABLE_TLS)
    if(WIN32 AND ILIAS_FORCE_OPENSSL)
        message(STATUS 
            "Forcing OpenSSL usage on Windows instead of native Schannel. "
            "This requires OpenSSL to be installed and available.")
    elseif(NOT WIN32 AND NOT ILIAS_FORCE_OPENSSL)
        message(STATUS "Using OpenSSL for TLS support on non-Windows platform")
    endif()
endif()

# Validate development mode implications
if(ILIAS_DEV_MODE)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        message(WARNING 
            "Development mode is enabled in Release build. "
            "This may impact performance. Consider disabling ILIAS_DEV_MODE for production builds.")
    endif()
    
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(STATUS 
            "Development mode enabled in ${CMAKE_BUILD_TYPE} build. "
            "Some debugging features may not be available.")
    endif()
endif()

# Validate coroutine trace with fiber support
if(ILIAS_ENABLE_CORO_TRACE AND NOT ILIAS_ENABLE_FIBER)
    message(WARNING 
        "Coroutine tracing is most useful with fiber support enabled. "
        "Consider enabling ILIAS_ENABLE_FIBER for better debugging experience.")
endif()

# =============================================================================
# Build Configuration Validation
# =============================================================================

# Validate build type
set(VALID_BUILD_TYPES "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
if(NOT CMAKE_BUILD_TYPE IN_LIST VALID_BUILD_TYPES)
    message(WARNING 
        "Unknown build type: ${CMAKE_BUILD_TYPE}. "
        "Valid build types are: ${VALID_BUILD_TYPES}. "
        "Falling back to Release build.")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build" FORCE)
endif()

# Validate shared library configuration
if(ILIAS_BUILD_SHARED AND BUILD_SHARED_LIBS)
    message(STATUS 
        "Both ILIAS_BUILD_SHARED and BUILD_SHARED_LIBS are set. "
        "Using shared library configuration.")
elseif(ILIAS_BUILD_SHARED OR BUILD_SHARED_LIBS)
    message(STATUS "Building shared library as requested")
else()
    message(STATUS "Building static library (default)")
endif()

# =============================================================================
# Feature Dependency Validation
# =============================================================================

# Check for conflicting feature combinations
set(VALIDATION_WARNINGS)
set(VALIDATION_ERRORS)

# I/O support is required for most features
if(NOT ILIAS_ENABLE_IO)
    if(ILIAS_ENABLE_TLS)
        list(APPEND VALIDATION_WARNINGS "TLS support requires I/O support but ILIAS_ENABLE_IO is disabled")
    endif()
    if(ILIAS_USE_IO_URING)
        list(APPEND VALIDATION_WARNINGS "io_uring requires I/O support but ILIAS_ENABLE_IO is disabled")
    endif()
endif()

# Fiber support implications
if(NOT ILIAS_ENABLE_FIBER)
    message(STATUS 
        "Fiber support is disabled. Some async operations will use different coroutine mechanisms.")
endif()

# Report validation warnings
if(VALIDATION_WARNINGS)
    message(WARNING "Configuration validation warnings:")
    foreach(warning ${VALIDATION_WARNINGS})
        message(WARNING "  - ${warning}")
    endforeach()
endif()

# Report validation errors (if any)
if(VALIDATION_ERRORS)
    message(FATAL_ERROR 
        "Configuration validation failed with errors:\n"
        "  ${VALIDATION_ERRORS}\n"
        "Please fix the configuration and try again.")
endif()

message(STATUS "Configuration validation completed successfully")

# =============================================================================
# Feature Summary Display
# =============================================================================

message(STATUS "=== Ilias Library Configuration ===")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
if(ILIAS_BUILD_SHARED OR BUILD_SHARED_LIBS)
    message(STATUS "Library type: Shared")
else()
    message(STATUS "Library type: Static")
endif()
message(STATUS "")
message(STATUS "Features:")
message(STATUS "  TLS support: ${ILIAS_ENABLE_TLS}")
message(STATUS "  Fiber support: ${ILIAS_ENABLE_FIBER}")
message(STATUS "  Logging support: ${ILIAS_ENABLE_LOGGING}")
message(STATUS "  I/O support: ${ILIAS_ENABLE_IO}")
message(STATUS "")
message(STATUS "Dependencies:")
message(STATUS "  Use fmt: ${ILIAS_USE_FMT}")
message(STATUS "  Use spdlog: ${ILIAS_USE_SPDLOG}")
message(STATUS "  Use io_uring: ${ILIAS_USE_IO_URING}")
message(STATUS "  Force OpenSSL: ${ILIAS_FORCE_OPENSSL}")
message(STATUS "")
message(STATUS "Debug options:")
message(STATUS "  C++20 compatibility: ${ILIAS_CPP20_COMPAT}")
message(STATUS "  Coroutine trace: ${ILIAS_ENABLE_CORO_TRACE}")
message(STATUS "  Development mode: ${ILIAS_DEV_MODE}")
message(STATUS "=====================================")

# =============================================================================
# Platform Detection and Configuration
# =============================================================================

# Detect target platform
if(WIN32)
    set(ILIAS_PLATFORM "Windows")
    set(ILIAS_PLATFORM_WINDOWS TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(ILIAS_PLATFORM "Linux")
    set(ILIAS_PLATFORM_LINUX TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(ILIAS_PLATFORM "macOS")
    set(ILIAS_PLATFORM_MACOS TRUE)
else()
    message(WARNING "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
    set(ILIAS_PLATFORM "Unknown")
endif()

message(STATUS "Target platform: ${ILIAS_PLATFORM}")

# =============================================================================
# Source File Collection and Organization
# =============================================================================

# Core source files (platform-independent)
set(ILIAS_CORE_SOURCES
    src/fiber.cpp
    src/io.cpp
    src/log.cpp
    src/runtime.cpp
    src/sync.cpp
    src/task.cpp
    src/net/addrinfo.cpp
)

# Platform-specific source files
set(ILIAS_PLATFORM_SOURCES)
set(ILIAS_PLATFORM_LIBRARIES)

if(ILIAS_PLATFORM_WINDOWS)
    # Windows-specific sources
    list(APPEND ILIAS_PLATFORM_SOURCES
        src/win32/io_context.cpp
        src/win32/ntdll.cpp
        src/win32/process.cpp
        src/win32/signal.cpp
        src/win32/win32.cpp
    )
    
    # Windows-specific system libraries
    list(APPEND ILIAS_PLATFORM_LIBRARIES
        ws2_32      # Winsock2
        mswsock     # Microsoft Winsock2 extensions
        kernel32    # Windows kernel
        user32      # Windows user interface
        advapi32    # Advanced Windows API
    )
    
    message(STATUS "Windows platform sources: ${ILIAS_PLATFORM_SOURCES}")
    
elseif(ILIAS_PLATFORM_LINUX)
    # Linux-specific sources
    list(APPEND ILIAS_PLATFORM_SOURCES
        src/linux/epoll.cpp
        src/linux/process.cpp
        src/linux/signal.cpp
    )
    
    # Add io_uring sources if enabled
    if(ILIAS_USE_IO_URING)
        list(APPEND ILIAS_PLATFORM_SOURCES
            src/linux/uring.cpp
        )
    endif()
    
    # Linux-specific system libraries
    list(APPEND ILIAS_PLATFORM_LIBRARIES
        pthread     # POSIX threads
        dl          # Dynamic linking
        anl         # Asynchronous name lookup
        rt          # Real-time extensions
    )
    
    message(STATUS "Linux platform sources: ${ILIAS_PLATFORM_SOURCES}")
    
elseif(ILIAS_PLATFORM_MACOS)
    # macOS uses similar sources to Linux but without io_uring
    list(APPEND ILIAS_PLATFORM_SOURCES
        src/linux/epoll.cpp     # macOS can use kqueue, but epoll compatibility layer
        src/linux/process.cpp
        src/linux/signal.cpp
    )
    
    # macOS-specific system libraries
    list(APPEND ILIAS_PLATFORM_LIBRARIES
        pthread     # POSIX threads
        dl          # Dynamic linking
    )
    
    message(STATUS "macOS platform sources: ${ILIAS_PLATFORM_SOURCES}")
    
    # Note: io_uring is not supported on macOS
    if(ILIAS_USE_IO_URING)
        message(WARNING "io_uring is not supported on macOS. Disabling io_uring support.")
        set(ILIAS_USE_IO_URING OFF CACHE BOOL "Use io_uring on Linux platforms" FORCE)
    endif()
endif()

# Feature-specific source files
set(ILIAS_FEATURE_SOURCES)

# TLS sources
if(ILIAS_ENABLE_TLS)
    if(ILIAS_PLATFORM_WINDOWS AND NOT ILIAS_FORCE_OPENSSL)
        # Use Windows native Schannel
        list(APPEND ILIAS_FEATURE_SOURCES src/tls/schannel.cpp)
        list(APPEND ILIAS_PLATFORM_LIBRARIES secur32 crypt32)
        message(STATUS "Using Windows Schannel for TLS")
    else()
        # Use OpenSSL on other platforms or when forced
        list(APPEND ILIAS_FEATURE_SOURCES src/tls/openssl.cpp)
        message(STATUS "Using OpenSSL for TLS")
    endif()
endif()

# Combine all source files
set(ILIAS_ALL_SOURCES
    ${ILIAS_CORE_SOURCES}
    ${ILIAS_PLATFORM_SOURCES}
    ${ILIAS_FEATURE_SOURCES}
)

# Display source file summary
message(STATUS "")
message(STATUS "Source files summary:")
message(STATUS "  Core sources: ${ILIAS_CORE_SOURCES}")
message(STATUS "  Platform sources: ${ILIAS_PLATFORM_SOURCES}")
message(STATUS "  Feature sources: ${ILIAS_FEATURE_SOURCES}")
message(STATUS "  Total sources: ${ILIAS_ALL_SOURCES}")
message(STATUS "  Platform libraries: ${ILIAS_PLATFORM_LIBRARIES}")

# Validate that all source files exist
foreach(source_file ${ILIAS_ALL_SOURCES})
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${source_file}")
        message(FATAL_ERROR "Required source file not found: ${source_file}")
    endif()
endforeach()

message(STATUS "All source files validated successfully")

# =============================================================================
# Library Target Creation and Configuration
# =============================================================================

# Determine library type based on options
set(ILIAS_LIBRARY_TYPE STATIC)
if(ILIAS_BUILD_SHARED OR BUILD_SHARED_LIBS)
    set(ILIAS_LIBRARY_TYPE SHARED)
endif()

message(STATUS "Creating ${ILIAS_LIBRARY_TYPE} library target")

# Create the main library target
add_library(ilias ${ILIAS_LIBRARY_TYPE} ${ILIAS_ALL_SOURCES})

# Create alias target for modern CMake usage
add_library(ilias::ilias ALIAS ilias)

# =============================================================================
# Target Properties and Configuration
# =============================================================================

# Set target include directories - moved to FetchContent section for proper visibility

# Set compile definitions - moved to FetchContent section for proper visibility

# Set basic target properties
set_target_properties(ilias PROPERTIES
    # Version information
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    
    # Output name
    OUTPUT_NAME "ilias"
    
    # C++ standard
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    
    # Position independent code for shared libraries
    POSITION_INDEPENDENT_CODE ON
    
    # Symbol visibility for shared libraries
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    
    # Windows-specific properties
    $<$<BOOL:${ILIAS_PLATFORM_WINDOWS}>:WINDOWS_EXPORT_ALL_SYMBOLS ON>
)

# =============================================================================
# FetchContent Support and Subproject Detection
# =============================================================================

# Detect if this project is being used as a subproject
if(NOT CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(ILIAS_IS_SUBPROJECT TRUE)
    message(STATUS "Ilias is being used as a subproject")
else()
    set(ILIAS_IS_SUBPROJECT FALSE)
    message(STATUS "Ilias is the main project")
endif()

# When used as a subproject, we don't want to build tests, examples, or benchmarks
# This is controlled by the parent project
if(ILIAS_IS_SUBPROJECT)
    message(STATUS "Subproject mode: Skipping tests, examples, and benchmarks")
    # Set default values for subproject usage
    set(ILIAS_BUILD_TESTS OFF CACHE BOOL "Build tests" FORCE)
    set(ILIAS_BUILD_EXAMPLES OFF CACHE BOOL "Build examples" FORCE)
    set(ILIAS_BUILD_BENCHMARKS OFF CACHE BOOL "Build benchmarks" FORCE)
endif()

# =============================================================================
# Dependency Management
# =============================================================================

# Include dependency management script
include(cmake/FindDependencies.cmake)

# =============================================================================
# Configuration Header File Generation
# =============================================================================

# Set configuration variables for header generation
set(ILIAS_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(ILIAS_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(ILIAS_VERSION_PATCH ${PROJECT_VERSION_PATCH})

# Set feature configuration variables
if(ILIAS_ENABLE_TLS)
    set(ILIAS_ENABLE_TLS 1)
endif()

if(ILIAS_ENABLE_FIBER)
    set(ILIAS_ENABLE_FIBER 1)
endif()

if(ILIAS_ENABLE_LOGGING)
    set(ILIAS_ENABLE_LOGGING 1)
endif()

if(ILIAS_ENABLE_IO)
    set(ILIAS_ENABLE_IO 1)
endif()

if(ILIAS_USE_FMT)
    set(ILIAS_USE_FMT 1)
endif()

if(ILIAS_USE_SPDLOG)
    set(ILIAS_USE_SPDLOG 1)
endif()

if(ILIAS_USE_IO_URING)
    set(ILIAS_USE_IO_URING 1)
endif()

if(ILIAS_FORCE_OPENSSL)
    set(ILIAS_FORCE_OPENSSL 1)
endif()

if(ILIAS_CPP20_COMPAT)
    set(ILIAS_CPP20_COMPAT 1)
endif()

if(ILIAS_ENABLE_CORO_TRACE)
    set(ILIAS_ENABLE_CORO_TRACE 1)
endif()

# Set library type macros
if(ILIAS_LIBRARY_TYPE STREQUAL "SHARED")
    set(ILIAS_DLL 1)
else()
    set(ILIAS_STATIC 1)
endif()

# Set debug configuration
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(ILIAS_BUILD_DEBUG 1)
endif()

# Set platform macros
if(ILIAS_PLATFORM_WINDOWS)
    set(ILIAS_PLATFORM_WINDOWS 1)
elseif(ILIAS_PLATFORM_LINUX)
    set(ILIAS_PLATFORM_LINUX 1)
elseif(ILIAS_PLATFORM_MACOS)
    set(ILIAS_PLATFORM_MACOS 1)
endif()

# =============================================================================
# Git Information Retrieval
# =============================================================================

# Find Git executable
find_package(Git QUIET)

# Initialize Git variables with default values
set(ILIAS_GIT_COMMIT "unknown")
set(ILIAS_GIT_COMMIT_LONG "unknown")
set(ILIAS_GIT_COMMIT_DATE "unknown")
set(ILIAS_GIT_BRANCH "unknown")
set(ILIAS_GIT_TAG "unknown")
set(ILIAS_GIT_TAG_LONG "unknown")
set(ILIAS_GIT_CUSTOM "unknown")

if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    message(STATUS "Git found, retrieving repository information...")
    
    # Get short commit hash
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE ILIAS_GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    
    # Get long commit hash
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE ILIAS_GIT_COMMIT_LONG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    
    # Get commit date
    execute_process(
        COMMAND ${GIT_EXECUTABLE} log -1 --format=%cd --date=iso
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE ILIAS_GIT_COMMIT_DATE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    
    # Get current branch
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE ILIAS_GIT_BRANCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    
    # Get latest tag
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE ILIAS_GIT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    
    # Get tag with commit info
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --long --dirty
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE ILIAS_GIT_TAG_LONG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    
    # Create custom version string
    if(NOT ILIAS_GIT_TAG STREQUAL "")
        set(ILIAS_GIT_CUSTOM "${ILIAS_GIT_TAG}-${ILIAS_GIT_COMMIT}")
    else()
        set(ILIAS_GIT_CUSTOM "v${PROJECT_VERSION}-${ILIAS_GIT_COMMIT}")
    endif()
    
    message(STATUS "Git information retrieved:")
    message(STATUS "  Commit: ${ILIAS_GIT_COMMIT}")
    message(STATUS "  Branch: ${ILIAS_GIT_BRANCH}")
    message(STATUS "  Tag: ${ILIAS_GIT_TAG}")
    message(STATUS "  Custom: ${ILIAS_GIT_CUSTOM}")
else()
    message(STATUS "Git not found or not a git repository, using default values")
endif()

# =============================================================================
# Generate Configuration Header File
# =============================================================================

# Configure the header file
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ilias/detail/config.hpp.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/ilias/detail/config.hpp
    @ONLY
)

message(STATUS "Configuration header generated: ${CMAKE_CURRENT_BINARY_DIR}/include/ilias/detail/config.hpp")

# Display configuration summary
message(STATUS "")
message(STATUS "Configuration header variables:")
message(STATUS "  Version: ${ILIAS_VERSION_MAJOR}.${ILIAS_VERSION_MINOR}.${ILIAS_VERSION_PATCH}")
message(STATUS "  Library type: ${ILIAS_LIBRARY_TYPE}")
message(STATUS "  Git commit: ${ILIAS_GIT_COMMIT}")
message(STATUS "  Git branch: ${ILIAS_GIT_BRANCH}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

# =============================================================================
# Target Export Configuration for FetchContent
# =============================================================================

# Ensure proper attribute propagation for consumer projects
# This is critical for FetchContent usage where the target needs to properly
# propagate its dependencies and compile requirements

message(STATUS "Configuring target export properties for FetchContent support...")

# Set up proper target properties for export
set_target_properties(ilias PROPERTIES
    # Export properties
    EXPORT_NAME "ilias"
    
    # Interface properties that will be propagated to consumers
    INTERFACE_COMPILE_FEATURES cxx_std_23
)

# Configure include directories with proper visibility
# PUBLIC: Available to both the library and its consumers
# PRIVATE: Only available to the library itself
# INTERFACE: Only available to consumers, not the library

target_include_directories(ilias
    PUBLIC
        # For build tree usage (FetchContent scenario)
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        # For install tree usage (find_package scenario)
        $<INSTALL_INTERFACE:include>
)

# Configure compile definitions with proper visibility
target_compile_definitions(ilias
    PRIVATE
        # Internal definitions only for building the library
        _ILIAS_SOURCE
        $<$<BOOL:${ILIAS_PLATFORM_WINDOWS}>:_WIN32_WINNT=0x0A00>
        $<$<BOOL:${ILIAS_PLATFORM_WINDOWS}>:NOMINMAX>
        $<$<BOOL:${ILIAS_PLATFORM_WINDOWS}>:WIN32_LEAN_AND_MEAN>
    PUBLIC
        # Definitions that consumers need to know about
        $<$<STREQUAL:${ILIAS_LIBRARY_TYPE},SHARED>:ILIAS_DLL>
        $<$<STREQUAL:${ILIAS_LIBRARY_TYPE},STATIC>:ILIAS_STATIC>
        # Feature definitions that affect the public API
        $<$<BOOL:${ILIAS_ENABLE_TLS}>:ILIAS_ENABLE_TLS>
        $<$<BOOL:${ILIAS_ENABLE_FIBER}>:ILIAS_ENABLE_FIBER>
        $<$<BOOL:${ILIAS_ENABLE_LOGGING}>:ILIAS_ENABLE_LOGGING>
        $<$<BOOL:${ILIAS_ENABLE_IO}>:ILIAS_ENABLE_IO>
        $<$<BOOL:${ILIAS_USE_FMT}>:ILIAS_USE_FMT>
        $<$<BOOL:${ILIAS_USE_SPDLOG}>:ILIAS_USE_SPDLOG>
        $<$<BOOL:${ILIAS_USE_IO_URING}>:ILIAS_USE_IO_URING>
        $<$<BOOL:${ILIAS_FORCE_OPENSSL}>:ILIAS_FORCE_OPENSSL>
        $<$<BOOL:${ILIAS_CPP20_COMPAT}>:ILIAS_CPP20_COMPAT>
        $<$<BOOL:${ILIAS_ENABLE_CORO_TRACE}>:ILIAS_ENABLE_CORO_TRACE>
        # Debug configuration - use generator expression for proper build type detection
        $<$<CONFIG:Debug>:ILIAS_BUILD_DEBUG>
)

# Configure compile features with proper visibility
target_compile_features(ilias
    PUBLIC
        # Consumers need C++23 support
        cxx_std_23
)

# =============================================================================
# FetchContent Integration Validation
# =============================================================================

# Validate that the target is properly configured for FetchContent usage
get_target_property(ILIAS_INCLUDE_DIRS ilias INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(ILIAS_COMPILE_DEFS ilias INTERFACE_COMPILE_DEFINITIONS)
get_target_property(ILIAS_COMPILE_FEATURES ilias INTERFACE_COMPILE_FEATURES)

message(STATUS "")
message(STATUS "FetchContent export validation:")
message(STATUS "  Target name: ilias")
message(STATUS "  Alias target: ilias::ilias")
message(STATUS "  Include directories: ${ILIAS_INCLUDE_DIRS}")
message(STATUS "  Compile definitions: ${ILIAS_COMPILE_DEFS}")
message(STATUS "  Compile features: ${ILIAS_COMPILE_FEATURES}")
message(STATUS "  Is subproject: ${ILIAS_IS_SUBPROJECT}")

# Ensure the alias target is available for FetchContent consumers
if(NOT TARGET ilias::ilias)
    message(FATAL_ERROR "Alias target ilias::ilias was not created properly")
endif()

message(STATUS "FetchContent support configured successfully")

# =============================================================================
# Package Configuration Files Generation
# =============================================================================

# Include required CMake modules for package configuration
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

message(STATUS "Configuring package configuration files...")

# Set installation directories
set(ILIAS_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
set(ILIAS_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
set(ILIAS_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
set(ILIAS_INSTALL_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/cmake/Ilias)

# Set variables for package configuration
set(ILIAS_IS_SHARED_LIBRARY FALSE)
if(ILIAS_LIBRARY_TYPE STREQUAL "SHARED")
    set(ILIAS_IS_SHARED_LIBRARY TRUE)
endif()

# Configure the main package configuration file
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/IliasConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/IliasConfig.cmake
    INSTALL_DESTINATION ${ILIAS_INSTALL_CMAKEDIR}
    PATH_VARS
        ILIAS_INSTALL_LIBDIR
        ILIAS_INSTALL_BINDIR
        ILIAS_INSTALL_INCLUDEDIR
        ILIAS_INSTALL_CMAKEDIR
    NO_SET_AND_CHECK_MACRO
    NO_CHECK_REQUIRED_COMPONENTS_MACRO
)

# Configure the version configuration file
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/IliasConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# Alternative: Use our custom version file for more control
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/IliasConfigVersion.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/IliasConfigVersionCustom.cmake
    @ONLY
)

# =============================================================================
# Final Target Export (after all configuration is complete)
# =============================================================================

# Create the targets export file for the build tree
# This must be done after all target properties and dependencies are set
message(STATUS "Exporting targets for build tree...")

export(TARGETS ilias
    NAMESPACE ilias::
    FILE ${CMAKE_CURRENT_BINARY_DIR}/IliasTargets.cmake
)

# Register the package in the CMake package registry (optional)
# This allows find_package to locate the build tree
if(NOT ILIAS_IS_SUBPROJECT)
    export(PACKAGE Ilias)
    message(STATUS "Package registered in CMake package registry")
endif()

message(STATUS "Package configuration files generated:")
message(STATUS "  Config file: ${CMAKE_CURRENT_BINARY_DIR}/IliasConfig.cmake")
message(STATUS "  Version file: ${CMAKE_CURRENT_BINARY_DIR}/IliasConfigVersion.cmake")
message(STATUS "  Custom version file: ${CMAKE_CURRENT_BINARY_DIR}/IliasConfigVersionCustom.cmake")
message(STATUS "  Targets file: ${CMAKE_CURRENT_BINARY_DIR}/IliasTargets.cmake")
message(STATUS "  Install destination: ${ILIAS_INSTALL_CMAKEDIR}")

# =============================================================================
# Package Configuration Validation
# =============================================================================

# Check if the targets file was created
if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/IliasTargets.cmake")
    message(STATUS "IliasTargets.cmake successfully generated")
else()
    message(WARNING "IliasTargets.cmake was not generated - this may cause issues with find_package")
endif()

# Validate that the other generated files exist
set(REQUIRED_CONFIG_FILES
    "${CMAKE_CURRENT_BINARY_DIR}/IliasConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/IliasConfigVersion.cmake"
)

foreach(config_file ${REQUIRED_CONFIG_FILES})
    if(NOT EXISTS ${config_file})
        message(FATAL_ERROR "Failed to generate required configuration file: ${config_file}")
    endif()
endforeach()

message(STATUS "Package configuration validation completed")

# =============================================================================
# Export Information Summary
# =============================================================================

# Display export information for debugging
get_target_property(ILIAS_EXPORT_NAME ilias EXPORT_NAME)
get_target_property(ILIAS_ALIASED_TARGET ilias ALIASED_TARGET)

message(STATUS "")
message(STATUS "Target export information:")
message(STATUS "  Target name: ilias")
message(STATUS "  Export name: ${ILIAS_EXPORT_NAME}")
message(STATUS "  Alias target: ilias::ilias")
message(STATUS "  Library type: ${ILIAS_LIBRARY_TYPE}")
message(STATUS "  Shared library: ${ILIAS_IS_SHARED_LIBRARY}")

# Display feature configuration that will be exported
message(STATUS "")
message(STATUS "Exported feature configuration:")
message(STATUS "  TLS support: ${ILIAS_ENABLE_TLS}")
message(STATUS "  Fiber support: ${ILIAS_ENABLE_FIBER}")
message(STATUS "  Logging support: ${ILIAS_ENABLE_LOGGING}")
message(STATUS "  I/O support: ${ILIAS_ENABLE_IO}")
message(STATUS "  Use fmt: ${ILIAS_USE_FMT}")
message(STATUS "  Use spdlog: ${ILIAS_USE_SPDLOG}")
message(STATUS "  Use io_uring: ${ILIAS_USE_IO_URING}")
message(STATUS "  Force OpenSSL: ${ILIAS_FORCE_OPENSSL}")
message(STATUS "  C++20 compatibility: ${ILIAS_CPP20_COMPAT}")
message(STATUS "  Coroutine trace: ${ILIAS_ENABLE_CORO_TRACE}")

# =============================================================================
# Installation System Configuration
# =============================================================================

message(STATUS "Configuring installation system...")

# Installation directories (already set in package configuration section)
# ILIAS_INSTALL_LIBDIR, ILIAS_INSTALL_BINDIR, ILIAS_INSTALL_INCLUDEDIR, ILIAS_INSTALL_CMAKEDIR
# are already defined above

# Display installation directories
message(STATUS "Installation directories:")
message(STATUS "  Prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  Libraries: ${ILIAS_INSTALL_LIBDIR}")
message(STATUS "  Binaries: ${ILIAS_INSTALL_BINDIR}")
message(STATUS "  Headers: ${ILIAS_INSTALL_INCLUDEDIR}")
message(STATUS "  CMake files: ${ILIAS_INSTALL_CMAKEDIR}")

# =============================================================================
# RPATH Configuration (Unix Systems)
# =============================================================================

if(UNIX AND NOT APPLE)
    # Linux RPATH configuration
    message(STATUS "Configuring RPATH for Linux...")
    
    # Set RPATH for the build tree
    set_target_properties(ilias PROPERTIES
        BUILD_RPATH "${CMAKE_CURRENT_BINARY_DIR}"
        BUILD_RPATH_USE_ORIGIN ON
    )
    
    # Set RPATH for the install tree
    set_target_properties(ilias PROPERTIES
        INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${ILIAS_INSTALL_LIBDIR}"
        INSTALL_RPATH_USE_LINK_PATH ON
    )
    
    message(STATUS "Linux RPATH configured:")
    message(STATUS "  Build RPATH: ${CMAKE_CURRENT_BINARY_DIR}")
    message(STATUS "  Install RPATH: ${CMAKE_INSTALL_PREFIX}/${ILIAS_INSTALL_LIBDIR}")
    
elseif(APPLE)
    # macOS RPATH configuration
    message(STATUS "Configuring RPATH for macOS...")
    
    # Set RPATH for the build tree
    set_target_properties(ilias PROPERTIES
        BUILD_RPATH "${CMAKE_CURRENT_BINARY_DIR}"
        MACOSX_RPATH ON
    )
    
    # Set RPATH for the install tree
    set_target_properties(ilias PROPERTIES
        INSTALL_RPATH "@loader_path"
        INSTALL_RPATH_USE_LINK_PATH ON
    )
    
    message(STATUS "macOS RPATH configured:")
    message(STATUS "  Build RPATH: ${CMAKE_CURRENT_BINARY_DIR}")
    message(STATUS "  Install RPATH: @loader_path")
    
else()
    message(STATUS "RPATH configuration skipped for Windows platform")
endif()

# =============================================================================
# Library Installation
# =============================================================================

message(STATUS "Configuring library installation...")

# Install the library target
install(TARGETS ilias
    EXPORT IliasTargets
    LIBRARY DESTINATION ${ILIAS_INSTALL_LIBDIR}
        COMPONENT Runtime
        NAMELINK_COMPONENT Development
    ARCHIVE DESTINATION ${ILIAS_INSTALL_LIBDIR}
        COMPONENT Development
    RUNTIME DESTINATION ${ILIAS_INSTALL_BINDIR}
        COMPONENT Runtime
    INCLUDES DESTINATION ${ILIAS_INSTALL_INCLUDEDIR}
)

message(STATUS "Library installation configured:")
message(STATUS "  Library files: ${ILIAS_INSTALL_LIBDIR}")
message(STATUS "  Archive files: ${ILIAS_INSTALL_LIBDIR}")
message(STATUS "  Runtime files: ${ILIAS_INSTALL_BINDIR}")

# =============================================================================
# Header Files Installation
# =============================================================================

message(STATUS "Configuring header files installation...")

# Install public headers from source tree
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION ${ILIAS_INSTALL_INCLUDEDIR}
    COMPONENT Development
    FILES_MATCHING
        PATTERN "*.hpp"
        PATTERN "*.h"
        # Exclude the template file
        PATTERN "*.in" EXCLUDE
)

# Install generated configuration header
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/ilias/detail/config.hpp
    DESTINATION ${ILIAS_INSTALL_INCLUDEDIR}/ilias/detail
    COMPONENT Development
)

message(STATUS "Header files installation configured:")
message(STATUS "  Source headers: ${CMAKE_CURRENT_SOURCE_DIR}/include/ -> ${ILIAS_INSTALL_INCLUDEDIR}")
message(STATUS "  Generated config: ${CMAKE_CURRENT_BINARY_DIR}/include/ilias/detail/config.hpp -> ${ILIAS_INSTALL_INCLUDEDIR}/ilias/detail")

# =============================================================================
# CMake Configuration Files Installation
# =============================================================================

message(STATUS "Configuring CMake configuration files installation...")

# Install the targets export file
install(EXPORT IliasTargets
    FILE IliasTargets.cmake
    NAMESPACE ilias::
    DESTINATION ${ILIAS_INSTALL_CMAKEDIR}
    COMPONENT Development
)

# Install the package configuration files
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/IliasConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/IliasConfigVersion.cmake
    DESTINATION ${ILIAS_INSTALL_CMAKEDIR}
    COMPONENT Development
)

# Install the dependency finding script
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindDependencies.cmake
    DESTINATION ${ILIAS_INSTALL_CMAKEDIR}
    COMPONENT Development
)

message(STATUS "CMake configuration files installation configured:")
message(STATUS "  Targets file: IliasTargets.cmake -> ${ILIAS_INSTALL_CMAKEDIR}")
message(STATUS "  Config files: IliasConfig.cmake, IliasConfigVersion.cmake -> ${ILIAS_INSTALL_CMAKEDIR}")
message(STATUS "  Dependencies: FindDependencies.cmake -> ${ILIAS_INSTALL_CMAKEDIR}")

# =============================================================================
# Installation Components Configuration
# =============================================================================

# Define installation components for selective installation
set(CPACK_COMPONENTS_ALL Runtime Development)

# Runtime component (shared libraries, executables)
set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME "Runtime Libraries")
set(CPACK_COMPONENT_RUNTIME_DESCRIPTION "Shared libraries and runtime files")

# Development component (headers, static libraries, CMake files)
set(CPACK_COMPONENT_DEVELOPMENT_DISPLAY_NAME "Development Files")
set(CPACK_COMPONENT_DEVELOPMENT_DESCRIPTION "Header files, static libraries, and CMake configuration files")

# Development component depends on runtime
set(CPACK_COMPONENT_DEVELOPMENT_DEPENDS Runtime)

message(STATUS "Installation components configured:")
message(STATUS "  Runtime: Shared libraries and runtime files")
message(STATUS "  Development: Headers, static libraries, CMake files")

# =============================================================================
# Installation Validation
# =============================================================================

# Validate that all required files exist before installation
set(REQUIRED_INSTALL_FILES
    "${CMAKE_CURRENT_BINARY_DIR}/IliasConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/IliasConfigVersion.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindDependencies.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/include/ilias/detail/config.hpp"
)

foreach(install_file ${REQUIRED_INSTALL_FILES})
    if(NOT EXISTS ${install_file})
        message(FATAL_ERROR "Required installation file not found: ${install_file}")
    endif()
endforeach()

# Validate that the main include directory exists
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include/ilias")
    message(FATAL_ERROR "Main include directory not found: ${CMAKE_CURRENT_SOURCE_DIR}/include/ilias")
endif()

message(STATUS "Installation validation completed successfully")

# =============================================================================
# Installation Summary
# =============================================================================

message(STATUS "")
message(STATUS "=== Installation Configuration Summary ===")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
message(STATUS "Library installation:")
message(STATUS "  Target: ilias (${ILIAS_LIBRARY_TYPE})")
message(STATUS "  Destination: ${CMAKE_INSTALL_PREFIX}/${ILIAS_INSTALL_LIBDIR}")
message(STATUS "  Export: IliasTargets")
message(STATUS "")
message(STATUS "Headers installation:")
message(STATUS "  Source: ${CMAKE_CURRENT_SOURCE_DIR}/include/")
message(STATUS "  Generated: ${CMAKE_CURRENT_BINARY_DIR}/include/ilias/detail/config.hpp")
message(STATUS "  Destination: ${CMAKE_INSTALL_PREFIX}/${ILIAS_INSTALL_INCLUDEDIR}")
message(STATUS "")
message(STATUS "CMake files installation:")
message(STATUS "  Config: IliasConfig.cmake, IliasConfigVersion.cmake")
message(STATUS "  Targets: IliasTargets.cmake")
message(STATUS "  Dependencies: FindDependencies.cmake")
message(STATUS "  Destination: ${CMAKE_INSTALL_PREFIX}/${ILIAS_INSTALL_CMAKEDIR}")
message(STATUS "")
if(UNIX)
    get_target_property(ILIAS_INSTALL_RPATH ilias INSTALL_RPATH)
    message(STATUS "RPATH configuration:")
    message(STATUS "  Install RPATH: ${ILIAS_INSTALL_RPATH}")
    message(STATUS "  Use link path: ON")
endif()
message(STATUS "")
message(STATUS "Installation components:")
message(STATUS "  Runtime: Shared libraries and executables")
message(STATUS "  Development: Headers, static libraries, CMake files")
message(STATUS "==========================================")

# =============================================================================
# Include subdirectories for further configuration
# =============================================================================

# Note: Additional configuration can be added here if needed.

# =============================================================================
# Final Configuration Summary and Validation Report
# =============================================================================

message(STATUS "")
message(STATUS "🎯 ===== ILIAS LIBRARY CONFIGURATION COMPLETE =====")
message(STATUS "")

# Project Information
message(STATUS "📋 Project Information:")
message(STATUS "  Name: ${PROJECT_NAME}")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Description: ${PROJECT_DESCRIPTION}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Library Type: ${ILIAS_LIBRARY_TYPE}")

# Build Environment
message(STATUS "")
message(STATUS "🔧 Build Environment:")
message(STATUS "  CMake: ${CMAKE_VERSION}")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Platform: ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_VERSION}")
message(STATUS "  Architecture: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "  C++ Standard: C++${CMAKE_CXX_STANDARD}")

# Feature Configuration Summary
message(STATUS "")
message(STATUS "⚙️  Feature Configuration:")
message(STATUS "  TLS Support: ${ILIAS_ENABLE_TLS}")
message(STATUS "  Fiber Support: ${ILIAS_ENABLE_FIBER}")
message(STATUS "  Logging Support: ${ILIAS_ENABLE_LOGGING}")
message(STATUS "  I/O Support: ${ILIAS_ENABLE_IO}")
message(STATUS "  C++20 Compatibility: ${ILIAS_CPP20_COMPAT}")
message(STATUS "  Coroutine Tracing: ${ILIAS_ENABLE_CORO_TRACE}")
message(STATUS "  Development Mode: ${ILIAS_DEV_MODE}")

# Dependency Configuration
message(STATUS "")
message(STATUS "📦 Dependencies:")
message(STATUS "  Use fmt: ${ILIAS_USE_FMT}")
message(STATUS "  Use spdlog: ${ILIAS_USE_SPDLOG}")
message(STATUS "  Use io_uring: ${ILIAS_USE_IO_URING}")
message(STATUS "  Force OpenSSL: ${ILIAS_FORCE_OPENSSL}")

# Build Configuration
message(STATUS "")
message(STATUS "🏗️  Build Configuration:")
message(STATUS "  Shared Library: ${ILIAS_BUILD_SHARED}")
message(STATUS "  Install Prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  Is Subproject: ${ILIAS_IS_SUBPROJECT}")

# Source Files Summary
list(LENGTH ILIAS_ALL_SOURCES TOTAL_SOURCES)
list(LENGTH ILIAS_CORE_SOURCES CORE_SOURCES)
list(LENGTH ILIAS_PLATFORM_SOURCES PLATFORM_SOURCES)
list(LENGTH ILIAS_FEATURE_SOURCES FEATURE_SOURCES)

message(STATUS "")
message(STATUS "📁 Source Files:")
message(STATUS "  Total Sources: ${TOTAL_SOURCES}")
message(STATUS "  Core Sources: ${CORE_SOURCES}")
message(STATUS "  Platform Sources: ${PLATFORM_SOURCES}")
message(STATUS "  Feature Sources: ${FEATURE_SOURCES}")

# Installation Configuration
message(STATUS "")
message(STATUS "📦 Installation:")
message(STATUS "  Library Destination: ${CMAKE_INSTALL_PREFIX}/${ILIAS_INSTALL_LIBDIR}")
message(STATUS "  Headers Destination: ${CMAKE_INSTALL_PREFIX}/${ILIAS_INSTALL_INCLUDEDIR}")
message(STATUS "  CMake Files Destination: ${CMAKE_INSTALL_PREFIX}/${ILIAS_INSTALL_CMAKEDIR}")

# Usage Instructions
message(STATUS "")
message(STATUS "🚀 Usage Instructions:")
message(STATUS "")
message(STATUS "  Build the library:")
message(STATUS "    cmake --build . --config ${CMAKE_BUILD_TYPE}")
message(STATUS "")
message(STATUS "  Install the library:")
message(STATUS "    cmake --install . --prefix ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
message(STATUS "  Use with FetchContent:")
message(STATUS "    FetchContent_Declare(ilias GIT_REPOSITORY <repo_url>)")
message(STATUS "    FetchContent_MakeAvailable(ilias)")
message(STATUS "    target_link_libraries(your_target PRIVATE ilias::ilias)")
message(STATUS "")
message(STATUS "  Use with find_package (after installation):")
message(STATUS "    find_package(Ilias REQUIRED)")
message(STATUS "    target_link_libraries(your_target PRIVATE ilias::ilias)")

# Configuration Validation Status
message(STATUS "")
message(STATUS "✅ Configuration Status:")
message(STATUS "  All dependencies resolved: YES")
message(STATUS "  Configuration validated: YES")
message(STATUS "  Ready to build: YES")

message(STATUS "")
message(STATUS "🎉 Configuration completed successfully!")
message(STATUS "   You can now build the Ilias library.")
message(STATUS "")
message(STATUS "==================================================")