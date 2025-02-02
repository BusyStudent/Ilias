#pragma once

#include <ilias/http/transfer.hpp>
#include <ilias/http/headers.hpp>
#include <ilias/http/request.hpp>
#include <ilias/task/task.hpp>
#include <ilias/task/spawn.hpp>
#include <ilias/sync/event.hpp>
#include <ilias/io/stream.hpp>
#include <ilias/net/tcp.hpp>
#include <ilias/buffer.hpp>
#include <ilias/log.hpp>
#include <source_location>

#undef min
#undef max

ILIAS_NS_BEGIN

/**
 * @brief Impl the simplest
 * 
 */
class Http1Connection final {
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
     * @return IoTask<std::unique_ptr<HttpStream> > 
     */
    auto newStream() -> IoTask<std::unique_ptr<HttpStream> >;

    /**
     * @brief Shutdown the conenction
     * 
     * @return IoTask<void> 
     */
    auto shutdown() -> IoTask<void>;

    /**
     * @brief Check the connection is closed?
     * 
     * @return true 
     * @return false 
     */
    auto isClosed() const -> bool;

    /**
     * @brief The event for idle, if set, it is idle
     * 
     * @return Event & 
     */
    auto idleEvent() -> Event &;
private:
    /**
     * @brief Notify the connection is closed
     * 
     */
    auto notifyClosed() -> void;

    /**
     * @brief The task used to watcher the mClient peer closed
     * 
     * @return Task<void> 
     */
    auto watchClosed() -> Task<void>;

    BufferedStream<> mClient;
    WaitHandle<void> mHandle; //< The handle of watchClosed()
    Event mIdleEvent; //< The event for idle, if set, the connection is idle
    bool mHasStream = false; //< Has one stream alive?
    bool mClosed = false; //< The the tcp stream closed ?
friend class Http1Stream;
};

/**
 * @brief Impl the http1 protocol
 * 
 */
class Http1Stream final : public HttpStream {
public:
    Http1Stream(Http1Connection *con) : mCon(con) { 
        ILIAS_TRACE("Http1.1", "New stream {}", (void*) this); 
        mCon->mHasStream = true;
        mCon->mIdleEvent.clear();
    }

    Http1Stream(const Http1Stream &) = delete;
    
    ~Http1Stream() { 
        if (!mCon) { //< Already closed by accident
            return;
        }
        if (!mContentEnd) {
            // User did not finish reading the response body
            ILIAS_ERROR("Http1.1", "Stream {} is not finished reading the response body", (void*)this);
            ILIAS_ERROR("Http1.1", "Stream {} was marked to broken", (void*)this);
            mCon->notifyClosed();
        }
        if (!mKeepAlive) {
            mCon->notifyClosed();
        }
        ILIAS_TRACE("Http1.1", "Delete stream {}", (void*)this);
        mCon->mHasStream = false; //< The stream belong to it, destroyed
        mCon->mIdleEvent.set();
        // Start the watch task when not closed
        if (!mCon->mClosed) {
            mCon->mHandle = spawn(&Http1Connection::watchClosed, mCon);
        }
    }

#if !defined(NDEBUG)
    auto returnError(Error err, std::source_location loc = std::source_location::current()) -> Unexpected<Error> {
        mCon->notifyClosed();
        mCon = nullptr;
        ILIAS_ERROR("Http1.1", "Error happened on {}: {} => {}", 
            loc.function_name(), 
            int(loc.line()), 
            err
        );
        return Unexpected(err);
    }
#else
    auto returnError(Error err) -> Unexpected<Error> {
        mCon->notifyClosed();
        mCon = nullptr;
        return Unexpected(err);
    }
#endif // !defined(NDEBUG)

