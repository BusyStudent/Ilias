#pragma once

#include "ilias_http_headers.hpp"
#include "ilias_http_request.hpp"
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
    auto post(const HttpRequest &request) -> Task<HttpReply>;
    auto sendRequest(Operation op, HttpRequest request, std::span<uint8_t> extraData = {}) -> Task<HttpReply>;
private:
    /**
     * @brief Connection used tp keep alive
     * 
     */
    struct Connection {
        IStreamClient client;
        IPEndpoint endpoint;
        std::string recvbuffer; //< Used for recvbuffer
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
    // TODO: Add zlib or another things
    _sprintf(buffer, "Accept-Encoding: identity\r\n");
    // Add padding extraData
    if (!extraData.empty()) {
        buffer.append(reinterpret_cast<const char*>(extraData.data()), extraData.size());
    }
    // Add end \r\n
    buffer.append("\r\n");

    // Send this reuqets
    size_t bytesLeft = buffer.size();
    size_t cur = 0;
    while (bytesLeft > 0) {
        auto ret = co_await client.send(buffer.c_str() + cur, bytesLeft);
        if (!ret) {
            co_return Unexpected(ret.error());
        }
        cur += ret.value();
        bytesLeft -= ret.value();
    }
    auto reply = co_await _readReply(op, request, std::move(*con));
    if (!reply && fromCache) {
        ::printf("ERROR from read reply in cache => %s, try again\n", reply.error().message().c_str());
        continue; //< Try again
    }
    reply->mTransferDuration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - now);
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
    if (reply.mResponseHeaders.value(HttpHeaders::Connection) == "keep-alive") {
        _cache(std::move(connection));
    }
    co_return reply;
}
inline auto HttpSession::_readHeaders(Connection &con, HttpReply &reply) -> Task<void> {
    auto &buffer = con.recvbuffer;
    auto &client = con.client;
    while (!buffer.contains("\r\n")) {
        size_t curSize = buffer.size();
        buffer.resize(curSize + BufferIncreaseSize);
        auto n = co_await client.recv(buffer.data() + curSize, BufferIncreaseSize);
        if (!n) {
            co_return Unexpected(n.error());
        }
        if (*n == 0) {
            co_return Unexpected(Error::ConnectionReset);
        }
        buffer.resize(curSize + n.value());
    }
    // Got headers string
    // Parse the headers
    // Begin with HTTP/1.1 xxx OK
    std::string_view sv(buffer);

    // First split first line of \r\n
    auto firstLineEnd = sv.find("\r\n");
    auto firstLine = sv.substr(0, firstLineEnd);

    // Parse first line
    int statusCode = 0;
    auto firstSpace = firstLine.find(' ');
    auto secondSpace = firstLine.find(' ', firstSpace + 1);
    if (firstSpace == std::string_view::npos || secondSpace == std::string_view::npos) {
        co_return Unexpected(Error::Unknown);
    }
    auto codeString = firstLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    auto statusString = firstLine.substr(secondSpace + 1);
    std::from_chars(codeString.data(), codeString.data() + codeString.size(), statusCode);

    // Second split header strings
    auto headersString = sv.substr(firstLineEnd + 2, sv.find("\r\n\r\n") - firstLineEnd - 2);
    auto header = HttpHeaders::parse(headersString);
    if (header.empty()) {
        co_return Unexpected(Error::Unknown);
    }

    reply.mStatus = statusString;
    reply.mStatusCode = statusCode;
    reply.mResponseHeaders = std::move(header);

    // Drop a little bits
    buffer.erase(0, firstLineEnd + 2 + headersString.size() + 4);
    co_return Result<>();
}
inline auto HttpSession::_readContent(Connection &con, HttpReply &reply) -> Task<void> {
    // Select Mode
    auto &buffer = con.recvbuffer;
    auto &client = con.client;
    auto contentLength = reply.mResponseHeaders.value(HttpHeaders::ContentLength);
    auto transferEncoding = reply.mResponseHeaders.value(HttpHeaders::TransferEncoding);
    if (!contentLength.empty()) {
        int64_t len = 0;
        if (std::from_chars(contentLength.data(), contentLength.data() + contentLength.size(), len).ec != std::errc()) {
            co_return Unexpected(Error::Unknown);
        }
        len -= buffer.size(); //< Still has a a lot of bytes in byffer
        while (len > 0) {
            size_t curSize = buffer.size();
            buffer.resize(curSize + BufferIncreaseSize);
            auto ret = co_await client.recv(buffer.data() + curSize, (std::min<int64_t>)(BufferIncreaseSize, len));
            if (!ret) {
                co_return Unexpected(ret.error());
            }
            if (ret.value() == 0) {
                // Peer closed
                co_return Unexpected(Error::ConnectionReset);
            }
            buffer.resize(curSize + ret.value());
            len -= ret.value();
        }
        // Exchange to reply.content
        std::swap(reply.mContent, buffer);
    }
    else if (transferEncoding == "chunked") {
        // Chunked
        while (true) {
            // Custome readed bytes
            int64_t len = -1;
            std::string_view bufferView = buffer;
            while (!bufferView.empty() && bufferView.contains("\r\n")) {
                auto numRange = bufferView.substr(0, bufferView.find("\r\n"));
                auto errc = std::from_chars(numRange.data(), numRange.data() + numRange.size(), len, 16);
                if (errc.ec != std::errc()) {
                    co_return Unexpected(Error::Unknown);
                }
                if (bufferView.size() < numRange.size() + 2 + len + 2) {
                    // num\r\ndata\r\n
                    break;
                }
                bufferView = bufferView.substr(numRange.size() + 2);
                // Copy data
                reply.mContent.append(bufferView.data(), len);
                bufferView = bufferView.substr(len + 2);
            }
            if (!bufferView.empty() && bufferView.data() != buffer.data()) {
                buffer.erase(0, bufferView.data() - buffer.data());
            }
            if (len == 0) {
                buffer.clear();
                co_return Result<>();
            }
            // No encough bytes
            size_t curSize = buffer.size();
            buffer.resize(curSize + BufferIncreaseSize);
            auto ret = co_await client.recv(buffer.data() + curSize, BufferIncreaseSize);
            if (!ret) {
                co_return Unexpected(ret.error());
            }
            if (ret.value() == 0) {
                // Peer closed
                co_return Unexpected(Error::Unknown);
            }
            buffer.resize(curSize + ret.value());
        }
    }
    else {
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