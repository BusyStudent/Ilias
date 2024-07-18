#pragma once

#include "headers.hpp"
#include "../url.hpp"
#include <chrono>

ILIAS_NS_BEGIN

/**
 * @brief Request
 * 
 */
class HttpRequest {
public:
    using WellKnownHeader = HttpHeaders::WellKnownHeader;
    using enum HttpHeaders::WellKnownHeader;

    enum Operation {
        GET,
        PUT,
        POST,
        HEAD
        // DELETE
    };

    HttpRequest();
    HttpRequest(const char *url);
    HttpRequest(const Url &url);
    HttpRequest(const HttpRequest &) = default;
    ~HttpRequest();

    auto setHeader(std::string_view key, std::string_view value) -> void;
    auto setHeader(WellKnownHeader header, std::string_view value) -> void;
    auto setOperation(Operation operation) -> void;
    auto setUrl(const Url &url) -> void;
    auto setMaximumRedirects(int maximumRedirects) -> void;
    auto setTransferTimeout(std::chrono::milliseconds transferTimeout) -> void;
    auto header(std::string_view key) const -> std::string_view;
    auto header(WellKnownHeader header) const -> std::string_view;
    auto headers() const -> const HttpHeaders &;
    auto url() const -> const Url &;
    auto operation() const -> Operation;
    auto maximumRedirects() const -> int;
    auto transferTimeout() const -> std::chrono::milliseconds;
private:
    HttpHeaders mHeaders;
    Operation mOperation { };
    Url mUrl;
    int mMaximumRedirects = 10;
    std::chrono::milliseconds mTransferTimeout = std::chrono::seconds(5);
};

inline HttpRequest::HttpRequest() = default;
inline HttpRequest::HttpRequest(const Url &url) : mUrl(url) { }
inline HttpRequest::HttpRequest(const char *url) : mUrl(url) { }
inline HttpRequest::~HttpRequest() = default;

inline auto HttpRequest::setHeader(std::string_view key, std::string_view value) -> void {
    mHeaders.append(key, value);
}
inline auto HttpRequest::setHeader(WellKnownHeader header, std::string_view value) -> void {
    mHeaders.append(header, value);
}
inline auto HttpRequest::setOperation(Operation operation) -> void {
    mOperation = operation;
}
inline auto HttpRequest::setUrl(const Url &url) -> void {
    mUrl = url;
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
inline auto HttpRequest::operation() const -> Operation {
    return mOperation;
}
inline auto HttpRequest::maximumRedirects() const -> int {
    return mMaximumRedirects;
}
inline auto HttpRequest::transferTimeout() const -> std::chrono::milliseconds {
    return mTransferTimeout;
}

ILIAS_NS_END