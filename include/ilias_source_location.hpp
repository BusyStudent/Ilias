#pragma once
#include <version>
#include <stdint.h>

#ifdef __cpp_lib_source_location
#include <source_location>
#elif defined(__GNUC__) and (__GNUC__ > 4 or (__GNUC__ == 4 and __GNUC__MINOR__ >= 8))
#elif defined(_MSC_VER) && (_MSC_VER >= 1910)
#else
#   define ILIAS_NO_SOURCE_LOCATION
#endif

namespace std {
inline namespace _ilias_fallback {

#ifndef __cpp_lib_source_location
struct source_location {
#if defined(__GNUC__) and (__GNUC__ > 4 or (__GNUC__ == 4 and __GNUC__MINOR__ >= 8))
    inline static constexpr source_location current (
        const char *fileName = __builtin_FILE(),
        const char *funcName = __builtin_FUNCTION(),
        const uint32_t lineNumber = __builtin_LINE(),
        const uint32_t columnOffset = 0) noexcept
#elif defined(_MSC_VER) && (_MSC_VER >= 1910)
    inline static constexpr source_location current (
        const char *fileName = __builtin_FILE(),
        const char *funcName = __builtin_FUNCTION(),
        const uint32_t lineNumber = __builtin_LINE(),
        const uint32_t columnOffset = __builtin_COLUMN()) noexcept
#else
    inline static constexpr source_location current (
        const char *fileName = "unsupported",
        const char *funcName = "unsupported",
        const uint32_t lineNumber = 0,
        const uint32_t columnOffset = 0) noexcept
#endif
    {
        return source_location(fileName, funcName, lineNumber, columnOffset);
    }

    inline constexpr source_location(const char *fileName = "", const char *functionName = "",const uint32_t lineNumber = 0, const uint32_t columnOffset = 0) noexcept
    : mFileName(fileName),
    mFuncName(functionName),
    mLineNumber(lineNumber),
    mColumnOffset(columnOffset) { }
    source_location(const source_location&) = default;
    source_location(source_location&&) = default;
    source_location& operator=(const source_location&) = default;
    source_location& operator=(source_location&&) = default;

    inline constexpr const char *file_name() const noexcept {
        return mFileName;
    }

    inline constexpr const char *function_name() const noexcept {
        return mFuncName;
    }

    inline constexpr const uint32_t line() const noexcept {
        return mLineNumber;
    }

    inline constexpr const uint32_t column() const noexcept {
        return mColumnOffset;
    }
private:

    const char * mFileName;
    const char * mFuncName;
    uint32_t mLineNumber;
    uint32_t mColumnOffset;
};
#endif

}
}