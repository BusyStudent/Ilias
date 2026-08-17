/**
 * @brief The ilias module entry point.
 * 
 */
module;

#include <ilias/detail/config.hpp>
#include <version>

// C
#include <cassert>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <ctime>
#include <cmath>

// Core
#include <algorithm>
#include <atomic>
#include <bit>
#include <charconv>
#include <compare>
#include <concepts>
#include <coroutine>
#include <exception>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <ranges>
#include <source_location>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <version>

// Container
#include <array>
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// Io Fs Time
#include <chrono>
#include <filesystem>
#include <iosfwd>
#include <istream>
#include <ostream>
#include <sstream>

// Sync
#include <latch>
#include <mutex>
#include <stop_token>

// std::format
#if __has_include(<format>)
    #include <format>
#endif

// std::expected
#if __has_include(<expected>)
    #include <expected>
#endif

// std::stacktrace
#if __has_include(<stacktrace>)
    #include <stacktrace>
#endif

// Win32
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN 1
    #define NOMINMAX 1
    #include <WinSock2.h>
    #include <WS2tcpip.h>
    #include <Windows.h>
#endif // _WIN32

// Linux
#if defined(__linux__)
    #include <unistd.h>
    #include <fcntl.h>

    #include <sys/stat.h>
    #include <sys/uio.h>

    #include <sys/socket.h>
    #include <sys/poll.h>
    #include <sys/un.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>

    #include <sys/epoll.h>
#endif

// MARK: Thirdparty
#if defined(ILIAS_USE_ZEUS_EXPECTED)
    #include <ilias/detail/bundled/expected.hpp>
#endif // ILIAS_USE_ZEUS_EXPECTED

#if defined(ILIAS_USE_FMT)
    #include <fmt/format.h>
    #include <fmt/chrono.h>
#endif // ILIAS_USE_FMT

// MARK: ilias
export module ilias;

#if defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable: 4251) // Shut up!!!
    #pragma warning(disable: 5244)
    // We already pre-include all external dependencies in the global module fragment
#endif // _MSC_VER

#define ILIAS_MODULE
extern "C++" {
    #include <ilias.hpp>
}

#if defined(_MSC_VER)
    #pragma warning(pop)
#endif // _MSC_VER