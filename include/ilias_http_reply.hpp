#pragma once

#include "ilias_http_headers.hpp"
#include "ilias_async.hpp"

ILIAS_NS_BEGIN

/**
 * @brief Reply class
 * 
 */
class HttpReply {
public:
    HttpReply(const HttpReply &) = delete;
    HttpReply(HttpReply &&);
    ~HttpReply();
    
    auto text() -> Task<std::string>;
    auto content() -> Task<std::vector<uint8_t> >;
    auto statusCode() const -> int;
    auto status() const -> std::string_view;
    auto headers() const -> const HttpHeaders &;
    auto transferDuration() const -> std::chrono::milliseconds;
    auto operator =(const HttpReply &) -> HttpReply & = delete;
    auto operator =(HttpReply &&) -> HttpReply &;
private:
    HttpReply();

    Url mUrl;
    int mStatusCode = 0;
    std::string mContent;
    std::string mStatus;
    HttpHeaders mRequestHeaders;
    HttpHeaders mResponseHeaders;
    std::chrono::milliseconds mTransferDuration;
friend class HttpSession;
};

ILIAS_NS_END