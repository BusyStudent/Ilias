# FindDependencies.cmake
# Dependency management script for Ilias C++ Async I/O Library
# This script handles finding and configuring all optional dependencies

# =============================================================================
# Dependency Management for Ilias Library
# =============================================================================

message(STATUS "=== Dependency Management ===")

# =============================================================================
# Enhanced OpenSSL Dependency with Version Validation
# =============================================================================

if(ILIAS_ENABLE_TLS)
    # Use OpenSSL on non-Windows platforms or when explicitly forced
    if(NOT WIN32 OR ILIAS_FORCE_OPENSSL)
        message(STATUS "Looking for OpenSSL...")
        
        # Define minimum and recommended OpenSSL versions
        set(MIN_OPENSSL_VERSION "1.1.0")
        set(RECOMMENDED_OPENSSL_VERSION "1.1.1")
        
        # Find OpenSSL with minimum version requirement
        find_package(OpenSSL ${MIN_OPENSSL_VERSION} QUIET)
        
        if(OpenSSL_FOUND)
            message(STATUS "✓ Found OpenSSL: ${OPENSSL_VERSION}")
            message(STATUS "  OpenSSL include dir: ${OPENSSL_INCLUDE_DIR}")
            message(STATUS "  OpenSSL libraries: ${OPENSSL_LIBRARIES}")
            
            # Version compatibility warnings
            if(OPENSSL_VERSION VERSION_LESS ${RECOMMENDED_OPENSSL_VERSION})
                message(WARNING 
                    "OpenSSL ${OPENSSL_VERSION} is older than recommended version ${RECOMMENDED_OPENSSL_VERSION}. "
                    "Some TLS features may not be available or may have security limitations. "
                    "Consider upgrading OpenSSL for better security and performance.")
            endif()
            
            # Check for deprecated OpenSSL versions
            if(OPENSSL_VERSION VERSION_LESS "1.1.1")
                message(WARNING 
                    "OpenSSL ${OPENSSL_VERSION} is approaching end-of-life. "
                    "Please consider upgrading to OpenSSL 1.1.1 or later for continued security support.")
            endif()
            
            # Link OpenSSL to the target (PRIVATE - implementation detail)
            target_link_libraries(ilias PRIVATE OpenSSL::SSL OpenSSL::Crypto)
            target_compile_definitions(ilias PRIVATE ILIAS_USE_OPENSSL)
            
            # Set dependency found flag
            set(ILIAS_OPENSSL_FOUND TRUE)
            
        else()
            # Enhanced error message with platform-specific installation instructions
            set(OPENSSL_ERROR_MSG 
                "OpenSSL ${MIN_OPENSSL_VERSION}+ is required for TLS support but was not found.\n"
                "Please install OpenSSL development packages:\n")
            
            if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
                if(EXISTS "/etc/debian_version")
                    set(OPENSSL_ERROR_MSG "${OPENSSL_ERROR_MSG}  Ubuntu/Debian: sudo apt-get install libssl-dev\n")
                elseif(EXISTS "/etc/redhat-release")
                    set(OPENSSL_ERROR_MSG "${OPENSSL_ERROR_MSG}  CentOS/RHEL/Fedora: sudo dnf install openssl-devel\n")
                else()
                    set(OPENSSL_ERROR_MSG "${OPENSSL_ERROR_MSG}  Linux: Install openssl-dev or openssl-devel package\n")
                endif()
            elseif(APPLE)
                set(OPENSSL_ERROR_MSG "${OPENSSL_ERROR_MSG}  macOS: brew install openssl\n")
                set(OPENSSL_ERROR_MSG "${OPENSSL_ERROR_MSG}         Then set: export PKG_CONFIG_PATH=\"/usr/local/opt/openssl/lib/pkgconfig\"\n")
            endif()
            
            set(OPENSSL_ERROR_MSG "${OPENSSL_ERROR_MSG}Alternative solutions:\n")
            set(OPENSSL_ERROR_MSG "${OPENSSL_ERROR_MSG}  - Use vcpkg: vcpkg install openssl\n")
            set(OPENSSL_ERROR_MSG "${OPENSSL_ERROR_MSG}  - Use Conan: conan install openssl/1.1.1@\n")
            set(OPENSSL_ERROR_MSG "${OPENSSL_ERROR_MSG}  - Disable TLS support: -DILIAS_ENABLE_TLS=OFF")
            
            message(FATAL_ERROR ${OPENSSL_ERROR_MSG})
        endif()
        
    else()
        # Windows platform using native Schannel
        message(STATUS "✓ Using Windows Schannel for TLS (native)")
        set(ILIAS_SCHANNEL_FOUND TRUE)
        
        # Validate Windows version for Schannel support
        if(CMAKE_SYSTEM_VERSION VERSION_LESS "6.1")
            message(WARNING 
                "Windows version ${CMAKE_SYSTEM_VERSION} may have limited Schannel support. "
                "Windows 7 (6.1) or later is recommended for full TLS functionality.")
        endif()
    endif()
