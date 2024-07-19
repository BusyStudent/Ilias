#pragma once

/**
 * @file charcvt.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Utils to convert between char(utf8) and wchar_t
 * @version 0.1
 * @date 2024-07-19
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#if defined(_WIN32)

#include "../ilias.hpp"
#include <Windows.h> //< For MultiByteToWideChar
#include <string>

ILIAS_NS_BEGIN

inline auto Utf8ToWide(std::string_view u8) -> std::wstring {
    auto len = MultiByteToWideChar(CP_UTF8, 0, u8.data(), u8.size(), nullptr, 0);
    std::wstring buf(len, L'\0');
    len = MultiByteToWideChar(CP_UTF8, 0, u8.data(), u8.size(), buf.data(), len);
    return buf;
}
inline auto WideToUtf8(std::wstring_view wide) -> std::string {
    auto len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wide.size(), nullptr, 0, nullptr, nullptr);
    std::string buf(len, '\0');
    len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wide.size(), buf.data(), len, nullptr, nullptr);
    return buf;
}

ILIAS_NS_END

#endif