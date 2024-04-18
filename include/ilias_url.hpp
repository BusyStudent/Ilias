#pragma once

#include "ilias.hpp"
#include <charconv>

ILIAS_NS_BEGIN

/**
 * @brief Wrapper of url string like https://google.com/xxxx
 * 
 */
class Url {
public:
    Url(std::string_view str);
    Url(const char *str);
    Url(const Url &);
    Url(Url &&url);
    Url();
    ~Url();
    
    auto empty() const -> bool;
    auto scheme() const -> std::string_view;
    auto query() const -> std::string_view;
    auto host() const -> std::string_view;
    auto path() const -> std::string_view;
    auto port() const -> uint16_t;
    auto toString() const -> std::string;

    auto operator =(const Url &) -> Url &;
    auto operator =(Url &&) -> Url &;
    auto operator <=>(const Url &other);
private:
    std::string mData;
};

inline Url::Url(std::string_view str) : mData(str) { }
inline Url::Url(const char *str) : mData(str) { }
inline Url::Url(const Url &) = default;
inline Url::Url(Url &&url) = default;
inline Url::Url() = default;
inline Url::~Url() = default;
inline auto Url::operator =(const Url &) -> Url & = default;
inline auto Url::operator =(Url &&) -> Url & = default;
inline auto Url::operator <=>(const Url &other) {
    return mData <=> other.mData;
}


inline auto Url::empty() const -> bool {
    return mData.empty();
}
inline auto Url::scheme() const -> std::string_view {
    std::string_view sv(mData);
    auto pos = sv.find("://");
    if (pos == std::string_view::npos) {
        return std::string_view();
    }
    return sv.substr(0, pos);
}
inline auto Url::port() const -> uint16_t {
    std::string_view sv(mData);
    do {
        // Try skip :// if has
        if (auto pos = sv.find("://"); pos != sv.npos) {
            sv = sv.substr(pos + 3);
        }

        // Try find port : and / end
        auto pos1 = sv.find(":");
        if (pos1 == sv.npos) {
            break;
        }
        auto port = sv.substr(pos1 + 1);
        // Try find : end /
        auto pos2 = port.find("/");
        if (pos2 != port.npos) {
            port = port.substr(0, pos2);
        }
        int result = 0;
        if (std::from_chars(port.data(), port.data() + port.size(), result).ec != std::errc()) {
            // Invliad String
            return 0;
        }
        return result;
    }
    while (false);
    // No port founded, try scheme
    auto sc = scheme();
    if (sc == "http") {
        return 80;
    }
    if (sc == "https") {
        return 443;
    }
    if (sc == "ftp") {
        return 21;
    }
    return 0; //< Unkown
}
inline auto Url::host() const -> std::string_view {
    std::string_view sv(mData);
    // Try find :// and /
    auto pos1 = sv.find("://");
    if (pos1 != sv.npos) {
        sv = sv.substr(pos1 + 3);
    }
    auto pos2 = sv.find("/");
    if (pos2 != sv.npos) {
        sv = sv.substr(0, pos2);
    }
    // Try find port
    auto pos3 = sv.find(":");
    if (pos3 != sv.npos) {
        sv = sv.substr(0, pos3);
    }
    return sv;
}
inline auto Url::path() const -> std::string_view {
    std::string_view sv(mData);
    auto pos = sv.find("://");
    if (pos != sv.npos) {
        sv = sv.substr(pos + 3);
    }
    pos = sv.find("/");
    if (pos == sv.npos) {
        return "/";
    }
    sv = sv.substr(pos);
    pos = sv.find("?");
    if (pos != sv.npos) {
        sv = sv.substr(0, pos);
    }
    return sv;
}
inline auto Url::query() const -> std::string_view {
    std::string_view sv(mData);
    auto pos = sv.find("?");
    if (pos == sv.npos) {
        return std::string_view();
    }
    return sv.substr(pos + 1);
}
inline auto Url::toString() const -> std::string {
    return mData;
}


ILIAS_NS_END