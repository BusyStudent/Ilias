#pragma once

#include "../inet.hpp"
#include <memory>
#include <span>

ILIAS_NS_BEGIN

class HttpHeaders;
class HttpRequest;

/**
 * @brief Represent a stream to receive the header
 * 
 */
class HttpStream {
public:
    virtual ~HttpStream() = default;

    /**
     * @brief Send a request to the server
     * 
     * @param header The request of it. impl may modify the headers such as add Content-Length
     * @param data The data of the post request. can be null
     * 
     * 
     */
    virtual auto sendRequest(HttpRequest &request, std::span<const std::byte> data) -> Task<void> = 0;

    /**
     * @brief Recv the content, (chunked encoding should was removed in implementation)
     * 
     * @param buf the buffer to store the content in
     * 
     * @return the number of bytes recv'ed (0 on EOF)
     * 
     */
    virtual auto recvContent(std::span<std::byte> buf) -> Task<size_t> = 0;

    /**
     * @brief Receive the header from the server.
     * 
     * @param statusCode the status code of the response
     * @param statusMessage the status message of the response
     * @param headers the headers of the response
     * 
     * @return Task<void> 
     */
    virtual auto recvHeaders(int &statusCode, std::string &statusMessage, HttpHeaders &headers) -> Task<void> = 0;
};

/**
 * @brief Represent a physical connection to a server.
 * 
 */
class HttpConnection {
public:
    virtual ~HttpConnection() = default;

    /**
     * @brief Create a substream in the connection.
     * 
     * @return std::unique_ptr<HttpStream> 
     */
    virtual auto newStream() -> Task<std::unique_ptr<HttpStream> > = 0;

    /**
     * @brief Gracefully shutdown the connection.
     * 
     * @return Task<void> 
     */
    virtual auto shutdown() -> Task<void> = 0;

    /**
     * @brief Get the number of active streams. (0 means no active stream)
     *
     * 
     * @return size_t 
     */
    virtual auto activeStreams() const -> size_t = 0;

    /**
     * @brief Check the connection is broken.
     * 
     * @return true 
     * @return false 
     */
    virtual auto isBroken() const -> bool = 0;
};

ILIAS_NS_END