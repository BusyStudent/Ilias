#pragma once

#include "../coro/mutex.hpp"
#include "../net/stream.hpp"
#include "headers.hpp"
#include "request.hpp"
#include "transfer.hpp"

#undef min
#undef max

ILIAS_NS_BEGIN

class Http1Connection;
class Http1Stream;


/**
 * @brief Impl the simplest
 * 
 */
class Http1Connection final : public HttpConnection {
public:
    Http1Connection(const Http1Connection &) = delete;
    ~Http1Connection();

    /**
     * @brief Construct a new Http 1 Connection object
     * 
     * @param client 
     */
    Http1Connection(BufferedStream<> &&client) : mClient(std::move(client)) { }

    /**
     * @brief Create a new http stream on a physical connection
     * 
     * @return Task<std::unique_ptr<HttpStream> > 
     */
    auto newStream() -> Task<std::unique_ptr<HttpStream> > override;

    /**
     * @brief Get num of active streams
     * 
     * @return size_t 
     */
    auto activeStreams() const -> size_t override;

    /**
     * @brief Check the connection is broken
     * 
     * @return true 
     * @return false 
     */
    auto isBroken() const -> bool override;

    auto shutdown() -> Task<void> override;

    /**
     * @brief Create a Http1 Connection from a IStreamClient
     * 
     * @param client 
     * @return std::unique_ptr<Http1Connection> 
     */
    static auto make(IStreamClient &&client) -> std::unique_ptr<Http1Connection>;
private:
    /**
     * @brief A Task will be started on idle, to check if the connection is broken
     * 
     * @return Task<> 
     */
    auto _waitBroken() -> Task<>;

    BufferedStream<> mClient;
    Mutex mMutex; //< For Http1 keep-alive, at one time, only a single request can be processed
    bool mBroken = false; //< False on physical connection close
    size_t mNumOfStream = 0; //< Number of Http1Stream alived
    JoinHandle<void> mHandle; //< The handle of _waitBroken()
friend class Http1Stream;
};

/**
 * @brief Impl the http1 protocol
 * 
 */
class Http1Stream final : public HttpStream {
public:
    Http1Stream(Http1Connection *con) : mCon(con) { log("[Http1.1] New stream %p\n", this); }
    Http1Stream(const Http1Stream &) = delete;
    ~Http1Stream() { 
        if (!mContentEnd && !mCon->mBroken) {
            // User did not finish reading the response body
            log("[Http1.1] Stream %p is not finished reading the response body\n", this);
            log("[Http1.1] Stream %p was marked to broken\n", this);
            mCon->mBroken = true;
        }
        if (!mKeepAlive) {
            mCon->mBroken = true;
        }
        log("[Http1.1] Delete stream %p\n", this);
        mCon->mMutex.unlock(); 
    }

#if !defined(NDEBUG)
    auto returnError(Error err, std::source_location loc = std::source_location::current()) -> Unexpected<Error> {
        mCon->mBroken = true;
        log("[Http1.1] Error happended on %s: %d => %s\n", 
            loc.function_name(), 
            int(loc.line()), 
            err.toString().c_str()
        );
        return Unexpected(err);
    }

    auto log(const char *fmt, ...) -> void {
        va_list args;
        va_start(args, fmt);
        ::vfprintf(stderr, fmt, args);
        va_end(args);
    }
#else
    auto returnError(Error err) -> Unexpected<Error> {
        mCon->mBroken = true;
        return Unexpected(err);
    }

    auto log(const char *fmt, ...) -> void { }
#endif

