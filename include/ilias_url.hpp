#pragma once

#include "ilias.hpp"
#include <charconv>
#include <optional>
#include <cctype>
#include <bit>

ILIAS_NS_BEGIN

/**
 * @brief Wrapper of url string like https://google.com/xxxx, interface like QUrl
 * 
 */
class Url {
public:
    /**
     * @brief Construct a new Url object
     * 
     * @param str The valid url string
     */
    Url(std::string_view str);
    Url(const char *str);
    Url(const Url &);
    Url(Url &&url);
    Url();
    ~Url();

    /**
     * @brief Check the url is empty
     * 
     * @return true 
     * @return false 
     */
    auto empty() const -> bool;

    /**
     * @brief Check all compoments is valid and no-empty
     * 
     * @return true 
     * @return false 
     */
    auto isValid() const -> bool;
    
    /**
     * @brief Get the scheme of the url
     * 
     * @return std::string_view 
     */
    auto scheme() const -> std::string_view;

    /**
     * @brief Get the query of the url
     * 
     * @return std::string_view 
     */
    auto query() const -> std::string_view;

    /**
     * @brief Get the host of the url
     * 
     * @return std::string_view 
     */
    auto host() const -> std::string_view;

    /**
     * @brief Get the path of the url
     * 
     * @return std::string_view 
     */
    auto path() const -> std::string_view;

    /**
     * @brief Get the port of the url
     * 
     * @param defaultPort
     * 
     * @return std::optional<uint16_t> 
     */
    auto port() const -> std::optional<uint16_t>;

    /**
     * @brief Get the port of the url, it will return scheme's default if port is not sepcified
     * 
     * @return std::optional<uint16_t> 
     */
    auto portOrScheme() const -> std::optional<uint16_t>;

    /**
     * @brief Make the url to the string (encoded)
     * 
     * @return std::string 
     */
    auto toString() const -> std::string;

    /**
     * @brief Set the Scheme object, must in ansii
     * 
     * @param scheme 
     */
    auto setScheme(std::string_view scheme) -> void;

    /**
     * @brief Set the Query object, must be url-encoded(precent encoding) string.
     * @note The query will be decoded before set.
     * 
     * @param query 
     */
    auto setQuery(std::string_view query) -> void;

    /**
     * @brief Set the Host object
     * 
     * @param host 
     */
    auto setHost(std::string_view host) -> void;

    /**
     * @brief Set the Path object, must be url-encoded(precent encoding) string.
     * 
     * @param path 
     */
    auto setPath(std::string_view path) -> void;

    /**
     * @brief Set the Port object, in range 0 - 65535
     * 
     * @param port 
     */
    auto setPort(std::optional<uint16_t> port) -> void;

    auto operator =(const Url &) -> Url &;
    auto operator =(Url &&) -> Url &;
    auto operator <=>(const Url &other) const noexcept;

    /**
     * @brief Encode the string to url-encoded(precent encoding) string.
     * @note char not in 'a' - 'z' and 'A' - 'Z' and '0' - '9' and '-' and '.' and '_' and '~' will be encoded.
     * 
     * @param str 
     * @return std::string 
     */
    static auto encodeComponent(std::string_view str) -> std::string;

    /**
     * @brief Decode the url-encoded (precent encoding) string to original string.
     * 
     * @param str 
     * @return std::string 
     */
    static auto decodeComponent(std::string_view str) -> std::string;
private:
    static auto _parseScheme(std::string_view) -> std::string_view;
    static auto _parseQuery(std::string_view) -> std::string_view;
    static auto _parseHost(std::string_view) -> std::string_view;
    static auto _parsePath(std::string_view) -> std::string_view;
    static auto _parsePort(std::string_view) -> std::optional<uint16_t>;
    static auto _isSafeString(std::string_view) -> bool;
    static auto _isSafeChar(char) -> bool;

    std::string mScheme; //< The scheme of the url
    std::string mHost;   //< The host of the url
    std::optional<uint16_t> mPort; //< The port of the url
    std::string mPath;   //< The path of the url
    std::string mQuery;  //< The query of the url
};

inline Url::Url(const char *str) : Url(std::string_view(str)) { }
inline Url::Url(const Url &) = default;
inline Url::Url(Url &&url) = default;
inline Url::Url() = default;
inline Url::~Url() = default;
inline auto Url::operator =(const Url &) -> Url & = default;
inline auto Url::operator =(Url &&) -> Url & = default;
inline auto Url::operator <=>(const Url &other) const noexcept = default;

inline Url::Url(std::string_view str) : 
    mScheme(_parseScheme(str)), 
    mHost(_parseHost(str)), 
    mPort(_parsePort(str)),
    mPath(_parsePath(str)), 
    mQuery(_parseQuery(str))
{

}

inline auto Url::empty() const -> bool {
    return mScheme.empty() && mHost.empty() && mPath.empty() && mQuery.empty();
}

inline auto Url::isValid() const -> bool {
    if (empty() ||
        !_isSafeString(mScheme) ||
        !_isSafeString(mHost)
    ) {
        return false;
    }
    auto p = path();
    if (!p.empty() && p[0] == '/') {
        // Because path starts with '/'
        return _isSafeString(p.substr(1));
    }
    return _isSafeString(p);
}

inline auto Url::scheme() const -> std::string_view {
    return mScheme;
}

inline auto Url::host() const -> std::string_view {
    return mHost;
}