else()
    message(STATUS "TLS support disabled")
endif()

# =============================================================================
# Enhanced fmt Library Dependency with Version Validation
# =============================================================================

if(ILIAS_USE_FMT)
    message(STATUS "Looking for fmt library...")
    
    # Define minimum and recommended fmt versions
    set(MIN_FMT_VERSION "8.0")
    set(RECOMMENDED_FMT_VERSION "9.0")
    
    # Find fmt with minimum version requirement
    find_package(fmt ${MIN_FMT_VERSION} QUIET)
    
    if(fmt_FOUND)
        message(STATUS "✓ Found fmt: ${fmt_VERSION}")
        
        # Version compatibility checks
        if(fmt_VERSION VERSION_LESS ${RECOMMENDED_FMT_VERSION})
            message(WARNING 
                "fmt ${fmt_VERSION} is older than recommended version ${RECOMMENDED_FMT_VERSION}. "
                "Consider upgrading for better performance and features.")
        endif()
        
        # Check for very old versions
        if(fmt_VERSION VERSION_LESS "8.1")
            message(WARNING 
                "fmt ${fmt_VERSION} may have compatibility issues with some C++23 features. "
                "Upgrade to fmt 8.1+ is recommended.")
        endif()
        
        # Link fmt to the target (PUBLIC - affects public API)
        target_link_libraries(ilias PUBLIC fmt::fmt)
        # Note: ILIAS_USE_FMT definition is set in main CMakeLists.txt
        
        # Set dependency found flag
        set(ILIAS_FMT_FOUND TRUE)
        
    else()
        # Enhanced error message with installation instructions
        set(FMT_ERROR_MSG 
            "fmt library ${MIN_FMT_VERSION}+ is required but was not found.\n"
            "Please install fmt development packages:\n")
        
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            if(EXISTS "/etc/debian_version")
                set(FMT_ERROR_MSG "${FMT_ERROR_MSG}  Ubuntu/Debian: sudo apt-get install libfmt-dev\n")
            elseif(EXISTS "/etc/redhat-release")
                set(FMT_ERROR_MSG "${FMT_ERROR_MSG}  CentOS/RHEL/Fedora: sudo dnf install fmt-devel\n")
            else()
                set(FMT_ERROR_MSG "${FMT_ERROR_MSG}  Linux: Install fmt-dev or fmt-devel package\n")
            endif()
        elseif(APPLE)
            set(FMT_ERROR_MSG "${FMT_ERROR_MSG}  macOS: brew install fmt\n")
        elseif(WIN32)
            set(FMT_ERROR_MSG "${FMT_ERROR_MSG}  Windows: Install via vcpkg or build from source\n")
        endif()
        
        set(FMT_ERROR_MSG "${FMT_ERROR_MSG}Alternative solutions:\n")
        set(FMT_ERROR_MSG "${FMT_ERROR_MSG}  - Use vcpkg: vcpkg install fmt\n")
        set(FMT_ERROR_MSG "${FMT_ERROR_MSG}  - Use Conan: conan install fmt/[>=8.0]\n")
        set(FMT_ERROR_MSG "${FMT_ERROR_MSG}  - Build from source: https://github.com/fmtlib/fmt\n")
        set(FMT_ERROR_MSG "${FMT_ERROR_MSG}  - Use std::format instead: -DILIAS_USE_FMT=OFF")
        
        message(FATAL_ERROR ${FMT_ERROR_MSG})
    endif()
else()
    message(STATUS "✓ Using std::format (C++20 standard)")
    
    # Warn about std::format compiler support
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "13.0")
        message(WARNING 
            "GCC ${CMAKE_CXX_COMPILER_VERSION} has incomplete std::format support. "
            "Consider using fmt library (-DILIAS_USE_FMT=ON) or upgrading to GCC 13+.")
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "14.0")
        message(WARNING 
            "Clang ${CMAKE_CXX_COMPILER_VERSION} may have incomplete std::format support. "
            "Consider using fmt library (-DILIAS_USE_FMT=ON) or upgrading to Clang 14+.")
    endif()
