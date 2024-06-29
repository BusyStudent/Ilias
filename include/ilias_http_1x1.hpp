#pragma once

#include "ilias_http_core.hpp"
#include "ilias_mutex.hpp"

ILIAS_NS_BEGIN

class Http1Connection;
class Http1Stream;


/**
 * @brief Impl the simplest
 * 
 */
class Http1Connection final : public HttpConnection {
public:
    Http1Connection() = delete;
    Http1Connection(const Http1Connection &) = delete;
    ~Http1Connection() = default;

    /**
     * @brief Create a new http stream on a physical connection
     * 
     * @return Task<std::unique_ptr<HttpStream> > 
     */
    auto newStream() -> Task<std::unique_ptr<HttpStream> > override;

    /**
     * @brief Create a Http1 Connection from a IStreamClient
     * 
     * @param client 
     * @return std::unique_ptr<Http1Connection> 
     */
    static auto make(IStreamClient &&client) -> std::unique_ptr<Http1Connection>;
private:
    Http1Connection(IStreamClient &&client) : mClient(std::move(client)) { }

    IStreamClient mClient;
    Mutex mMutex; //< For Http1 keep-alive, at one time, only a single request can be processed
    bool mBroken = false; //< False on physical connection close
friend class Http1Stream;
};

/**
 * @brief Impl the http1 protocol
 * 
 */
class Http1Stream final : public HttpStream {
public:
    ~Http1Stream() { mCon->mMutex.unlock(); }

    auto sendRequest(std::string_view header, std::span<const std::byte> data) -> Task<void> override {
        auto &client = mCon->mClient;
    }

    auto recvContent(std::span<std::byte> data) -> Task<size_t> override {

    }

    auto recvHeaders() -> Task<std::string> override {

    }
private:
    Http1Stream(Http1Connection *con) : mCon(con) { }

    Http1Connection *mCon;
friend class Http1Connection;
};

inline auto Http1Connection::newStream() -> Task<std::unique_ptr<HttpStream> > {
    if (mBroken) {
        co_return Unexpected(Error::ConnectionAborted);
    }
    auto val = co_await mMutex.lock();
    if (!val) {
        co_return Unexpected(val.error());
    }
    co_return std::make_unique<Http1Stream>(this);
}

inline auto Http1Connection::make(IStreamClient &&client) -> std::unique_ptr<Http1Connection> {
    return std::make_unique<Http1Connection>(std::move(client));
}

ILIAS_NS_END