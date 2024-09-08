/**
 * @file cookie.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Impl the HttpCookie class and Jar class
 * @version 0.1
 * @date 2024-09-06
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#pragma once

#include <ilias/detail/mem.hpp>
#include <ilias/url.hpp>
#include <charconv>
#include <chrono>
#include <string>
#include <vector>
#include <map>

ILIAS_NS_BEGIN

/**
 * @brief The Single Http Cookie class
 * 
 */
class HttpCookie {
public:
    enum SameSite {
        Strict,
        Lax,
        None
    };

    HttpCookie(std::string_view name, std::string_view value);
    HttpCookie(const HttpCookie &) = default;
    HttpCookie(HttpCookie &&) = default;
    HttpCookie() = default;

    /**
     * @brief Get the cookie's name
     * 
     * @return const std::string& 
     */
    auto name() const -> const std::string &;

    /**
     * @brief Get the cookie's path
     * 
     * @return const std::string& 
     */
    auto path() const -> const std::string &;

    /**
     * @brief Get the cookie's domain
     * 
     * @return const std::string& 
     */
    auto domain() const -> const std::string &;

    /**
     * @brief Get the cookie's value
     * 
     * @return const std::string& 
     */
    auto value() const -> const std::string &;

    /**
     * @brief Get the cookie's expire time
     * 
     * @return std::chrono::system_clock::time_point 
     */
    auto expireTime() const -> std::chrono::system_clock::time_point;

    /**
     * @brief Get the cookie's same site policy
     * 
     * @return SameSite 
     */
    auto sameSite() const -> SameSite;

    /**
     * @brief Check if the cookie is valid
     * 
     * @return true 
     * @return false 
     */
    auto isValid() const -> bool;

    /**
     * @brief Check if the cookie is expired
     * 
     * @return true 
     * @return false 
     */
    auto isExpired() const -> bool;

    /**
     * @brief Check if the cookie is secure (request over https)
     * 
     * @return true 
     * @return false 
     */
    auto isSecure() const -> bool;

    /**
     * @brief Check if the cookie is http only
     * 
     * @return true 
     * @return false 
     */
    auto isHttpOnly() const -> bool;

    /**
     * @brief Set the cookie name
     * 
     * @param name 
     */
    auto setName(std::string_view name) -> void;

    /**
     * @brief Set the cookie value
     * 
     * @param value 
     */
    auto setValue(std::string_view value) -> void;

    /**
     * @brief Set the cookie domain
     * 
     * @param domain 
     */
    auto setDomain(std::string_view domain) -> void;

    /**
     * @brief Set the coookie path
     * 
     * @param path 
     */
    auto setPath(std::string_view path) -> void;

    /**
     * @brief Set the cookie secure (request over https)
     * 
     * @param secure 
     */
    auto setSecure(bool secure) -> void;

    /**
     * @brief Set the cookie http only
     * 
     * @param httpOnly 
     */
    auto setHttpOnly(bool httpOnly) -> void;

    /**
     * @brief Set the cookie same site policy
     * 
     * @param sameSite 
     */
    auto setSameSite(SameSite sameSite) -> void;

    /**
     * @brief Set the cookie expire time
     * 
     * @param expireTime 
     */
    auto setExpireTime(std::chrono::system_clock::time_point expireTime) -> void;

    /**
     * @brief Set extra information by url if not specified
     * 
     * @param url 
     */
    auto normalize(const Url &url) -> void;

    auto operator =(const HttpCookie &) -> HttpCookie & = default;

    auto operator =(HttpCookie &&) -> HttpCookie & = default;

    auto operator <=>(const HttpCookie &) const = default;

    /**
     * @brief Parse the cookie from the Set-Cookie header
     * 
     * @param setCookie The value of the Set-Cookie header (cookie 1; cookie 2; ...)
     * @return std::vector<HttpCookie> 
     */
    static auto parse(std::string_view setCookie) -> std::vector<HttpCookie>;
private:
    static auto parseTime(std::string_view expireTime) -> std::chrono::system_clock::time_point;
    
    std::string mName;
    std::string mValue;
    std::string mDomain;
    std::string mPath;
    bool        mSecure = false;
    bool        mHttpOnly = false;
    SameSite    mSameSite = SameSite::Lax; //< Default is Lax
    std::chrono::system_clock::time_point mCreated;
    std::chrono::system_clock::time_point mExpireTime;
};

/**
 * @brief The Http Cookie Jar class, it is the container of all the cookies
 * 
 */
class HttpCookieJar {
public:
    HttpCookieJar() = default;

    auto insertCookie(HttpCookie &&) -> bool;
    auto insertCookie(const HttpCookie &cookie) -> bool;
    auto cookiesForUrl(const Url &url) -> std::vector<HttpCookie>;
    auto allCookies() const -> std::vector<HttpCookie>;
private:
    // Mapping domain to site's cookie
    std::map<
        std::string, 
        std::map<std::string, HttpCookie, std::less<> >, //< The cookie name to cookie object
        mem::CaseCompare //< Domain name is case insensitive
    > mCookies;
};

inline HttpCookie::HttpCookie(std::string_view name, std::string_view value) : mName(name), mValue(value) { }

inline auto HttpCookie::name() const -> const std::string & { return mName; }

inline auto HttpCookie::value() const -> const std::string & { return mValue; }

inline auto HttpCookie::domain() const -> const std::string & { return mDomain; }

inline auto HttpCookie::path() const -> const std::string & { return mPath; }

inline auto HttpCookie::expireTime() const -> std::chrono::system_clock::time_point { return mExpireTime; }

