#pragma once

#include "ilias_http_headers.hpp"
#include "ilias_http_request.hpp"
#include "ilias_http_zlib.hpp"
#include "ilias_async.hpp"
#include "ilias_url.hpp"
#include "ilias_ssl.hpp"
#include <chrono>
#include <array>
#include <list>

ILIAS_NS_BEGIN

class HttpSession;
class HttpReply;

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

/**
 * @brief A Session for 
 * 
 */
class HttpSession {
public:
    using Operation = HttpRequest::Operation;

    HttpSession(IoContext &ctxt);
    HttpSession(const HttpSession &) = delete;
    HttpSession();
    ~HttpSession();

    auto get(const HttpRequest &request) -> Task<HttpReply>;
    auto post(const HttpRequest &request, std::span<uint8_t> data = {}) -> Task<HttpReply>;
    auto sendRequest(Operation op, HttpRequest request, std::span<uint8_t> extraData = {}) -> Task<HttpReply>;
private:
    /**
     * @brief Connection used tp keep alive
     * 
     */
    struct Connection {
        ByteStream<> client;
        IPEndpoint endpoint;
        std::chrono::steady_clock::time_point lastUsedTime;
        bool cached = false;
    };
    static constexpr auto BufferIncreaseSize = int64_t(4096);

    auto _sendRequest(Operation op, const HttpRequest &request, std::span<uint8_t> extraData = {}) -> Task<HttpReply>;
    auto _readReply(Operation op, const HttpRequest &request, Connection connection) -> Task<HttpReply>;
    auto _readContent(Connection &connection, HttpReply &outReply) -> Task<void>;
    auto _readHeaders(Connection &connection, HttpReply &outReply) -> Task<void>;
    auto _connect(const Url &url) -> Task<Connection>;
    auto _cache(Connection connection) -> void;
    auto _sprintf(std::string &buf, const char *fmt, ...) -> void;

    IoContext &mIoContext;

#if !defined(ILIAS_NO_SSL)
    SslContext mSslContext;
#endif

    // TODO: Improve the cache algorithm
    std::list<Connection> mConnections;
friend class HttpReply;
};

inline HttpSession::HttpSession(IoContext &ctxt) : mIoContext(ctxt) { }
inline HttpSession::HttpSession() : mIoContext(*IoContext::instance()) { }
inline HttpSession::~HttpSession() {

}