endif()

# =============================================================================
# Enhanced spdlog Library Dependency with Version Validation
# =============================================================================

if(ILIAS_USE_SPDLOG AND ILIAS_ENABLE_LOGGING)
    message(STATUS "Looking for spdlog library...")
    
    # Define minimum and recommended spdlog versions
    set(MIN_SPDLOG_VERSION "1.8")
    set(RECOMMENDED_SPDLOG_VERSION "1.10")
    
    # Find spdlog with minimum version requirement
    find_package(spdlog ${MIN_SPDLOG_VERSION} QUIET)
    
    if(spdlog_FOUND)
        message(STATUS "✓ Found spdlog: ${spdlog_VERSION}")
        
        # Version compatibility checks
        if(spdlog_VERSION VERSION_LESS ${RECOMMENDED_SPDLOG_VERSION})
            message(WARNING 
                "spdlog ${spdlog_VERSION} is older than recommended version ${RECOMMENDED_SPDLOG_VERSION}. "
                "Consider upgrading for better performance and C++20 support.")
        endif()
        
        # Check for fmt compatibility
        if(ILIAS_USE_FMT AND fmt_FOUND)
            # Ensure spdlog and fmt versions are compatible
            if(spdlog_VERSION VERSION_LESS "1.9" AND fmt_VERSION VERSION_GREATER_EQUAL "9.0")
                message(WARNING 
                    "spdlog ${spdlog_VERSION} may not be compatible with fmt ${fmt_VERSION}. "
                    "Consider upgrading spdlog to 1.9+ or using a compatible fmt version.")
            endif()
        endif()
        
        # Link spdlog to the target (PUBLIC - affects public API)
        target_link_libraries(ilias PUBLIC spdlog::spdlog)
        # Note: ILIAS_USE_SPDLOG definition is set in main CMakeLists.txt
        
        # Set dependency found flag
        set(ILIAS_SPDLOG_FOUND TRUE)
        
    else()
        # Enhanced error message with installation instructions
        set(SPDLOG_ERROR_MSG 
            "spdlog library ${MIN_SPDLOG_VERSION}+ is required but was not found.\n"
            "Please install spdlog development packages:\n")
        
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            if(EXISTS "/etc/debian_version")
                set(SPDLOG_ERROR_MSG "${SPDLOG_ERROR_MSG}  Ubuntu/Debian: sudo apt-get install libspdlog-dev\n")
            elseif(EXISTS "/etc/redhat-release")
                set(SPDLOG_ERROR_MSG "${SPDLOG_ERROR_MSG}  CentOS/RHEL/Fedora: sudo dnf install spdlog-devel\n")
            else()
                set(SPDLOG_ERROR_MSG "${SPDLOG_ERROR_MSG}  Linux: Install spdlog-dev or spdlog-devel package\n")
            endif()
        elseif(APPLE)
            set(SPDLOG_ERROR_MSG "${SPDLOG_ERROR_MSG}  macOS: brew install spdlog\n")
        elseif(WIN32)
            set(SPDLOG_ERROR_MSG "${SPDLOG_ERROR_MSG}  Windows: Install via vcpkg or build from source\n")
        endif()
        
        set(SPDLOG_ERROR_MSG "${SPDLOG_ERROR_MSG}Alternative solutions:\n")
        set(SPDLOG_ERROR_MSG "${SPDLOG_ERROR_MSG}  - Use vcpkg: vcpkg install spdlog\n")
        set(SPDLOG_ERROR_MSG "${SPDLOG_ERROR_MSG}  - Use Conan: conan install spdlog/[>=1.8]\n")
        set(SPDLOG_ERROR_MSG "${SPDLOG_ERROR_MSG}  - Build from source: https://github.com/gabime/spdlog\n")
        set(SPDLOG_ERROR_MSG "${SPDLOG_ERROR_MSG}  - Use built-in logging: -DILIAS_USE_SPDLOG=OFF")
        
        message(FATAL_ERROR ${SPDLOG_ERROR_MSG})
    endif()
    