inline auto HttpCookie::isValid() const -> bool { 
    return !mName.empty() && !mValue.empty() && !mDomain.ends_with('.');
}

inline auto HttpCookie::isExpired() const -> bool { 
    if (mExpireTime == std::chrono::system_clock::time_point{}) return false;
    return mExpireTime <= std::chrono::system_clock::now(); 
}

inline auto HttpCookie::isSecure() const -> bool { return mSecure; }

inline auto HttpCookie::isHttpOnly() const -> bool { return mHttpOnly; }

inline auto HttpCookie::sameSite() const -> SameSite { return mSameSite; }

inline auto HttpCookie::setName(std::string_view name) -> void { mName = name; }

inline auto HttpCookie::setValue(std::string_view value) -> void { mValue = value; }

inline auto HttpCookie::setDomain(std::string_view domain) -> void { 
    if(domain.starts_with('.')) { //< According to MDN, ignore the leading dot
        domain.remove_prefix(1);
    } 
    mDomain = domain; 
}

inline auto HttpCookie::setPath(std::string_view path) -> void { mPath = path; }

inline auto HttpCookie::setSecure(bool secure) -> void { mSecure = secure; }

inline auto HttpCookie::setHttpOnly(bool httpOnly) -> void { mHttpOnly = httpOnly; }

inline auto HttpCookie::setSameSite(SameSite sameSite) -> void { mSameSite = sameSite; }

inline auto HttpCookie::setExpireTime(std::chrono::system_clock::time_point expireTime) -> void { mExpireTime = expireTime; }

inline auto HttpCookie::normalize(const Url &url) -> void {
    if (mDomain.empty()) mDomain = url.host();
    if (mPath.empty()) mPath = url.path();
}

inline auto HttpCookie::parse(std::string_view setCookie) -> std::vector<HttpCookie> {
    std::vector<std::pair<std::string_view, std::string_view> > kvs; //< Another name value pairs
    std::vector<HttpCookie> cookies;
    std::string_view domain;
    std::string_view path;
    std::string_view expires;
    std::string_view maxAges;
    std::chrono::system_clock::time_point expireTime;
    SameSite sameSite = SameSite::Lax;
    bool secure = false;
    bool httpOnly = false;

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
        if (mem::strcasecmp(name, "domain") == std::strong_ordering::equal) {
            domain = value;
        }
        else if (mem::strcasecmp(name, "path") == std::strong_ordering::equal) {
            path = value;
        }
        else if (mem::strcasecmp(name, "expires") == std::strong_ordering::equal) {
            expires = value;
        }
        else if (mem::strcasecmp(name, "max-age") == std::strong_ordering::equal) {
            maxAges = value;
        }
        else if (mem::strcasecmp(name, "secure") == std::strong_ordering::equal) {
            secure = true;
        }
        else if (mem::strcasecmp(name, "httponly") == std::strong_ordering::equal) {
            httpOnly = true;
        }
        else if (mem::strcasecmp(name, "samesite") == std::strong_ordering::equal) {
            if (mem::strcasecmp(value, "strict") == std::strong_ordering::equal) {
                sameSite = SameSite::Strict;
            }
            else if (mem::strcasecmp(value, "lax") == std::strong_ordering::equal) {
                sameSite = SameSite::Lax;
            }
            else if (mem::strcasecmp(value, "none") == std::strong_ordering::equal) {
                sameSite = SameSite::None;
            }
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
        expireTime = parseTime(expires);
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
        cookie.mSecure = secure;
        cookie.mHttpOnly = httpOnly;
        cookie.mSameSite = sameSite;
        cookie.mExpireTime = expireTime;
    }
    return cookies;
}

inline auto HttpCookie::parseTime(std::string_view expires) -> std::chrono::system_clock::time_point {
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
        if (mem::strcasecmp(monthsTable[i], months) == std::strong_ordering::equal) {
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


// --- HttpCookieJar
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
    auto cur = std::string_view(host);
    while (!cur.empty() && cur.contains('.')) { //for each level of the domain, www.google.com -> google.com
        auto iter = mCookies.find(cur);
        if (iter != mCookies.end()) {
            // Add all items in current domain to it
            auto &[_, map] = *iter;
            for (auto iter = map.begin(); iter != map.end(); ) {
                auto &[_, cookie] = *iter;
                if (cookie.isExpired()) {
                    iter = map.erase(iter);
                    continue;
                }
                if (cookie.path().empty() || url.path().starts_with(cookie.path())) {
                    // Handle the path
                    ret.emplace_back(cookie);
                }
                ++iter;
            }
        }
        // Find the next domain by dot
        auto pos = cur.find('.', 1);
        if (pos == std::string::npos) {
            break;
        }
        if (pos + 1 == cur.size()) { //< xxx. (dot on the last)
            break;
        }
        cur = cur.substr(pos + 1);
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

// --- Formatter for HttpCookie 
#if defined(__cpp_lib_format)
template <>
struct std::formatter<ILIAS_NAMESPACE::HttpCookie::SameSite> {
    using SameSite = ILIAS_NAMESPACE::HttpCookie::SameSite;

    constexpr auto parse(std::format_parse_context &ctx) {
        return ctx.begin();
    }
    template <typename FormatContext>
    auto format(const SameSite &ss, FormatContext &ctx) {    
        std::string_view content;
        switch (ss) {
            case SameSite::Strict: content = "Strict"; break;
            case SameSite::Lax:     content = "Lax"; break;
            case SameSite::None:    content = "None"; break;
            default: ::abort();
        }
        return std::format_to(ctx.out(), "{}", content);
    }
};
#endif