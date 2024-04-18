#pragma once

#include "ilias.hpp"
#include <memory>
#include <span>
#include <map>

ILIAS_NS_BEGIN

struct _Compare {
    using is_transparent = void;

    auto operator ()(std::string_view a, std::string_view b) const -> bool {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](auto a, auto b) {
            return std::tolower(a) < std::tolower(b);
        });
    }
};

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
        ContentType,
        ContentLength,
        ContentEncoding,
        Connection,
        TransferEncoding
    };

    HttpHeaders() = default;
    HttpHeaders(HttpHeaders &&) = default;
    HttpHeaders(const HttpHeaders &) = default;
    ~HttpHeaders() = default;

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
     * @brief Append a new header item
     * 
     * @param key 
     * @param value 
     */
    auto append(std::string_view key, std::string_view value) -> void;

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
     * @brief Append a new header item
     * 
     * @param key 
     * @param value 
     */
    auto append(WellKnownHeader header, std::string_view value) -> void;
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

    auto operator =(const HttpHeaders& other) -> HttpHeaders& = default;
    auto operator =(HttpHeaders&& other) -> HttpHeaders& = default;
    /**
     * @brief Get the string of the WellKnownHeader
     * 
     * @param header 
     * @return std::string_view 
     */
    static auto stringOf(WellKnownHeader header) -> std::string_view;
    /**
     * @brief Parse the data
     * 
     * @param data 
     * @return Expected<HttpHeaders, size_t> 
     */
    static auto parse(std::string_view data) -> HttpHeaders;
private:
    std::map<std::string, std::string, _Compare> mValues;
};

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
inline auto HttpHeaders::append(std::string_view key, std::string_view value) -> void {
    mValues.insert(std::make_pair(
        std::string(key), std::string(value)
    ));
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
        default: return ""sv;
    }
}

inline auto HttpHeaders::contains(WellKnownHeader header) const -> bool {
    return contains(stringOf(header));
}
inline auto HttpHeaders::value(WellKnownHeader header) const -> std::string_view {
    return value(stringOf(header));
}
inline auto HttpHeaders::append(WellKnownHeader header, std::string_view value) -> void {
    return append(stringOf(header), value);
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
inline auto HttpHeaders::parse(std::string_view text) -> HttpHeaders {
    // Split by \r\n
    HttpHeaders headers;
    size_t pos = text.find("\r\n");
    while (pos != text.npos) {
        auto range = text.substr(0, pos);
        // Try find : and split it
        size_t pos2 = range.find(": ");
        if (pos2 != range.npos) {
            auto key = range.substr(0, pos2);
            auto value = range.substr(pos2 + 2);
            headers.append(key, value);
        }
        // Forward
        text = text.substr(pos + 2);
        pos = text.find("\r\n");
    }
    return headers;
}

ILIAS_NS_END