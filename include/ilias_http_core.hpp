#pragma once

#include "ilias_async.hpp"
#include <memory>
#include <span>

ILIAS_NS_BEGIN

/**
 * @brief Represent a stream to receive the header
 * 
 */
class HttpStream {
public:
    /**
     * @brief Send a request to the server.
     * 
     * @param header The header of the request, GET XXX \r\n headeritems... \r\n
     * @param data The data of the post request. can be null
     * 
     * 
     */
    virtual auto sendRequest(std::string_view header, std::span<const std::byte> data) -> Task<void> = 0;

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
     */
    virtual auto recvHeaders() -> Task<std::string> = 0;
};

/**
 * @brief Represent a physical connection to a server.
 * 
 */
class HttpConnection {
public:
    /**
     * @brief Create a substream in the connection.
     * 
     * @return std::unique_ptr<HttpStream> 
     */
    virtual auto newStream() -> Task<std::unique_ptr<HttpStream> > = 0;
};

ILIAS_NS_END