    auto send(std::string_view method, const Url &url, const HttpHeaders &hheaders, std::span<const std::byte> payload) -> IoTask<void> override {
        auto &client = mCon->mClient;
        auto headers = hheaders; //< Copy it
        // Add content length if needeed
        if (!payload.empty()) {
            headers.append(HttpHeaders::ContentLength, std::to_string(payload.size()));
        }
        // Host
        headers.append("Host", url.host());
        
        std::string headersBuf;

        // FORMAT the first line
        auto path = url.path();
        std::string requestString(path);
        if (auto query = url.query(); !query.empty()) {
            requestString += "?";
            requestString += query;
        }

        sprintf(headersBuf, "%.*s %s HTTP/1.1\r\n", int(method.size()), method.data(), requestString.c_str());

        // Then format request headers
        for (const auto &[key, value] : headers) {
            sprintf(headersBuf, "%s: %s\r\n", key.c_str(), value.c_str());
        }
        // Add the end of headers
        headersBuf += "\r\n";

        ILIAS_TRACE("Http1.1", "Send Headers: {}", headersBuf);

        // Send it
        auto val = co_await client.writeAll(makeBuffer(headersBuf));
        if (!val || *val != headersBuf.size()) {
            co_return returnError(val.error_or(Error::ConnectionAborted));
        }
        
        // Send the content if
        if (!payload.empty()) {
            val = co_await client.writeAll(payload);
            if (!val || *val != payload.size()) {
                co_return returnError(val.error_or(Error::ConnectionAborted));
            }
        }
        mHeaderSent = true;
        mMethodHead = (method == "HEAD");
        ILIAS_TRACE("Http1.1", "Send Request Successfully");
        co_return {};
    }

    auto read(std::span<std::byte> buffer) -> IoTask<size_t> override {
        ILIAS_ASSERT(mHeaderSent && mHeaderReceived);
        if (mContentEnd) {
            co_return 0;
        }
        // Using the content length
        if (mContentLength) {
            auto num = co_await mCon->mClient.read(buffer.subspan(0, std::min(buffer.size(), *mContentLength)));
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
            auto num = co_await mCon->mClient.readAll(buffer);
            if (num && *num == 0) {
                mContentEnd = true;
            }
            co_return num;
        }
        // Chunked
        if (!mChunkSize) [[unlikely]] { //< We didn't get the first chunk size
            ILIAS_TRACE("Http1.1", "Try Get the first chunk size");
            if (auto ret = co_await readChunkSize(); !ret) {
                co_return returnError(ret.error());
            }
        }
        // Read the chunk
        auto num = co_await mCon->mClient.readAll(buffer.subspan(0, std::min(buffer.size(), mChunkRemain)));
        if (num) {
            mChunkRemain -= *num;
        }
        if (mChunkRemain == 0) {
            ILIAS_TRACE("Http1.1", "Current chunk was all read = {}", *mChunkSize);
            // Drop the \r\n, Every chunk end is \r\n
            auto str = co_await mCon->mClient.getline("\r\n");
            if (!str || !str->empty()) {
                co_return returnError(str.error_or(Error::HttpBadReply));
            }
            // Try Get the next chunk size
            if (auto ret = co_await readChunkSize(); !ret) {
                co_return returnError(ret.error());
            }
            if (mChunkSize == 0) { //< Discard last chunk \r\n
                str = co_await mCon->mClient.getline("\r\n");
                if (!str || !str->empty()) {
                    co_return returnError(str.error_or(Error::HttpBadReply));
                }
                ILIAS_TRACE("Http1.1", "All chunks were read");
                mContentEnd = true;
            }
        }
        co_return num;
    }

    auto readChunkSize() -> IoTask<void> {
        auto line = co_await mCon->mClient.getline("\r\n");
        if (!line || line->empty()) {
            co_return returnError(line.error_or(Error::HttpBadReply));
        }
        size_t size = 0;
        auto [ptr, ec] = std::from_chars(line->data(), line->data() + line->size(), size, 16);
        if (ec != std::errc{}) {
            co_return returnError(Error::HttpBadReply);
        }
        ILIAS_TRACE("Http1.1", "Reach new chunk, size = {}", size);
        mChunkSize = size;
        mChunkRemain = size;
        co_return {};
    }