    auto sendRequest(HttpRequest &request, std::span<const std::byte> data) -> Task<void> override {
        auto &client = mCon->mClient;
        // Add content length if needeed
        if (!data.empty()) {
            request.setHeader(HttpHeaders::ContentLength, std::to_string(data.size()));
        }
        // Host
        request.setHeader("Host", request.url().host());
        
        std::string headers;

        // FORMAT the first line
        const char *operation = nullptr;
        switch (request.operation()) {
            case HttpRequest::GET: operation = "GET"; break;
            case HttpRequest::POST: operation = "POST"; break;
            case HttpRequest::PUT: operation = "PUT"; break;
            case HttpRequest::HEAD: operation = "HEAD"; break;
            default: ::abort();
        }
        auto &url = request.url();
        auto path = url.path();
        std::string requestString(path);
        if (auto query = url.query(); !query.empty()) {
            requestString += "?";
            requestString += query;
        }

        _sprintf(headers, "%s %s HTTP/1.1\r\n", operation, requestString.c_str());

        // Then format request headers
        for (const auto &[key, value] : request.headers()) {
            _sprintf(headers, "%s: %s\r\n", key.c_str(), value.c_str());
        }
        // Add the end of headers
        headers += "\r\n";

        log("[Http1.1] Send Headers: \n%s", headers.c_str());

        // Send it
        auto val = co_await client.sendAll(headers.data(), headers.size());
        if (!val || *val != headers.size()) {
            co_return returnError(val.error_or(Error::ConnectionAborted));
        }
        
        // Send the content if
        if (!data.empty()) {
            val = co_await client.sendAll(data.data(), data.size());
            if (!val || *val != data.size()) {
                co_return returnError(val.error_or(Error::ConnectionAborted));
            }
        }
        mHeaderSent = true;
        log("[Http1.1] Send Request Successfully\n");
        co_return {};
    }

    auto recvContent(std::span<std::byte> data) -> Task<size_t> override {
        ILIAS_ASSERT(mHeaderSent && mHeaderReceived);
        if (mContentEnd) {
            co_return 0;
        }
        // Using the content length
        if (mContentLength) {
            auto num = co_await mCon->mClient.recvAll(data.data(), std::min(data.size(), *mContentLength));
            if (num) {
                *mContentLength -= *num;
            }
            if (*mContentLength == 0) {
                mContentEnd = true;
            }
            co_return num;
        }
        // Not Chunked
        if (!mChunked) {
            // Read until eof
            auto num = co_await mCon->mClient.recvAll(data);
            if (num && *num == 0) {
                mContentEnd = true;
            }
            co_return num;
        }
        // Chunked
        if (!mChunkSize) {
            // Oh, we did not receive the chunk size
            auto line = co_await mCon->mClient.getline("\r\n");
            if (!line || line->empty()) {
                co_return returnError(line.error_or(Error::HttpBadReply));
            }
            size_t size = 0;
            auto [ptr, ec] = std::from_chars(line->data(), line->data() + line->size(), size, 16);
            if (ec != std::errc{}) {
                co_return returnError(Error::HttpBadReply);
            }
            log("[Http1.1] Reach new chunk, size = %zu\n", size);
            mChunkSize = size;
            mChunkRemain = size;
        }
        // Read the chunk
        auto num = co_await mCon->mClient.recvAll(data.data(), std::min(data.size(), mChunkRemain));
        if (num) {
            mChunkRemain -= *num;
        }
        if (mChunkRemain == 0) {
            log("[Http1.1] Current chunk was all readed = %zu\n", *mChunkSize);
            // Drop the \r\n, Every chunk end is \r\n
            auto str = co_await mCon->mClient.getline("\r\n");
            if (mChunkSize == 0 && str) {
                log("[Http1.1] All chunk was readed\n");
                mContentEnd = true;
            }
            mChunkSize = std::nullopt;
        }
        co_return num;
    }

