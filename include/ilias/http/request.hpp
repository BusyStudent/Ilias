#pragma once

#include <ilias/http/headers.hpp>
#include <ilias/url.hpp>
#include <chrono>

ILIAS_NS_BEGIN

/**
 * @brief The HttpRequest class 
 * 
 */
class HttpRequest {
public:
    using WellKnownHeader = HttpHeaders::WellKnownHeader;

    HttpRequest();
    HttpRequest(const char *url);
    HttpRequest(std::string_view url);
    HttpRequest(const Url &url);
    HttpRequest(const HttpRequest &) = default;
    ~HttpRequest();

    /**
     * @brief Set the Header object
     * 
     * @param key The header name
     * @param value The header value
     */
    auto setHeader(std::string_view key, std::string_view value) -> void;

    /**
     * @brief Set the Header object
     * 
     * @param header The header name in WellKnownHeader enum
     * @param value The header value
     */
    auto setHeader(WellKnownHeader header, std::string_view value) -> void;

    /**
     * @brief Set the Url object
     * 
     * @param url The valid url
     */
    auto setUrl(const Url &url) -> void;

    /**
     * @brief Set the Stream Mode object
     * 
     * @param streamMode If true, the session won't download content immediately
     */
    auto setStreamMode(bool streamMode) -> void;

    /**
     * @brief Set the Maximum Redirects object
     * 
     * @param maximumRedirects How many redirects are allowed
     */
    auto setMaximumRedirects(int maximumRedirects) -> void;

    /**
     * @brief Set the Transfer Timeout object
     * 
     * @param transferTimeout The transfer timeout (if timeout is reached, the transfer is aborted)
     */
    auto setTransferTimeout(std::chrono::milliseconds transferTimeout) -> void;

    /**
     * @brief Get the Header object
     * 
     * @param key The header name
     * @return std::string_view 
     */
    auto header(std::string_view key) const -> std::string_view;

    /**
     * @brief Get the Header object
     * 
     * @param header The header name in WellKnownHeader enum
     * @return std::string_view 
     */
    auto header(WellKnownHeader header) const -> std::string_view;

    /**
     * @brief Get the Headers object
     * 
     * @return const HttpHeaders& 
     */
    auto headers() const -> const HttpHeaders &;

    /**
     * @brief Get the Url object
     * 
     * @return const Url& 
     */
    auto url() const -> const Url &;

    /**
     * @brief Get the Stream Mode object
     * 
     * @return true 
     * @return false 
     */
    auto streamMode() const -> bool;

    /**
     * @brief Get the Maximum Redirects object
     * 
     * @return int 
     */
    auto maximumRedirects() const -> int;

    /**
     * @brief Get the Transfer Timeout object
     * 
     * @return std::chrono::milliseconds 
     */
    auto transferTimeout() const -> std::chrono::milliseconds;
private:
    Url mUrl;
    HttpHeaders mHeaders;
    int  mMaximumRedirects = 10;
    bool mStreamMode = false;
    std::chrono::milliseconds mTransferTimeout = std::chrono::seconds(5);
};

inline HttpRequest::HttpRequest() = default;
inline HttpRequest::HttpRequest(const Url &url) : mUrl(url) { }
inline HttpRequest::HttpRequest(const char *url) : mUrl(url) { }
inline HttpRequest::HttpRequest(std::string_view url) : mUrl(url) { }
inline HttpRequest::~HttpRequest() = default;

inline auto HttpRequest::setHeader(std::string_view key, std::string_view value) -> void {
    mHeaders.append(key, value);
}

inline auto HttpRequest::setHeader(WellKnownHeader header, std::string_view value) -> void {
    mHeaders.append(header, value);
}

inline auto HttpRequest::setUrl(const Url &url) -> void {
    mUrl = url;
}

inline auto HttpRequest::setStreamMode(bool streamMode) -> void {
    mStreamMode = streamMode;
}

inline auto HttpRequest::setMaximumRedirects(int maximumRedirects) -> void {
    mMaximumRedirects = maximumRedirects;
}

inline auto HttpRequest::setTransferTimeout(std::chrono::milliseconds transferTimeout) -> void {
    mTransferTimeout = transferTimeout;
}

inline auto HttpRequest::header(std::string_view key) const -> std::string_view {
    return mHeaders.value(key);
}

inline auto HttpRequest::header(WellKnownHeader header) const -> std::string_view {
    return mHeaders.value(header);
}

inline auto HttpRequest::url() const -> const Url & {
    return mUrl;
}

inline auto HttpRequest::headers() const -> const HttpHeaders & {
    return mHeaders;
}

inline auto HttpRequest::streamMode() const -> bool {
    return mStreamMode;
}

inline auto HttpRequest::maximumRedirects() const -> int {
    return mMaximumRedirects;
}

inline auto HttpRequest::transferTimeout() const -> std::chrono::milliseconds {
    return mTransferTimeout;
}

ILIAS_NS_END