    auto readHeaders(int &statusCode, std::string &statusMessage, HttpHeaders &headers) -> IoTask<void> override {
        ILIAS_ASSERT(mHeaderSent && !mHeaderReceived);
        ILIAS_TRACE("Http1.1", "Recv header Begin");
        
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
        
        ILIAS_TRACE("Http1.1", "Recv header > {}", *line);

        // Seek to the status message
        lineView = lineView.substr(lineView.find(' ') + 1);
        statusMessage = lineView;

        // While recv an empty line
        // Collect the headers
        while (true) {
            line = co_await client.getline("\r\n");
            if (!line) {
                co_return returnError(line.error());
            }
            ILIAS_TRACE("Http1.1", "Recv header > {}", *line);
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
        ILIAS_TRACE("Http1.1", "Recv header End");

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
        else if (mKeepAlive && !mMethodHead) { //< If keep alive and also no content length, and not head, ill-formed
            co_return returnError(Error::HttpBadReply);
        }

        // Check if the method is HEAD
        if (mMethodHead) {
            mContentEnd = true;
        }

        // Done
        mHeaderReceived = true;
        co_return {};
    }

private:
    auto sprintf(std::string &buf, const char *fmt, ...) -> void;

    Http1Connection *mCon;
    bool mMethodHead = false; //< If the method is HEAD, we should not recv the body
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

inline auto Http1Stream::sprintf(std::string &buf, const char *fmt, ...) -> void {
    va_list varg;
    int s;
    
    va_start(varg, fmt);
#ifdef _WIN32
    s = ::_vscprintf(fmt, varg);
#else
    s = ::vsnprintf(nullptr, 0, fmt, varg);
#endif // define _WIN32
    va_end(varg);

    int len = buf.length();
    buf.resize(len + s);

    va_start(varg, fmt);
    ::vsprintf(buf.data() + len, fmt, varg);
    va_end(varg);
}

inline Http1Connection::~Http1Connection() {
    ILIAS_ASSERT(!mHasStream);
    if (mHandle) {
        mHandle.cancel();
        mHandle.wait();
    }
}

inline auto Http1Connection::newStream() -> IoTask<std::unique_ptr<HttpStream> > {
    if (mClosed) {
        co_return Unexpected(Error::ConnectionAborted);
    }
    if (mHandle) { //< Cancel the watch task
        mHandle.cancel();
        mHandle.wait();
    }
    ILIAS_ASSERT(!mHasStream);
    co_return std::make_unique<Http1Stream>(this);
}

inline auto Http1Connection::shutdown() -> IoTask<void> {
    return mClient.shutdown();
}

inline auto Http1Connection::isClosed() const -> bool {
    return mClosed;
}

inline auto Http1Connection::idleEvent() -> Event & {
    return mIdleEvent;
}

inline auto Http1Connection::notifyClosed() -> void {
    if (mClosed) {
        return;
    }
    mClosed = true;
    mHasStream = false;
    mIdleEvent.set();
}

inline auto Http1Connection::watchClosed() -> Task<void> {
    std::byte buf[1];
    auto ret = co_await mClient.read(buf);
    if (!ret && ret.error() == Error::Canceled) {
        // Normal way, the connection class cancel the watch
        ILIAS_TRACE("Http1.1", "The watch of connection {} is canceled", (void*) this);
        co_return;
    }
    if (!ret) {
        ILIAS_INFO("Http1.1", "Connection {} has an error on it {}", (void*) this, ret.error());
    }
    else {
        ILIAS_INFO("Http1.1", "Connection {} got {} bytes, {}", (void*) this, *ret, *ret == 0 ? "EOF" : "Unexpected");
    }
    notifyClosed();
    co_return;
}

ILIAS_NS_END