elseif(ILIAS_USE_SPDLOG AND NOT ILIAS_ENABLE_LOGGING)
    message(WARNING 
        "spdlog requested but logging is disabled. "
        "This is an invalid configuration. "
        "Enable logging with -DILIAS_ENABLE_LOGGING=ON or disable spdlog with -DILIAS_USE_SPDLOG=OFF")
else()
    if(ILIAS_ENABLE_LOGGING)
        message(STATUS "✓ Using built-in logging implementation")
    else()
        message(STATUS "Logging support disabled")
    endif()
endif()

# =============================================================================
# Enhanced liburing Dependency with Version Validation
# =============================================================================

if(ILIAS_USE_IO_URING)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(STATUS "Looking for liburing...")
        
        # Define minimum and recommended liburing versions
        set(MIN_LIBURING_VERSION "2.0")
        set(RECOMMENDED_LIBURING_VERSION "2.2")
        
        # Try to find liburing using PkgConfig first
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(LIBURING QUIET liburing>=${MIN_LIBURING_VERSION})
        endif()
        
        # If PkgConfig didn't find it, try find_library
        if(NOT LIBURING_FOUND)
            find_library(LIBURING_LIBRARY 
                NAMES uring
                HINTS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
            )
            find_path(LIBURING_INCLUDE_DIR 
                NAMES liburing.h
                HINTS /usr/include /usr/local/include
            )
            
            if(LIBURING_LIBRARY AND LIBURING_INCLUDE_DIR)
                set(LIBURING_FOUND TRUE)
                set(LIBURING_LIBRARIES ${LIBURING_LIBRARY})
                set(LIBURING_INCLUDE_DIRS ${LIBURING_INCLUDE_DIR})
                
                # Try to determine version from header
                if(EXISTS "${LIBURING_INCLUDE_DIR}/liburing.h")
                    file(READ "${LIBURING_INCLUDE_DIR}/liburing.h" LIBURING_HEADER_CONTENT)
                    string(REGEX MATCH "#define LIBURING_VERSION_MAJOR ([0-9]+)" _ ${LIBURING_HEADER_CONTENT})
                    set(LIBURING_VERSION_MAJOR ${CMAKE_MATCH_1})
                    string(REGEX MATCH "#define LIBURING_VERSION_MINOR ([0-9]+)" _ ${LIBURING_HEADER_CONTENT})
                    set(LIBURING_VERSION_MINOR ${CMAKE_MATCH_1})
                    if(LIBURING_VERSION_MAJOR AND LIBURING_VERSION_MINOR)
                        set(LIBURING_VERSION "${LIBURING_VERSION_MAJOR}.${LIBURING_VERSION_MINOR}")
                    else()
                        set(LIBURING_VERSION "unknown")
                    endif()
                endif()
                
                message(STATUS "Found liburing: ${LIBURING_LIBRARY}")
            endif()
        endif()
        
        if(LIBURING_FOUND)
            message(STATUS "✓ Found liburing: ${LIBURING_VERSION}")
            message(STATUS "  liburing include dirs: ${LIBURING_INCLUDE_DIRS}")
            message(STATUS "  liburing libraries: ${LIBURING_LIBRARIES}")
            
            # Version compatibility checks
            if(LIBURING_VERSION AND LIBURING_VERSION VERSION_LESS ${RECOMMENDED_LIBURING_VERSION})
                message(WARNING 
                    "liburing ${LIBURING_VERSION} is older than recommended version ${RECOMMENDED_LIBURING_VERSION}. "
                    "Some advanced io_uring features may not be available. "
                    "Consider upgrading for better performance and features.")
            endif()
            
            # Check for very old versions
            if(LIBURING_VERSION AND LIBURING_VERSION VERSION_LESS "2.1")
                message(WARNING 
                    "liburing ${LIBURING_VERSION} may have compatibility issues. "
                    "Upgrade to liburing 2.1+ is strongly recommended.")
            endif()
            
            # Link liburing to the target (PRIVATE - implementation detail)
            target_include_directories(ilias PRIVATE ${LIBURING_INCLUDE_DIRS})
            target_link_libraries(ilias PRIVATE ${LIBURING_LIBRARIES})
            target_compile_definitions(ilias PRIVATE ILIAS_USE_IO_URING)
            
            # Set dependency found flag
            set(ILIAS_LIBURING_FOUND TRUE)
            
        else()
            # Enhanced error message with installation instructions
            set(LIBURING_ERROR_MSG 
                "liburing ${MIN_LIBURING_VERSION}+ is required for io_uring support but was not found.\n"
                "Please install liburing development packages:\n")
            
            if(EXISTS "/etc/debian_version")
                set(LIBURING_ERROR_MSG "${LIBURING_ERROR_MSG}  Ubuntu/Debian: sudo apt-get install liburing-dev\n")
            elseif(EXISTS "/etc/redhat-release")
                set(LIBURING_ERROR_MSG "${LIBURING_ERROR_MSG}  CentOS/RHEL/Fedora: sudo dnf install liburing-devel\n")
            elseif(EXISTS "/etc/arch-release")
                set(LIBURING_ERROR_MSG "${LIBURING_ERROR_MSG}  Arch Linux: sudo pacman -S liburing\n")
            else()
                set(LIBURING_ERROR_MSG "${LIBURING_ERROR_MSG}  Linux: Install liburing-dev or liburing-devel package\n")
            endif()
            
            set(LIBURING_ERROR_MSG "${LIBURING_ERROR_MSG}Alternative solutions:\n")
            set(LIBURING_ERROR_MSG "${LIBURING_ERROR_MSG}  - Build from source: https://github.com/axboe/liburing\n")
            set(LIBURING_ERROR_MSG "${LIBURING_ERROR_MSG}  - Use epoll instead: -DILIAS_USE_IO_URING=OFF\n")
            set(LIBURING_ERROR_MSG "${LIBURING_ERROR_MSG}Note: io_uring requires Linux kernel 5.1+ for basic support, 5.6+ recommended")
            
            message(FATAL_ERROR ${LIBURING_ERROR_MSG})
        endif()
        
    else()
        message(WARNING 
            "io_uring is only supported on Linux platforms. "
            "Current platform: ${CMAKE_SYSTEM_NAME}. "
            "Automatically disabling io_uring support.")
        set(ILIAS_USE_IO_URING OFF CACHE BOOL "Use io_uring on Linux platforms" FORCE)
    endif()
