/**
 * @file headers.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Impl the HttpHeaders container
 * @version 0.1
 * @date 2024-09-06
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/detail/mem.hpp>
#include <ilias/ilias.hpp>
#include <initializer_list>
#include <vector>
#include <map>

ILIAS_NS_BEGIN

/**
 * @brief Http Headers
 * 
 */
class HttpHeaders {
public:
    enum WellKnownHeader {
        UserAgent,
        Referer,
        Accept,
        SetCookie,
        ContentType,
        ContentLength,
        ContentEncoding,
        Connection,
        TransferEncoding,
        Location,
        Origin,
        Cookie,
        Host,
        Range,
    };

    HttpHeaders() = default;
    HttpHeaders(HttpHeaders &&) = default;
    HttpHeaders(const HttpHeaders &) = default;
    ~HttpHeaders() = default;

    /**
     * @brief Construct a new Http Headers object by init list
     * 
     * @param headers 
     */
    HttpHeaders(std::initializer_list<std::pair<std::string_view, std::string_view> > headers);

    /**
     * @brief Check this header contains this key
     * 
     * @return true 
     * @return false 
     */
    auto contains(std::string_view key) const -> bool;

    /**
     * @brief Get the key's value
     * 
     * @param key 
     * @return std::string_view 
     */
    auto value(std::string_view key) const -> std::string_view;

    /**
     * @brief Get all values of this key
     * 
     * @param key
     * @return std::vector<std::string_view> 
     */
    auto values(std::string_view key) const -> std::vector<std::string_view>;

    /**
     * @brief Append a new header item
     * 
     * @param key 
     * @param value 
     */
    auto append(std::string_view key, std::string_view value) -> void;

    /**
     * @brief Remove the value by key
     * 
     * @param key 
     */
    auto remove(std::string_view key) -> void;


    /**
     * @brief Check this header contains this key
     * 
     * @return true 
     * @return false 
     */
    auto contains(WellKnownHeader header) const -> bool;

    /**
     * @brief Get the key's value
     * 
     * @param key 
     * @return std::string_view 
     */
    auto value(WellKnownHeader header) const -> std::string_view;

    /**
     * @brief Get all values of this key
     * 
     * @param headers 
     * @return std::vector<std::string_view> 
     */
    auto values(WellKnownHeader headers) const -> std::vector<std::string_view>;

    /**
     * @brief Append a new header item
     * 
     * @param key 
     * @param value 
     */
    auto append(WellKnownHeader header, std::string_view value) -> void;

    /**
     * @brief Remove the value by key
     * 
     * @param key 
     */
    auto remove(WellKnownHeader header) -> void;

    /**
     * @brief Get the begin iterator
     * 
     * @return auto 
     */
    auto begin() const;

    /**
     * @brief Get the end iterator
     * 
     * @return auto 
     */
    auto end() const;

    /**
     * @brief Check is empty
     * 
     * @return true 
     * @return false 
     */
    auto empty() const -> bool;

    auto operator =(const HttpHeaders &other) -> HttpHeaders & = default;
    auto operator =(HttpHeaders &&other) -> HttpHeaders & = default;
    auto operator ==(const HttpHeaders &other) const -> bool = default;
    auto operator !=(const HttpHeaders &other) const -> bool = default;
    auto operator <=>(const HttpHeaders &other) const noexcept = default;

    /**
     * @brief Get the string of the WellKnownHeader
     * 
     * @param header 
     * @return std::string_view 
     */
    static auto stringOf(WellKnownHeader header) -> std::string_view;
private:
    std::multimap<std::string, std::string, mem::CaseCompare> mValues;
};

inline HttpHeaders::HttpHeaders(std::initializer_list<std::pair<std::string_view, std::string_view> > headers) : 
    mValues(headers.begin(), headers.end()) 
{

}

inline auto HttpHeaders::contains(std::string_view key) const -> bool {
    return mValues.contains(key);
}

inline auto HttpHeaders::value(std::string_view key) const -> std::string_view {
    auto iter = mValues.find(key);
    if (iter == mValues.end()) {
        return std::string_view();
    }
    return iter->second;
}

inline auto HttpHeaders::values(std::string_view key) const -> std::vector<std::string_view> {
    std::vector<std::string_view> ret;
    auto [begin, end] = mValues.equal_range(key);
    for (auto i = begin; i != end; ++i) {
        ret.emplace_back(i->second);
    }
    return ret;
}

inline auto HttpHeaders::append(std::string_view key, std::string_view value) -> void {
    mValues.emplace(key, value);
}

inline auto HttpHeaders::remove(std::string_view key) -> void {
    auto range = mValues.equal_range(key);
    mValues.erase(range.first, range.second);
}

inline auto HttpHeaders::stringOf(WellKnownHeader header) -> std::string_view {
    using namespace std::literals;
    switch (header) {
        case UserAgent: return "User-Agent"sv;
        case Accept: return "Accept"sv;
        case ContentType: return "Content-Type"sv;
        case ContentLength: return "Content-Length"sv;
        case ContentEncoding: return "Content-Encoding"sv;
        case Connection: return "Connection"sv;
        case TransferEncoding: return "Transfer-Encoding"sv;
        case SetCookie: return "Set-Cookie"sv;
        case Referer: return "Referer"sv;
        case Location: return "Location"sv;
        case Origin: return "Origin"sv;
        case Host: return "Host"sv;
        case Cookie: return "Cookie"sv;
        case Range: return "Range"sv;
        default: return ""sv;
    }
}

inline auto HttpHeaders::contains(WellKnownHeader header) const -> bool {
    return contains(stringOf(header));
}

inline auto HttpHeaders::value(WellKnownHeader header) const -> std::string_view {
    return value(stringOf(header));
}

inline auto HttpHeaders::values(WellKnownHeader header) const -> std::vector<std::string_view> {
    return values(stringOf(header));
}

inline auto HttpHeaders::append(WellKnownHeader header, std::string_view value) -> void {
    return append(stringOf(header), value);
}

inline auto HttpHeaders::remove(WellKnownHeader header) -> void {
    return remove(stringOf(header));
}

inline auto HttpHeaders::begin() const {
    return mValues.begin();
}

inline auto HttpHeaders::end() const {
    return mValues.end();
}

inline auto HttpHeaders::empty() const -> bool {
    return mValues.empty();
}

ILIAS_NS_END

// --- Formatter for HttpHeaders
#if !defined(ILIAS_NO_FORMAT)
ILIAS_FORMATTER(HttpHeaders::WellKnownHeader) {
    auto format(const auto &header, auto &ctxt) const {
        return format_to(ctxt.out(), "{}", ILIAS_NAMESPACE::HttpHeaders::stringOf(header));
    }
};

ILIAS_FORMATTER(HttpHeaders) {
    auto format(const auto &headers, auto &ctxt) const {
        for (auto &[key, value] : headers) {
            format_to(ctxt.out(), "{}: {}\r\n", key, value);
        }
    }
};
#endif