    auto recvHeaders(int &statusCode, std::string &statusMessage, HttpHeaders &headers) -> Task<void> override {
        ILIAS_ASSERT(mHeaderSent && !mHeaderReceived);
        log("[Http1.1] Recv header Begin\n");
        
        auto &client = mCon->mClient;
        // Recv the headers 
        auto line = co_await client.getline("\r\n");
        if (!line || line->empty()) {
            co_return returnError(line.error_or(Error::HttpBadReply));
        }

        std::string_view lineView(*line);
        // Seek to first space
        lineView = lineView.substr(lineView.find(' ') + 1);

        // Begin parse code
        auto [ptr, ec] = std::from_chars(lineView.data(), lineView.data() + lineView.size(), statusCode);
        if (ec != std::errc{}) {
            co_return returnError(Error::HttpBadReply);
        }
        
        log("[Http1.1] Recv header > %s\n", line->c_str());

        // Seek to the status message
        lineView = lineView.substr(lineView.find(' ') + 1);
        statusMessage = lineView;

        // While recv an empty line
        // Collect the hesders
        while (true) {
            line = co_await client.getline("\r\n");
            if (!line) {
                co_return returnError(line.error());
            }
            log("[Http1.1] Recv header > %s\n", line->c_str());
            if (line->empty()) {
                break;
            }
            // Try split the k v by ': '
            lineView = std::string_view(*line);
            size_t skip = 2;
            size_t delim = lineView.find(": ");
            if (delim == lineView.npos) {
                delim = lineView.find(':');
                skip = 1;
            }
            if (delim == lineView.npos) {
                // Still not found
                co_return returnError(Error::HttpBadReply);
            }
            auto key = lineView.substr(0, delim);
            auto value = lineView.substr(delim + skip);
            headers.append(key, value);
        }
        log("[Http1.1] Recv header End\n");

        // Check keep alive
        mKeepAlive = headers.value(HttpHeaders::Connection) == "keep-alive";

        // Check transfer encoding
        auto contentLength = headers.value(HttpHeaders::ContentLength);
        auto transferEncoding = headers.value(HttpHeaders::TransferEncoding);
        if (!contentLength.empty()) {
            size_t len;
            auto [ptr, ec] = std::from_chars(contentLength.data(), contentLength.data() + contentLength.size(), len);
            if (ec != std::errc{}) {
                co_return Unexpected(Error::HttpBadReply);
            }
            mContentLength = len;
        }
        else if (transferEncoding == "chunked") {
            mChunked = true;
        }
        else if (mKeepAlive) { //< If keep alive and also no content length, ill-formed
            co_return returnError(Error::HttpBadReply);
        }

        // Done
        mHeaderReceived = true;
        co_return {};
    }

private:
    auto _sprintf(std::string &buf, const char *fmt, ...) -> void;

    Http1Connection *mCon;
    bool mHeaderSent = false;
    bool mHeaderReceived = false;
    bool mContentEnd = false;
    bool mKeepAlive = false;
    bool mChunked = false;
    std::optional<size_t> mContentLength;

    // Used for chunked mode
    std::optional<size_t> mChunkSize; //< Current Chunksize
    size_t mChunkRemain = 0; //< How many bytes remain in the current chunk
friend class Http1Connection;
};

inline auto Http1Stream::_sprintf(std::string &buf, const char *fmt, ...) -> void {
    va_list varg;
    int s;
    
    va_start(varg, fmt);
#ifdef _WIN32
    s = ::_vscprintf(fmt, varg);
#else
    s = ::vsnprintf(nullptr, 0, fmt, varg);
#endif
    va_end(varg);

    int len = buf.length();
    buf.resize(len + s);

    va_start(varg, fmt);
    ::vsprintf(buf.data() + len, fmt, varg);
    va_end(varg);
}

inline Http1Connection::~Http1Connection() {
    ILIAS_ASSERT(mNumOfStream == 0);
    // if (mHandle) {
    //     mHandle.cancel();
    //     mHandle.join();
    // }
}

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

inline auto Http1Connection::activeStreams() const -> size_t {
    return mNumOfStream;
}

inline auto Http1Connection::isBroken() const -> bool {
    return mBroken;
}

inline auto Http1Connection::shutdown() -> Task<void> {
    ILIAS_ASSERT(mNumOfStream == 0);
    auto guard = co_await mMutex.lockGuard();
    if (guard) {
        co_return co_await mClient.shutdown();
    }
    co_return Unexpected(guard.error());
}

inline auto Http1Connection::make(IStreamClient &&client) -> std::unique_ptr<Http1Connection> {
    return std::make_unique<Http1Connection>(std::move(client));
}

ILIAS_NS_END