else()
    message(STATUS "io_uring support disabled")
    
    # Inform about the I/O backend being used
    if(WIN32)
        message(STATUS "Using IOCP (I/O Completion Ports) for async I/O on Windows")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(STATUS "Using epoll for async I/O on Linux")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        message(STATUS "Using kqueue for async I/O on macOS")
    endif()
endif()

# =============================================================================
# Platform-specific System Libraries
# =============================================================================

message(STATUS "Configuring platform-specific system libraries...")

if(WIN32)
    # Windows system libraries
    set(ILIAS_SYSTEM_LIBRARIES
        ws2_32      # Winsock2
        mswsock     # Microsoft Winsock2 extensions
        kernel32    # Windows kernel
        user32      # Windows user interface
        advapi32    # Advanced Windows API
    )
    
    # Additional libraries for TLS on Windows (Schannel)
    if(ILIAS_ENABLE_TLS AND NOT ILIAS_FORCE_OPENSSL)
        list(APPEND ILIAS_SYSTEM_LIBRARIES
            secur32     # Security Support Provider Interface
            crypt32     # Cryptography API
        )
    endif()
    
    message(STATUS "Windows system libraries: ${ILIAS_SYSTEM_LIBRARIES}")
    
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Linux system libraries
    set(ILIAS_SYSTEM_LIBRARIES
        pthread     # POSIX threads
        dl          # Dynamic linking
        anl         # Asynchronous name lookup
        rt          # Real-time extensions
    )
    
    message(STATUS "Linux system libraries: ${ILIAS_SYSTEM_LIBRARIES}")
    
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # macOS system libraries
    set(ILIAS_SYSTEM_LIBRARIES
        pthread     # POSIX threads
        dl          # Dynamic linking
    )
    
    message(STATUS "macOS system libraries: ${ILIAS_SYSTEM_LIBRARIES}")
    
else()
    message(WARNING "Unknown platform: ${CMAKE_SYSTEM_NAME}")
    set(ILIAS_SYSTEM_LIBRARIES)
endif()

# Link system libraries to the target (PRIVATE - implementation details)
if(ILIAS_SYSTEM_LIBRARIES)
    target_link_libraries(ilias PRIVATE ${ILIAS_SYSTEM_LIBRARIES})
endif()

# =============================================================================
# Enhanced Dependency Summary with Status Indicators
# =============================================================================

message(STATUS "")
message(STATUS "=== Enhanced Dependency Summary ===")

