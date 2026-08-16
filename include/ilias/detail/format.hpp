/**
 * @file format.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The format utils for std::format and fmtlib (optional features)
 * @version 0.1
 * @date 2026-08-14
 * 
 * @copyright Copyright (c) 2026
 * 
 */
#pragma once

// Get the std
#include <version>
#include <concepts>

// Format check
#if   defined(ILIAS_USE_FMT)
    #define ILIAS_FMT_NAMESPACE ::fmt
    #include <fmt/format.h>
    #include <fmt/chrono.h>
#elif defined(__cpp_lib_format)
    #define ILIAS_FMT_NAMESPACE ::std
    #include <format>
#else
    #define ILIAS_NO_FORMAT
#endif

// Formatter macro
#define ILIAS_FORMATTER(type)                              \
    template <>                                            \
    struct ilias::fmtlib::formatter<type> :                \
        ::ilias::detail::DefaultFormatter

// Mark a type is formattable, generate the fmtlib bridge and ostream operator<<
#define ILIAS_FORMATTABLE(type)                              \
    inline auto _ilias_implToString(const type &t) {         \
        return ::ilias::detail::toStringImpl(t);             \
    }                                                                              \
                                                                                   \
    template <typename Stream> requires(                                           \
        requires(Stream &stream) { stream << std::string_view{}; }                 \
    )                                                                              \
    inline auto operator <<(Stream &stream, const type &t) -> decltype(auto) {     \
        return stream << _ilias_implToString(t);                                   \
    }

// Impl the formatter for IntoString concept
ILIAS_NS_BEGIN

#if !defined(ILIAS_NO_FORMAT)
// MARK: Formatting
namespace fmtlib = ILIAS_FMT_NAMESPACE;

// Formatter with default parse and redirect some formatting function to the fmtlib namespace
namespace detail {

// The Helper class for formatting (compatible with fmtlib::formatter and std::formatter)
struct DefaultFormatter {
    using format_parse_context = fmtlib::format_parse_context;
    using format_context = fmtlib::format_context;

    constexpr auto parse(auto &ctxt) const noexcept {
        return ctxt.begin();
    }

    // Redirect the format_to
    template <typename It, typename ...Args>
    static auto format_to(It &&it, fmtlib::format_string<Args...> fmt, Args &&...args) {
        return fmtlib::format_to(it, fmt, std::forward<Args>(args)...);
    }

    // Redirect the format
    template <typename ...Args>
    static auto format(fmtlib::format_string<Args...> fmt, Args &&...args) {
        return fmtlib::format(fmt, std::forward<Args>(args)...);
    }
};

} // namespace detail

#endif // ILIAS_NO_FORMAT

// MARK: ToString
namespace detail {

// Generic toString implementation, used by macros
template <typename T>
inline auto toStringImpl(const T &t) {
    if constexpr (requires { t.toString(); }) {
        return t.toString();
    }
    else if constexpr (requires { toString(t); }) {
        return toString(t);
    }
    else {
        static_assert(sizeof(T) == 0, "The type is not formattable");
    }
}

} // namespace detail

// Common Concepts
template <typename T>
concept IntoString = requires (const T &t) {
    { _ilias_implToString(t) } -> std::convertible_to<std::string_view>;
};

ILIAS_NS_END

#if !defined(ILIAS_NO_FORMAT)
// Make formatter for all type with InfoString concept
template <ilias::IntoString T>
class ilias::fmtlib::formatter<T> {
public:
    constexpr auto parse(auto &ctxt) {
        return inner.parse(ctxt);
    }

    auto format(const T &value, auto &ctxt) const {
        auto str = _ilias_implToString(value);
        return inner.format(std::string_view{str}, ctxt);
    }
private:
    ilias::fmtlib::formatter<std::string_view> inner;
};
#endif // ILIAS_NO_FORMAT