inline auto Url::port() const -> std::optional<uint16_t> {
    return mPort;
}

inline auto Url::portOrScheme() const -> std::optional<uint16_t> {
    if (!mPort) {
        auto s = scheme();
        if (s == "http") {
            return 80;
        }
        else if (s == "https") {
            return 443;
        }
        else if (s == "ftp") {
            return 21;
        }
        else if (s == "gopher") {
            return 70;
        }
        else if (s == "ws") {
            return 80;
        }
        else if (s == "wss") {
            return 443;
        }
        return std::nullopt;
    }
    return mPort;
}

inline auto Url::query() const -> std::string_view {
    return mQuery;
}

inline auto Url::path() const -> std::string_view {
    if (mPath.empty()) {
        return "/";
    }
    return mPath;
}

// Set
inline auto Url::setScheme(std::string_view scheme) -> void {
    mScheme = scheme;
}

inline auto Url::setHost(std::string_view host) -> void {
    mHost = host;
}

inline auto Url::setPort(std::optional<uint16_t> port) -> void {
    mPort = port;
}

inline auto Url::setPath(std::string_view path) -> void {
    mPath = path;
}

inline auto Url::setQuery(std::string_view query) -> void {
    mQuery = query;
}

inline auto Url::toString() const -> std::string {
    std::string output;
    if (!mScheme.empty()) {
        output += mScheme;
        output += "://";
    }
    if (!mHost.empty()) {
        output += mHost;
    }
    if (mPort) {
        output += ":" + std::to_string(mPort.value());
    }
    if (!mPath.empty()) {
        output += mPath;
    }
    if (!mQuery.empty()) {
        output += "?" + mQuery;
    }
    return output;
}

// Parse Url
inline auto Url::_parseScheme(std::string_view sv) -> std::string_view {
    auto pos = sv.find("://");
    if (pos == std::string_view::npos) {
        return std::string_view();
    }
    return sv.substr(0, pos);
}

inline auto Url::_parseHost(std::string_view sv) -> std::string_view {
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

inline auto Url::_parsePort(std::string_view sv) -> std::optional<uint16_t> {
    // Try skip :// if has
    if (auto pos = sv.find("://"); pos != sv.npos) {
        sv = sv.substr(pos + 3);
    }

    // Try find port : and / end
    auto pos1 = sv.find(":");
    if (pos1 == sv.npos) {
        return std::nullopt;
    }
    auto port = sv.substr(pos1 + 1);
    // Try find : end /
    auto pos2 = port.find("/");
    if (pos2 != port.npos) {
        port = port.substr(0, pos2);
    }
    uint16_t result = 0; //< Port is 16 bits
    if (std::from_chars(port.data(), port.data() + port.size(), result).ec != std::errc()) {
        // Invliad String
        return std::nullopt;
    }
    return result;
}

inline auto Url::_parsePath(std::string_view sv) -> std::string_view {
    auto pos = sv.find("://");
    if (pos != sv.npos) {
        sv = sv.substr(pos + 3);
    }
    pos = sv.find("/");
    if (pos == sv.npos) {
        return {};
    }
    sv = sv.substr(pos);
    pos = sv.find("?");
    if (pos != sv.npos) {
        sv = sv.substr(0, pos);
    }
    return sv;
}

inline auto Url::_parseQuery(std::string_view sv) -> std::string_view {
    auto pos = sv.find("?");
    if (pos == sv.npos) {
        return std::string_view();
    }
    return sv.substr(pos + 1);
}


// Encoding check
inline auto Url::_isSafeChar(char ch) -> bool {
    return std::isdigit(ch) || 
        std::isalpha(ch) || 
        ch == '-' || 
        ch == '_' || 
        ch == '.' || 
        ch == '~'
    ;
}
inline auto Url::_isSafeString(std::string_view str) -> bool {
    for (auto iter = str.begin(); iter != str.end(); ) {
        if (!_isSafeChar(*iter)) {
            return false;
        }
        ++iter;
    }
    return true;
}

// Url encoding
inline auto Url::encodeComponent(std::string_view str) -> std::string {
    std::string out;
    for (auto &ch : str) {
        if (_isSafeChar(ch)) {
            out.push_back(ch);
            continue;
        } 
        char buf[2];
        auto [ptr, ec] = std::to_chars(buf, buf + 2, std::bit_cast<uint8_t>(ch), 16); //< For unicode, so cast to u8
        ILIAS_ASSERT(ec == std::errc());
        // Make the char to upper
        buf[0] = std::toupper(buf[0]);
        buf[1] = std::toupper(buf[1]);
        out.append("%");
        out.append(buf, 2);
    }
    return out;
}
inline auto Url::decodeComponent(std::string_view str) -> std::string {
    std::string out;
    out.reserve(str.size());
    for (auto iter = str.data(); iter < str.data() + str.size(); ) {
        auto in = *iter;
        if (in != '%') {
            out.push_back(in);
            ++iter;
            continue;
        }
        ++iter;
        if (iter + 1 >= str.data() + str.size()) {
            return {};
        }
        uint8_t ch;
        auto [ptr, ec] = std::from_chars(iter, iter + 2, ch, 16);
        if (ec != std::errc()) {
            return {};
        }
        out.push_back(std::bit_cast<char>(ch));
        iter += 2;
    }
    return out;
}


ILIAS_NS_END