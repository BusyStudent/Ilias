#pragma once

#include <ilias/detail/functional.hpp>
#include <ilias/http/headers.hpp>
#include <ilias/url.hpp>
#include <memory>
#include <span>

ILIAS_NS_BEGIN

/**
 * @brief The Http abstract Stream (per request)
 * 
 */
class HttpStream {
public:
    virtual ~HttpStream() = default;

    /**
     * @brief Send the request on the stream (only can be called once)
     * 
     * @param method GET, POST, PUT, DELETE, etc...
     * @param url The url of the request
     * @param headers The headers of the request
     * @param payload 
     * @return IoTask<void> 
     */
    virtual auto send(std::string_view method, const Url &url, const HttpHeaders &headers, std::span<const std::byte> payload) -> IoTask<void> = 0;

    /**
     * @brief Read the body of the request (must be called after readHeaders)
     * 
     * @param buffer 
     * @return IoTask<size_t> 
     */
    virtual auto read(std::span<std::byte> buffer) -> IoTask<size_t> = 0;

    /**
     * @brief Read the headers of the request (must be called before read)
     * 
     * @param statusCode 
     * @param status 
     * @param headers 
     * @return IoTask<void> 
     */
    virtual auto readHeaders(int &statusCode, std::string &statusMessage, HttpHeaders &headers) -> IoTask<void> = 0;
};

ILIAS_NS_END