inline auto HttpSession::get(const HttpRequest &request) -> Task<HttpReply> {
    return sendRequest(Operation::GET, request);
}
inline auto HttpSession::post(const HttpRequest &request, std::span<uint8_t> data) -> Task<HttpReply> {
    return sendRequest(Operation::POST, request, data);
}
inline auto HttpSession::sendRequest(Operation op, HttpRequest request, std::span<uint8_t> extraData) -> Task<HttpReply> {
    int n = 0;
    while (true) {
        auto reply = co_await _sendRequest(op, request, extraData);
        if (!reply) {
            co_return reply;
        }
        // Check redirect
        constexpr std::array redirectCodes = {
            301, 302, 303, 307, 308
        };
        if (std::find(redirectCodes.begin(), redirectCodes.end(), reply->statusCode())  != redirectCodes.end()) {
            auto newLocation = reply->headers().value(HttpHeaders::Location);
            if (!newLocation.empty() && n < request.maximumRedirects()) {
                ::printf("Redirecting to %s by(%d, %s)\n", newLocation.data(), reply->statusCode(), reply->status().data());
                request.setUrl(newLocation);
                n += 1;
                continue;
            }
        }
        co_return reply;
    } 
}
inline auto HttpSession::_sendRequest(Operation op, const HttpRequest &request, std::span<uint8_t> extraData) -> Task<HttpReply> {
    const auto &url = request.url();
    const auto host = url.host();
    const auto port = url.port();
    const auto path = url.path();
    const auto scheme = url.scheme();

    // Check args
    if (port <= 0 || host.empty() || (scheme != "https" && scheme != "http")) {
        co_return Unexpected(Error::InvalidArgument);
    }
    std::string requestString(path);
    if (auto query = url.query(); !query.empty()) {
        requestString += "?";
        requestString += query;
    }

    // Try peek from cache
while (true) {
    auto now = std::chrono::steady_clock::now();
    auto con = co_await _connect(url);
    if (!con) {
        co_return Unexpected(con.error());
    }
    auto &client = con->client;
    bool fromCache = con->cached;

    // TODO:
    std::string buffer;
    const char *operation = nullptr;
    switch (op) {
        case Operation::GET: operation = "GET"; break;
        case Operation::POST: operation = "POST"; break;
        case Operation::PUT: operation = "PUT"; break;
        // case Operation::DELETE: operation = "DELETE"; break
        default: co_return Unexpected(Error::Unknown); break;
    }
    _sprintf(buffer, "%s %s HTTP/1.1\r\n", operation, requestString.c_str());
    for (const auto &[key, value] : request.headers()) {
        _sprintf(buffer, "%s: %s\r\n", key.c_str(), value.c_str());
    }
    // Add host
    _sprintf(buffer, "Host: %s:%d\r\n", std::string(host).c_str(), int(port));

#if !defined(ILIAS_NO_ZLIB)
    _sprintf(buffer, "Accept-Encoding: gzip, deflate\r\n");
#else
    _sprintf(buffer, "Accept-Encoding: identity\r\n");
#endif
    // Add padding extraData
    if (!extraData.empty()) {
        buffer.append(reinterpret_cast<const char*>(extraData.data()), extraData.size());
    }
    // Add end \r\n
    buffer.append("\r\n");

    // Send this reuqets
    auto sended = co_await client.sendAll(buffer.data(), buffer.size());
    if (!sended) {
        co_return Unexpected(sended.error());
    }
    if (sended.value() != buffer.size()) {
        co_return Unexpected(Error::Unknown);
    }
    auto reply = co_await _readReply(op, request, std::move(*con));
    if (!reply && fromCache) {
        ::printf("ERROR from read reply in cache => %s, try again\n", reply.error().message().c_str());
        continue; //< Try again
    }
    if (reply) {
        reply->mTransferDuration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - now);
    }
    co_return reply;
}

}
inline auto HttpSession::_readReply(Operation op, const HttpRequest &request, Connection connection) -> Task<HttpReply> {
    HttpReply reply;
    reply.mUrl = request.url();
    reply.mRequestHeaders = request.headers();
    // Read the header
    if (auto err = co_await _readHeaders(connection, reply); !err) {
        co_return Unexpected(err.error());
    }
    // Read the body
    if (auto err = co_await _readContent(connection, reply); !err) {
        co_return Unexpected(err.error());
    }

#if !defined(ILIAS_NO_ZLIB)
    auto encoding = reply.mResponseHeaders.value(HttpHeaders::ContentEncoding);
    if (encoding == "gzip") {
        // Decompress gzip
        auto result = Gzip::decompress(reply.mContent);
        if (result.empty()) {
            co_return Unexpected(Error::Unknown);
        }
        reply.mContent = std::move(result);
    }
    else if (encoding == "deflate") {
        // Decompress deflate
        auto result = Deflate::decompress(reply.mContent);
        if (result.empty()) {
            co_return Unexpected(Error::Unknown);
        }
        reply.mContent = std::move(result);
    }
#endif

    if (reply.mResponseHeaders.value(HttpHeaders::Connection) == "keep-alive") {
        _cache(std::move(connection));
    }
    co_return reply;
}
inline auto HttpSession::_readHeaders(Connection &con, HttpReply &reply) -> Task<void> {
    auto &client = con.client;

    // Got headers string
    // Parse the headers
    // Begin with HTTP/1.1 xxx OK
    auto line = co_await client.getline("\r\n");
    if (!line || line->empty()) {
        co_return Unexpected(line.error_or(Error::Unknown));
    }
    // Parse first line
    std::string_view firstLine(*line);
    auto firstSpace = firstLine.find(' ');
    auto secondSpace = firstLine.find(' ', firstSpace + 1);
    if (firstSpace == std::string_view::npos || secondSpace == std::string_view::npos) {
        co_return Unexpected(Error::Unknown);
    }
    auto codeString = firstLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    std::from_chars(codeString.data(), codeString.data() + codeString.size(), reply.mStatusCode);
    reply.mStatus = firstLine.substr(secondSpace + 1);

    // Read all headers
    do {
        line = co_await client.getline("\r\n");
        if (!line) {
            co_return Unexpected(line.error());
        }
        if (line->empty()) {
            break;
        }
        // Split into key and value by : 
        auto delim = line->find(": ");
        if (delim == line->npos) {
            co_return Unexpected(Error::Unknown);
        }
        auto header = line->substr(0, delim);
        auto value = line->substr(delim + 2);
        reply.mResponseHeaders.append(header, value);
    }
    while (true);
    co_return Result<>();
}
inline auto HttpSession::_readContent(Connection &con, HttpReply &reply) -> Task<void> {
    // Select Mode
    auto &client = con.client;
    auto contentLength = reply.mResponseHeaders.value(HttpHeaders::ContentLength);
    auto transferEncoding = reply.mResponseHeaders.value(HttpHeaders::TransferEncoding);
    if (!contentLength.empty()) {
        int64_t len = 0;
        if (std::from_chars(contentLength.data(), contentLength.data() + contentLength.size(), len).ec != std::errc()) {
            co_return Unexpected(Error::Unknown);
        }
        std::string buffer;
        buffer.resize(len);
        auto ret = co_await client.recvAll(buffer.data(), len);
        if (!ret) {
            co_return Unexpected(ret.error());
        }
        if (ret.value() != len) {
            co_return Unexpected(Error::Unknown);
        }
        // Exchange to reply.content
        std::swap(reply.mContent, buffer);
    }
    else if (transferEncoding == "chunked") {
        // Chunked
        std::string buffer;
        int64_t len = 0;
        while (true) {
            // First, get a line
            auto lenString = co_await client.getline("\r\n");
            if (!lenString || lenString->empty()) {
                co_return Unexpected(lenString.error_or(Error::Unknown));
            }
            // Parse it
            if (std::from_chars(lenString->data(), lenString->data() + lenString->size(), len, 16).ec != std::errc()) {
                co_return Unexpected(Error::Unknown);
            }
            // Second, get a chunk
            size_t current = buffer.size();
            buffer.resize(current + len + 2);
            auto ret = co_await client.recvAll(buffer.data() + current, len + 2);
            if (!ret || ret.value() != len + 2) {
                co_return Unexpected(ret.error_or(Error::Unknown));
            }
            auto ptr = buffer.c_str();
            // Drop \r\n
            ILIAS_ASSERT(buffer.back() == '\n');
            buffer.pop_back();
            ILIAS_ASSERT(buffer.back() == '\r');
            buffer.pop_back();
            ::printf("chunk size %ld\n", len);
            if (len == 0) {
                break;
            }
        }
        std::swap(reply.mContent, buffer);
    }
    else {
        std::string buffer;
        while (true) {
            size_t curSize = buffer.size();
            buffer.resize(curSize + BufferIncreaseSize);
            auto ret = co_await client.recv(buffer.data() + curSize, BufferIncreaseSize);
            if (!ret) {
                co_return Unexpected(ret.error());
            }
            if (ret.value() == 0) {
                break;
            }
            buffer.resize(curSize + ret.value());
        }
        // Exchange to reply.content
        std::swap(reply.mContent, buffer);
    }
    co_return Result<>();
}
inline auto HttpSession::_connect(const Url &url) -> Task<Connection> {
    auto addr = IPAddress::fromHostname(std::string(url.host()).c_str());
    auto endpoint = IPEndpoint(addr, url.port());
    for (auto iter = mConnections.begin(); iter != mConnections.end(); ++iter) {
        if (iter->endpoint == endpoint) {
            // Cache hint
            ::fprintf(
                stderr, "Using cached connection on %s\n", 
                endpoint.toString().c_str()
            );
            auto con = std::move(*iter);
            mConnections.erase(iter);
            co_return con;
        }
    }

    // Prepare client
    Connection con;
    con.endpoint = endpoint;
    if (url.scheme() == "http") {
        TcpClient tcpClient(mIoContext, addr.family());
        con.client = std::move(tcpClient);
    }
#if !defined(ILIAS_NO_SSL)
    else if (url.scheme() == "https") {
        TcpClient tcpClient(mIoContext, addr.family());
        SslClient<TcpClient> sslClient(mSslContext, std::move(tcpClient));
        con.client = std::move(sslClient);
    }
#endif
    else {
        co_return Unexpected(Error::Unknown);
    }
    if (auto ret = co_await con.client.connect(endpoint); !ret) {
        co_return Unexpected(ret.error());
    }
    con.lastUsedTime = std::chrono::steady_clock::now();
    co_return con;
}
inline auto HttpSession::_cache(Connection con) -> void {
    // TODO :
    if (mConnections.size() >= 20) {
        mConnections.pop_front();
    }
    con.cached = true;
    con.lastUsedTime = std::chrono::steady_clock::now();
    mConnections.emplace_back(std::move(con));
}
inline auto HttpSession::_sprintf(std::string &buf, const char *fmt, ...) -> void {
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

// --- HttpReply
inline HttpReply::HttpReply(HttpReply &&) = default;
inline HttpReply::HttpReply() = default;
inline HttpReply::~HttpReply() = default;

inline auto HttpReply::text() -> Task<std::string> {
    co_return mContent;
}
inline auto HttpReply::statusCode() const -> int {
    return mStatusCode;
}
inline auto HttpReply::status() const -> std::string_view {
    return mStatus;
}
inline auto HttpReply::headers() const -> const HttpHeaders & {
    return mResponseHeaders;
}
inline auto HttpReply::transferDuration() const -> std::chrono::milliseconds {
    return mTransferDuration;
}
inline auto HttpReply::operator =(HttpReply &&) -> HttpReply & = default;

ILIAS_NS_END