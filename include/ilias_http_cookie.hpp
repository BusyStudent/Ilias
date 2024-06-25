#pragma once

#include "ilias_url.hpp"
#include <charconv>
#include <chrono>
#include <string>
#include <vector>
#include <map>

ILIAS_NS_BEGIN

class HttpCookie {
public:
    HttpCookie(std::string_view name, std::string_view value);
    HttpCookie(const HttpCookie &);
    HttpCookie(HttpCookie &&);
    HttpCookie();
    ~HttpCookie();

    auto name() const -> const std::string &;
    auto path() const -> const std::string &;
    auto domain() const -> const std::string &;
    auto value() const -> const std::string &;
    auto expireTime() const -> std::chrono::system_clock::time_point;
    auto isValid() const -> bool;
    auto isExpired() const -> bool;
    auto setName(std::string_view name) -> void;
    auto setValue(std::string_view value) -> void;
    auto setDomain(std::string_view domain) -> void;
    auto setPath(std::string_view path) -> void;
    auto operator =(const HttpCookie &) -> HttpCookie &;
    auto operator =(HttpCookie &&) -> HttpCookie &;

    static auto parse(std::string_view setCookie) -> std::vector<HttpCookie>;
private:
    static auto _parseTime(std::string_view expireTime) -> std::chrono::system_clock::time_point;
    static auto _strcasecmp(std::string_view str1, std::string_view str2) -> bool;
    
    std::string mName;
    std::string mValue;
    std::string mDomain;
    std::string mPath;
    std::chrono::system_clock::time_point mCreated;
    std::chrono::system_clock::time_point mExpireTime;
};

class HttpCookieJar {
public:
    HttpCookieJar();
    HttpCookieJar(const HttpCookieJar &) = delete;
    ~HttpCookieJar();

    auto insertCookie(HttpCookie &&) -> bool;
    auto insertCookie(const HttpCookie &cookie) -> bool;
    auto cookiesForUrl(const Url &url) -> std::vector<HttpCookie>;
    auto allCookies() const -> std::vector<HttpCookie>;
private:
    // Mapping domain to site's cookie
    std::map<
        std::string, 
        std::map<std::string, HttpCookie, std::less<> >, 
        std::less<> 
    > mCookies;
};

inline HttpCookie::HttpCookie() = default;
inline HttpCookie::~HttpCookie() = default;
inline HttpCookie::HttpCookie(const HttpCookie &) = default;
inline HttpCookie::HttpCookie(HttpCookie &&) = default;
inline HttpCookie::HttpCookie(std::string_view name, std::string_view value) : mName(name), mValue(value) { }
inline auto HttpCookie::operator =(const HttpCookie &) -> HttpCookie & = default;
inline auto HttpCookie::operator =(HttpCookie &&) -> HttpCookie & = default;

inline auto HttpCookie::name() const -> const std::string & { return mName; }
inline auto HttpCookie::value() const -> const std::string & { return mValue; }
inline auto HttpCookie::domain() const -> const std::string & { return mDomain; }
inline auto HttpCookie::path() const -> const std::string & { return mPath; }
inline auto HttpCookie::expireTime() const -> std::chrono::system_clock::time_point { return mExpireTime; }
inline auto HttpCookie::isValid() const -> bool { return !mName.empty(); }
inline auto HttpCookie::isExpired() const -> bool { 
    if (mExpireTime == std::chrono::system_clock::time_point{}) return false;
    return mExpireTime <= std::chrono::system_clock::now(); 
}

inline auto HttpCookie::setName(std::string_view name) -> void { mName = name; }
inline auto HttpCookie::setValue(std::string_view value) -> void { mValue = value; }
inline auto HttpCookie::setDomain(std::string_view domain) -> void { mDomain = domain; }
inline auto HttpCookie::setPath(std::string_view path) -> void { mPath = path; }

