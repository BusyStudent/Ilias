/**
 * @brief The ilias module entry point.
 * 
 */
module;

// MARK: C++ std
#include <cassert>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <ctime>
#include <cmath>

#include <type_traits>
#include <utility>
#include <limits>
#include <compare>
#include <version>

#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <span>
#include <optional>
#include <variant>

#include <memory>
#include <system_error>
#include <stdexcept>
#include <exception>

#include <functional>
#include <algorithm>
#include <iterator>
#include <ranges>
#include <concepts>

#include <iosfwd>
#include <ostream>
#include <istream>
#include <sstream>
#include <filesystem>
#include <chrono>

#include <source_location>
#include <bit>
#include <numbers>
#include <coroutine>
#include <stop_token>

// Some old C++20 doesn't have this
#if __has_include(<format>)
    #include <format>
#endif

// C++23
#if __has_include(<expected>)
    #include <expected>
#endif

#if __has_include(<print>)
    #include <print>
#endif

#if __has_include(<stacktrace>)
    #include <stacktrace>
#endif

#if __has_include(<spanstream>)
    #include <spanstream>
#endif

#if __has_include(<mdspan>)
    #include <mdspan>
#endif

// Win32
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN 1
    #define NOMINMAX 1
    #include <winsock2.h>
    #include <windows.h>
#endif // _WIN32


// MARK: ilias
export module ilias;

#define ILIAS_MODULE
extern "C++" {
    #include <ilias.hpp>
}