# TLS Support Status
message(STATUS "TLS Support:")
if(ILIAS_ENABLE_TLS)
    if(ILIAS_OPENSSL_FOUND)
        message(STATUS "  ✓ OpenSSL ${OPENSSL_VERSION} (${OPENSSL_LIBRARIES})")
        if(OPENSSL_VERSION VERSION_LESS "1.1.1")
            message(STATUS "    ⚠ Version warning: Consider upgrading for better security")
        endif()
    elseif(ILIAS_SCHANNEL_FOUND)
        message(STATUS "  ✓ Windows Schannel (native)")
    else()
        message(STATUS "  ✗ No TLS implementation found - THIS IS AN ERROR")
    endif()
else()
    message(STATUS "  - Disabled")
endif()

# Formatting Support Status
message(STATUS "Formatting:")
if(ILIAS_USE_FMT AND ILIAS_FMT_FOUND)
    message(STATUS "  ✓ fmt ${fmt_VERSION}")
    if(fmt_VERSION VERSION_LESS "9.0")
        message(STATUS "    ⚠ Consider upgrading to fmt 9.0+ for better performance")
    endif()
elseif(ILIAS_USE_FMT)
    message(STATUS "  ✗ fmt requested but not found - THIS IS AN ERROR")
else()
    message(STATUS "  ✓ std::format (C++20)")
    # Check compiler support for std::format
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "13.0")
        message(STATUS "    ⚠ GCC ${CMAKE_CXX_COMPILER_VERSION} has incomplete std::format support")
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "14.0")
        message(STATUS "    ⚠ Clang ${CMAKE_CXX_COMPILER_VERSION} may have incomplete std::format support")
    endif()
endif()

# Logging Support Status
message(STATUS "Logging:")
if(ILIAS_ENABLE_LOGGING)
    if(ILIAS_USE_SPDLOG AND ILIAS_SPDLOG_FOUND)
        message(STATUS "  ✓ spdlog ${spdlog_VERSION}")
        if(spdlog_VERSION VERSION_LESS "1.10")
            message(STATUS "    ⚠ Consider upgrading to spdlog 1.10+ for better C++20 support")
        endif()
    elseif(ILIAS_USE_SPDLOG)
        message(STATUS "  ✗ spdlog requested but not found - THIS IS AN ERROR")
    else()
        message(STATUS "  ✓ Built-in implementation")
    endif()
else()
    message(STATUS "  - Disabled")
endif()

# I/O Backend Status
message(STATUS "I/O Backend:")
if(ILIAS_USE_IO_URING AND ILIAS_LIBURING_FOUND)
    message(STATUS "  ✓ io_uring (liburing ${LIBURING_VERSION})")
    if(LIBURING_VERSION AND LIBURING_VERSION VERSION_LESS "2.2")
        message(STATUS "    ⚠ Consider upgrading to liburing 2.2+ for better performance")
    endif()
elseif(ILIAS_USE_IO_URING)
    message(STATUS "  ✗ io_uring requested but liburing not found - THIS IS AN ERROR")
else()
    if(WIN32)
        message(STATUS "  ✓ IOCP (I/O Completion Ports) - Windows native")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(STATUS "  ✓ epoll - Linux native")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        message(STATUS "  ✓ kqueue - macOS native")
    else()
        message(STATUS "  ? Platform-specific backend")
    endif()
endif()

# System Libraries Status
message(STATUS "System Libraries:")
if(ILIAS_SYSTEM_LIBRARIES)
    message(STATUS "  ✓ ${ILIAS_SYSTEM_LIBRARIES}")
else()
    message(STATUS "  - None required")
endif()

# Compiler and Build Information
message(STATUS "Build Environment:")
message(STATUS "  ✓ Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  ✓ CMake: ${CMAKE_VERSION}")
message(STATUS "  ✓ Platform: ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_VERSION}")
message(STATUS "  ✓ Architecture: ${CMAKE_SYSTEM_PROCESSOR}")

message(STATUS "=============================")

# =============================================================================
# Enhanced Validation and Critical Error Checking
# =============================================================================

message(STATUS "Performing final dependency validation...")

# Collect all dependency errors
set(DEPENDENCY_ERRORS)
set(DEPENDENCY_WARNINGS)

# Critical dependency failures
if(ILIAS_ENABLE_TLS AND NOT ILIAS_OPENSSL_FOUND AND NOT ILIAS_SCHANNEL_FOUND)
    list(APPEND DEPENDENCY_ERRORS "TLS support enabled but no TLS implementation found")