inline auto HttpCookie::parse(std::string_view setCookie) -> std::vector<HttpCookie> {
    std::vector<std::pair<std::string_view, std::string_view> > kvs; //< Another name value pairs
    std::vector<HttpCookie> cookies;
    std::string_view domain;
    std::string_view path;
    std::string_view expires;
    std::string_view maxAges;
    std::chrono::system_clock::time_point expireTime;

    // Parse the set-cookie header
    // Split all string by '; '
    size_t delim = setCookie.find("; ");
    while (!setCookie.empty()) {
        // Split the string by '='
        size_t delim2 = setCookie.find('=');
        auto name = setCookie.substr(0, std::min(delim, delim2));
        auto value = std::string_view();
        if (delim2 != std::string::npos) {
            value = setCookie.substr(delim2 + 1, delim - delim2 - 1);
        }

        // Special cases
        if (_strcasecmp(name, "domain")) {
            domain = value;
        }
        else if (_strcasecmp(name, "path")) {
            path = value;
        }
        else if (_strcasecmp(name, "expires")) {
            expires = value;
        }
        else if (_strcasecmp(name, "max-age")) {
            maxAges = value;
        }
        else {
            kvs.emplace_back(name, value);
        }

        // Find the next delimiter
        if (delim == std::string::npos) {
            break;
        }

        // Advance
        setCookie = setCookie.substr(delim + 2);
        delim = setCookie.find("; ");
    }

    // Build cookie here
    
    // Calc expireTime
    auto now = std::chrono::system_clock::now();
    if (!maxAges.empty()) {
        // The MDN said the maxAges has precedence
        int64_t seconds;
        if (std::from_chars(maxAges.data(), maxAges.data() + maxAges.size(), seconds).ec == std::errc()) {
            expireTime = now + std::chrono::seconds(seconds);
        }
    }
    else if (!expires.empty()) {
        // TODO : parse expires
        expireTime = _parseTime(expires);
    }
    for (auto [name, value] : kvs) {
        auto &cookie = cookies.emplace_back(name, value);
        cookie.mCreated = now;
        if (!path.empty()) {
            cookie.mPath = path;
        }
        if (!domain.empty()) {
            cookie.mDomain = domain;
        }
        cookie.mExpireTime = expireTime;
    }
    return cookies;
}
inline auto HttpCookie::_parseTime(std::string_view expires) -> std::chrono::system_clock::time_point {
    // Like Wed, 23 Apr 2020 10:10:10 GMT
    //      week, days, months, years, hh, min, ss
    auto pos = expires.find(", ");
    auto space = expires.find(" ", pos + 2);
    if (pos == std::string::npos) {
        return {};
    }
    // Find days
    auto days = expires.substr(pos + 2, space - pos - 2);
    expires = expires.substr(space + 1);
    // Find months
    pos = expires.find(" ");
    if (pos == std::string::npos) {
        return {};
    }
    auto months = expires.substr(0, pos);
    expires = expires.substr(pos + 1);
    // Find years
    pos = expires.find(" ");
    if (pos == std::string::npos) {
        return {};
    }
    auto years = expires.substr(0, pos);
    expires = expires.substr(pos + 1);
    // Find time
    pos = expires.find(" ");
    if (pos == std::string::npos) {
        return {};
    }
    auto time = expires.substr(0, pos);
    // Split time
    pos = time.find(":");
    if (pos == std::string::npos) {
        return {};
    }
    auto hh = time.substr(0, pos);
    time = time.substr(pos + 1);
    pos = time.find(":");
    if (pos == std::string::npos) {
        return {};
    }
    auto min = time.substr(0, pos);
    auto ss = time.substr(pos + 1);
    // Begin parse here
    ::tm t {};
    if (std::from_chars(days.data(), days.data() + days.size(), t.tm_mday).ec != std::errc()) {
        return {};
    }
    if (std::from_chars(years.data(), years.data() + years.size(), t.tm_year).ec != std::errc()) {
        return {};
    }
    t.tm_year -= 1900;
    // Months tables
    constexpr std::array monthsTable {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    t.tm_mon = -1;
    for (size_t i = 0; i < monthsTable.size(); i++) {
        if (_strcasecmp(monthsTable[i], months)) {
            t.tm_mon = i;
            break;
        }
    }
    if (t.tm_mon == -1) {
        return {};
    }
    if (
        std::from_chars(hh.data(), hh.data() + hh.size(), t.tm_hour).ec != std::errc() ||
        std::from_chars(min.data(), min.data() + min.size(), t.tm_min).ec != std::errc() ||
        std::from_chars(ss.data(), ss.data() + ss.size(), t.tm_sec).ec != std::errc()) 
    {
        return {};
    }
    return std::chrono::system_clock::from_time_t(::mktime(&t));
}
inline auto HttpCookie::_strcasecmp(std::string_view a, std::string_view b) -> bool {
    if (a.size() != b.size()) {
        return false;
    }
#if     defined(_WIN32)
    return ::_strnicmp(a.data(), b.data(), a.size()) == 0;
#else
    return ::strncasecmp(a.data(), b.data(), a.size()) == 0;
#endif
}


// --- HttpCookieJar
inline HttpCookieJar::HttpCookieJar() = default;
inline HttpCookieJar::~HttpCookieJar() = default;

inline auto HttpCookieJar::insertCookie(const HttpCookie& cookie) -> bool {
    if (!cookie.isValid()) {
        return false;
    }
    return insertCookie(HttpCookie(cookie));
}
inline auto HttpCookieJar::insertCookie(HttpCookie &&cookie) -> bool {
    if (!cookie.isValid()) {
        return false;
    }
    // try find the domain
    mCookies[cookie.domain()][cookie.name()] = std::move(cookie);
    return true;
}
inline auto HttpCookieJar::cookiesForUrl(const Url &url) -> std::vector<HttpCookie> {
    std::vector<HttpCookie> ret;
    if (mCookies.empty()) {
        return ret;
    }
    auto host = url.host();
    auto iter = mCookies.find(host);
    while (iter == mCookies.end()) {
        // If cookie's domain is like this .google.com
        // we need to remove the host's first dot
        auto dot = host.find('.', 1);
        if (dot == host.npos) {
            break;
        }
        host = host.substr(dot);
        iter = mCookies.find(host);
    }
    if (iter == mCookies.end()) {
        return ret;
    }
    // Add all items to it
    auto &[_, map] = *iter;
    for (auto iter = map.begin(); iter != map.end(); ) {
        auto &[_, cookie] = *iter;
        if (cookie.isExpired()) {
            iter = map.erase(iter);
            continue;
        }
        ret.emplace_back(cookie);
        ++iter;
    }
    return ret;
}
inline auto HttpCookieJar::allCookies() const -> std::vector<HttpCookie> {
    std::vector<HttpCookie> ret;
    for (auto &[_, map] : mCookies) {
        for (auto &[_, cookie] : map) {
            ret.emplace_back(cookie);
        }
    }
    return ret;
}

ILIAS_NS_END