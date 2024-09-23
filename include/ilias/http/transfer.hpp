#pragma once

#include <ilias/detail/functional.hpp>
#include <ilias/http/headers.hpp>
#include <ilias/url.hpp>
#include <span>
#include <memory>

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
     * @return Task<void> 
     */
    virtual auto send(std::string_view method, const Url &url, const HttpHeaders &headers, std::span<const std::byte> payload) -> Task<void> = 0;

    /**
     * @brief Read the body of the request (must be called after readHeaders)
     * 
     * @param buffer 
     * @return Task<size_t> 
     */
    virtual auto read(std::span<std::byte> buffer) -> Task<size_t> = 0;

    /**
     * @brief Read the headers of the request (must be called before read)
     * 
     * @param statusCode 
     * @param status 
     * @param headers 
     * @return Task<void> 
     */
    virtual auto readHeaders(int &statusCode, std::string &statusMessage, HttpHeaders &headers) -> Task<void> = 0;
};


/**
 * @brief The Http abstract Connection (per client)
 * 
 */
class HttpConnection {
public:
    enum Version {
        Http1_1 = 1,
        Http2   = 2,
        Http3   = 3
    };
    
    virtual ~HttpConnection() = default;

    /**
     * @brief Create a new stream for the connection
     * 
     * @return Task<std::unique_ptr<HttpStream> > 
     */
    virtual auto newStream() -> Task<std::unique_ptr<HttpStream> > = 0;

    /**
     * @brief Perform the graceful shutdown of the connection
     * 
     * @return Task<void> 
     */
    virtual auto shutdown() -> Task<void> = 0;

    /**
     * @brief Get the implmentation of the http version
     * 
     * @return auto 
     */
    auto version() const { return mVersion; }

    /**
     * @brief Check current connection is no stream is currently being processed
     * 
     * @return auto 
     */
    auto isIdle() const { return mIdle; }

    /**
     * @brief Check if the connection is closed (we can't use it anymore)
     * 
     * @return auto 
     */
    auto isClosed() const { return mClosed; }

    /**
     * @brief Set the handler to be called when the connection is broken (network error)
     * 
     * @param handler 
     * @return auto 
     */
    auto setBrokenHandler(detail::MoveOnlyFunction<void()> handler) { mOnBroken = std::move(handler); }
protected:
    HttpConnection(Version version) : mVersion(version) { }

    /**
     * @brief Set the Idle object
     * 
     * @param idle 
     * @return auto 
     */
    auto setIdle(bool idle) { mIdle = idle; }

    /**
     * @brief Set the Closed object
     * 
     * @return auto 
     */
    auto setClosed() { mClosed = true; }

    /**
     * @brief Set the connection as broken and call the handler
     * 
     * @return auto 
     */
    auto setBroken() { mClosed = true; return mOnBroken(); }

    /**
     * @brief Set the connection as broken but don't call the handler
     * 
     * @return auto 
     */
    auto markBroken() { mClosed = true; }
private:
    Version mVersion;
    bool    mIdle = true;
    bool    mClosed = false;
    detail::MoveOnlyFunction<void()> mOnBroken; 
};

ILIAS_NS_END