endif()

if(ILIAS_USE_FMT AND NOT ILIAS_FMT_FOUND)
    list(APPEND DEPENDENCY_ERRORS "fmt library requested but not found")
endif()

if(ILIAS_USE_SPDLOG AND ILIAS_ENABLE_LOGGING AND NOT ILIAS_SPDLOG_FOUND)
    list(APPEND DEPENDENCY_ERRORS "spdlog library requested but not found")
endif()

if(ILIAS_USE_IO_URING AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ILIAS_LIBURING_FOUND)
    list(APPEND DEPENDENCY_ERRORS "liburing requested but not found")
endif()

# Configuration warnings
if(ILIAS_USE_SPDLOG AND NOT ILIAS_ENABLE_LOGGING)
    list(APPEND DEPENDENCY_WARNINGS "spdlog requested but logging is disabled")
endif()

if(ILIAS_USE_IO_URING AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND DEPENDENCY_WARNINGS "io_uring requested on non-Linux platform")
endif()

# Version compatibility warnings
if(ILIAS_OPENSSL_FOUND AND OPENSSL_VERSION VERSION_LESS "1.1.1")
    list(APPEND DEPENDENCY_WARNINGS "OpenSSL ${OPENSSL_VERSION} is approaching end-of-life")
endif()

if(ILIAS_FMT_FOUND AND fmt_VERSION VERSION_LESS "9.0")
    list(APPEND DEPENDENCY_WARNINGS "fmt ${fmt_VERSION} is older than recommended")
endif()

if(ILIAS_SPDLOG_FOUND AND spdlog_VERSION VERSION_LESS "1.10")
    list(APPEND DEPENDENCY_WARNINGS "spdlog ${spdlog_VERSION} is older than recommended")
endif()

if(ILIAS_LIBURING_FOUND AND LIBURING_VERSION AND LIBURING_VERSION VERSION_LESS "2.2")
    list(APPEND DEPENDENCY_WARNINGS "liburing ${LIBURING_VERSION} is older than recommended")
endif()

# Report dependency warnings
if(DEPENDENCY_WARNINGS)
    message(STATUS "")
    message(WARNING "Dependency validation warnings:")
    foreach(warning ${DEPENDENCY_WARNINGS})
        message(WARNING "  ⚠ ${warning}")
    endforeach()
    message(WARNING "These warnings do not prevent building but may affect functionality or performance.")
endif()

# Report critical dependency errors
if(DEPENDENCY_ERRORS)
    message(STATUS "")
    message(FATAL_ERROR 
        "❌ Dependency validation failed with critical errors:\n"
        "$(foreach error,${DEPENDENCY_ERRORS},  ✗ ${error}\n)"
        "\nPlease resolve these dependency issues and try again.\n"
        "See the error messages above for specific installation instructions.")
endif()

# Success message
message(STATUS "")
message(STATUS "✅ All dependencies resolved successfully")
message(STATUS "✅ Configuration is valid and ready for build")

# =============================================================================
# Build Recommendations and Optimization Hints
# =============================================================================

message(STATUS "")
message(STATUS "=== Build Recommendations ===")

# Performance recommendations
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT ILIAS_DEV_MODE)
    message(STATUS "💡 For development: Enable ILIAS_DEV_MODE for additional debugging features")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release" AND ILIAS_DEV_MODE)
    message(STATUS "💡 For production: Disable ILIAS_DEV_MODE for better performance")
endif()

# Feature recommendations
if(NOT ILIAS_USE_IO_URING AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "💡 For better Linux performance: Consider enabling io_uring with -DILIAS_USE_IO_URING=ON")
endif()

if(NOT ILIAS_USE_FMT AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "13.0")
    message(STATUS "💡 For better formatting support: Consider using fmt with -DILIAS_USE_FMT=ON")
endif()

if(ILIAS_ENABLE_LOGGING AND NOT ILIAS_USE_SPDLOG)
    message(STATUS "💡 For advanced logging: Consider using spdlog with -DILIAS_USE_SPDLOG=ON")
endif()

# Security recommendations
if(ILIAS_OPENSSL_FOUND AND OPENSSL_VERSION VERSION_LESS "1.1.1")
    message(STATUS "🔒 Security: Upgrade OpenSSL to 1.1.1+ for continued security support")
endif()

message(STATUS "===============================")