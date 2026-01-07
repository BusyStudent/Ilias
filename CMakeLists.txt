cmake_minimum_required(VERSION 3.25)

project(ilias 
    VERSION 0.3.3
    DESCRIPTION "Modern C++ Async I/O Library"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The basic configuration
option(ILIAS_CORO_TRACE     "Enable Coroutine Trace" OFF)
option(ILIAS_CPP20          "Enable C++20 features/polyfill" OFF)
option(ILIAS_TLS            "Enable TLS support" ON)
option(ILIAS_USE_LOG        "Enable Logging" OFF)
option(ILIAS_USE_FMT        "Use fmt lib" OFF)
option(ILIAS_USE_FIBER      "Use Fiber" ON)
option(ILIAS_USE_SPDLOG     "Use spdlog" OFF)
option(ILIAS_USE_IO_URING   "Use io_uring (Linux only)" OFF)

if(BUILD_SHARED_LIBS)
    set(ILIAS_DLL 1)
else()
    set(ILIAS_STATIC 1)
endif()

# Find the dependencies
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(FindDependencies)

# Add source code
set(SRCS
    src/fiber.cpp
    src/io.cpp
    src/log.cpp
    src/runtime.cpp
    src/sync.cpp
    src/task.cpp
    src/net/addrinfo.cpp
)

set(LIBS)

if(WIN32)
    list(APPEND SRCS 
        src/win32/io_context.cpp
        src/win32/ntdll.cpp
        src/win32/process.cpp
        src/win32/signal.cpp
        src/win32/win32.cpp
    )
    list(APPEND LIBS ws2_32 mswsock kernel32)
    
    if(ILIAS_TLS)
        # Windows use Schannel by default
        list(APPEND SRCS src/tls/schannel.cpp)
    endif()

elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND SRCS 
        src/linux/epoll.cpp
        src/linux/process.cpp
        src/linux/signal.cpp
    )
    list(APPEND LIBS pthread dl anl rt)

    if(ILIAS_USE_IO_URING)
        list(APPEND SRCS src/linux/uring.cpp)
    endif()
    
    if(ILIAS_TLS)
        list(APPEND SRCS src/tls/openssl.cpp)
    endif()

elseif(APPLE)
    # Unsupported
    message(FATAL_ERROR "MacOS is not supported yet")
endif()

# Get the information of the current git commit
find_package(Git QUIET)
set(ILIAS_GIT_COMMIT "unknown")
set(ILIAS_GIT_BRANCH "unknown")
set(ILIAS_GIT_TAG    "unknown")

if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD OUTPUT_VARIABLE ILIAS_GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse HEAD OUTPUT_VARIABLE ILIAS_GIT_COMMIT_LONG OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${GIT_EXECUTABLE} log -1 --format=%cd --date=format:%Y-%m-%d OUTPUT_VARIABLE ILIAS_GIT_COMMIT_DATE OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD OUTPUT_VARIABLE ILIAS_GIT_BRANCH OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0 OUTPUT_VARIABLE ILIAS_GIT_TAG OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags OUTPUT_VARIABLE ILIAS_GIT_TAG_LONG OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()

set(ILIAS_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(ILIAS_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(ILIAS_VERSION_PATCH ${PROJECT_VERSION_PATCH})

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/ilias/detail/config.hpp.cmake.in" 
    "${CMAKE_CURRENT_BINARY_DIR}/include/ilias/detail/config.hpp"
)

# Make library target
add_library(ilias ${SRCS})
add_library(ilias::ilias ALIAS ilias)

target_include_directories(ilias PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(ilias PRIVATE ${LIBS})

# Add the third party libraries
if(ILIAS_USE_FMT AND fmt_FOUND)
    target_link_libraries(ilias PUBLIC fmt::fmt)
endif()
if(ILIAS_USE_SPDLOG AND spdlog_FOUND)
    target_link_libraries(ilias PUBLIC spdlog::spdlog)
endif()
if(ILIAS_TLS AND OpenSSL_FOUND)
    target_link_libraries(ilias PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()
if(ILIAS_USE_IO_URING AND LIBURING_FOUND)
    target_link_libraries(ilias PRIVATE ${LIBURING_LIBRARIES})
    target_include_directories(ilias PRIVATE ${LIBURING_INCLUDE_DIRS})
endif()

if(WIN32)
    target_compile_definitions(ilias PRIVATE _WIN32_WINNT=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(BUILD_SHARED_LIBS)
    target_compile_definitions(ilias PRIVATE _ILIAS_SOURCE)
endif()

# Install & Export
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

# Install library & headers
install(TARGETS ilias EXPORT IliasTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/include/ilias/detail/config.hpp" 
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ilias/detail)

# Generate the configuration files
set(CONFIG_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/Ilias")

configure_package_config_file(
    cmake/IliasConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/IliasConfig.cmake
    INSTALL_DESTINATION ${CONFIG_INSTALL_DIR}
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/IliasConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(EXPORT IliasTargets
    FILE IliasTargets.cmake
    NAMESPACE ilias::
    DESTINATION ${CONFIG_INSTALL_DIR}
)

install(FILES 
    ${CMAKE_CURRENT_BINARY_DIR}/IliasConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/IliasConfigVersion.cmake
    DESTINATION ${CONFIG_INSTALL_DIR}
)
