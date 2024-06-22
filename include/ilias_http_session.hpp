#pragma once

#include "ilias_http_headers.hpp"
#include "ilias_http_request.hpp"
#include "ilias_http_cookie.hpp"
#include "ilias_socks5.hpp"
#include "ilias_async.hpp"
#include "ilias_zlib.hpp"
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

    /**
     * @brief Send Get request
     * 
     * @param request 
     * @return Task<HttpReply> 
     */
    auto get(const HttpRequest &request) -> Task<HttpReply>;

    /**
     * @brief Send head request
     * 
     * @param request 
     * @return Task<HttpReply> 
     */
    auto head(const HttpRequest &request) -> Task<HttpReply>;

    /**
     * @brief Send the post request
     * 
     * @param request 
     * @param data The post data, in std::span format
     * @return Task<HttpReply> 
     */
    auto post(const HttpRequest &request, std::span<const std::byte> data = {}) -> Task<HttpReply>;
    auto post(const HttpRequest &reqyest, std::string_view data) -> Task<HttpReply>;

    /**
     * @brief Send custome request
     * 
     * @param op 
     * @param request 
     * @param payload 
     * @return Task<HttpReply> 
     */
    auto sendRequest(Operation op, HttpRequest request, std::span<const std::byte> payload = {}) -> Task<HttpReply>;

    /**
     * @brief Set the Cookie Jar object
     * 
     * @param jar 
     */
    auto setCookieJar(HttpCookieJar *jar) -> void;

    /**
     * @brief Set the Proxy object
     * 
     * @param proxy 
     */
    auto setProxy(const Url &proxy) -> void;
private:
    /**
     * @brief Connection used tp keep alive
     * 
     */
    struct Connection {
        ByteStream<> client;
        IPEndpoint endpoint;
        std::string host;
        uint16_t port = 0;
        std::chrono::steady_clock::time_point lastUsedTime;
        bool cached = false;
    };
    static constexpr auto BufferIncreaseSize = int64_t(4096);

    auto _sendRequest(Operation op, const HttpRequest &request, std::span<const std::byte> extraData = {}) -> Task<HttpReply>;
    auto _readReply(Operation op, const HttpRequest &request, Connection connection) -> Task<HttpReply>;
    auto _readContent(Connection &connection, HttpReply &outReply) -> Task<void>;
    auto _readHeaders(Connection &connection, HttpReply &outReply) -> Task<void>;
    auto _connectWithProxy(const Url &url) -> Task<Connection>;
    auto _connect(const Url &url) -> Task<Connection>;
    auto _cache(Connection connection) -> void;
    auto _sprintf(std::string &buf, const char *fmt, ...) -> void;

    IoContext &mIoContext;

#if !defined(ILIAS_NO_SSL)
    SslContext mSslContext;
#endif

    // TODO: Improve the cache algorithm
    std::list<Connection> mConnections;
    HttpCookieJar *mCookieJar = nullptr;
    Url mProxy;
friend class HttpReply;
};

inline HttpSession::HttpSession(IoContext &ctxt) : mIoContext(ctxt) { }
inline HttpSession::HttpSession() : mIoContext(*IoContext::instance()) { }
inline HttpSession::~HttpSession() {

}

inline auto HttpSession::get(const HttpRequest &request) -> Task<HttpReply> {
    return sendRequest(Operation::GET, request);
}
inline auto HttpSession::head(const HttpRequest &request) -> Task<HttpReply> {
    return sendRequest(Operation::HEAD, request);
}
inline auto HttpSession::post(const HttpRequest &request, std::span<const std::byte> data) -> Task<HttpReply> {
    return sendRequest(Operation::POST, request, data);
}
inline auto HttpSession::post(const HttpRequest &request, std::string_view data) -> Task<HttpReply> {
    return sendRequest(Operation::POST, request, std::as_bytes(std::span(data.data(), data.size())));
}
inline auto HttpSession::sendRequest(Operation op, HttpRequest request, std::span<const std::byte> extraData) -> Task<HttpReply> {
    int n = 0;
    while (true) {
#if 1
        // In here we add timeout check for avoid waiting to long
        auto [result, timeout] = co_await WhenAny(_sendRequest(op, request, extraData), Sleep(request.transferTimeout()));
        if (timeout) {
            co_return Unexpected(Error::TimedOut);
        }
        if (!result) {
            co_return Unexpected(Error::Canceled);
        }
        auto reply = std::move(*result);
#else
        auto reply = co_await _sendRequest(op, request, extraData);
#endif
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
                ::fprintf(stderr, "[Http] Redirecting to %s by(%d, %s)\n", newLocation.data(), reply->statusCode(), reply->status().data());
                request.setUrl(newLocation);
                n += 1;
                continue;
            }
        }
        co_return reply;
    } 
}
inline auto HttpSession::setCookieJar(HttpCookieJar *cookieJar) -> void {
    mCookieJar = cookieJar;
}
inline auto HttpSession::setProxy(const Url &proxy) -> void {
    mProxy = proxy;
    // Drop all caches
    mConnections.clear();
}
inline auto HttpSession::_sendRequest(Operation op, const HttpRequest &request, std::span<const std::byte> payload) -> Task<HttpReply> {
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
        case Operation::HEAD: operation = "HEAD"; break;
        // case Operation::DELETE: operation = "DELETE"; break
        default: co_return Unexpected(Error::HttpBadRequest); break;
    }
    _sprintf(buffer, "%s %s HTTP/1.1\r\n", operation, requestString.c_str());
    
    // Add host
    _sprintf(buffer, "Host: %s\r\n", std::string(host).c_str());

    // Add encoding
#if !defined(ILIAS_NO_ZLIB)
    _sprintf(buffer, "Accept-Encoding: gzip, deflate\r\n");
#else
    _sprintf(buffer, "Accept-Encoding: identity\r\n");
#endif

    // Add userheaders
    for (const auto &[key, value] : request.headers()) {
        ::fprintf(stderr, "[Http] Adding header %s: %s\n", key.c_str(), value.c_str());
        _sprintf(buffer, "%s: %s\r\n", key.c_str(), value.c_str());
    }
    
    // Add cookies to headers
    if (mCookieJar) {
        auto cookies = mCookieJar->cookiesForUrl(url);
        if (!cookies.empty()) {
            _sprintf(buffer, "Cookie: ");
            for (const auto &cookie : mCookieJar->cookiesForUrl(url)) {
                ::fprintf(stderr, "[Http] Adding cookie %s=%s\n", cookie.name().c_str(), cookie.value().c_str());
                _sprintf(buffer, "%s=%s; ", cookie.name().c_str(), cookie.value().c_str());
            }
            // Remove last '; '
            buffer.pop_back();
            buffer.pop_back();
            _sprintf(buffer, "\r\n");
        }
    }

    // Add the payload size if need
    if (!payload.empty()) {
        _sprintf(buffer, "Content-Length: %zu\r\n", payload.size_bytes());
    }

    // Add end \r\n on header end
    buffer.append("\r\n");

    // Add the data sending to
    if (!payload.empty()) {
        buffer.append(reinterpret_cast<const char*>(payload.data()), payload.size_bytes());
    }

    // Send this reuqets
    auto sended = co_await client.sendAll(buffer.data(), buffer.size());
    if (!sended) {
        co_return Unexpected(sended.error());
    }
    if (sended.value() != buffer.size()) {
        co_return Unexpected(Error::ConnectionAborted);
    }
    auto reply = co_await _readReply(op, request, std::move(*con));
    if (!reply && fromCache) {
        ::fprintf(stderr, "[Http] ERROR from read reply in cache => %s, try again\n", reply.error().toString().c_str());
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
        auto result = Zlib::decompress(reply.mContent, Zlib::GzipFormat);
        if (result.empty()) {
            co_return Unexpected(Error::HttpBadReply);
        }
        reply.mContent = std::move(result);
    }
    else if (encoding == "deflate") {
        // Decompress deflate
        auto result = Zlib::decompress(reply.mContent, Zlib::DeflateFormat);
        if (result.empty()) {
            co_return Unexpected(Error::HttpBadReply);
        }
        reply.mContent = std::move(result);
    }
#endif
    // Update cookiejars
    if (mCookieJar) {
        auto cookies = reply.mResponseHeaders.values(HttpHeaders::SetCookie);
        for (const auto &str : cookies) {
            for (auto &cookie : HttpCookie::parse(str)) {
                if (cookie.domain().empty()) {
                    cookie.setDomain(request.url().host());
                }
                mCookieJar->insertCookie(std::move(cookie));
            }
        }
    }

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
        co_return Unexpected(line.error_or(Error::HttpBadReply));
    }
    // Parse first line
    std::string_view firstLine(*line);
    auto firstSpace = firstLine.find(' ');
    auto secondSpace = firstLine.find(' ', firstSpace + 1);
    if (firstSpace == std::string_view::npos || secondSpace == std::string_view::npos) {
        co_return Unexpected(Error::HttpBadReply);
    }
    auto codeString = firstLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    if (std::from_chars(codeString.data(), codeString.data() + codeString.size(), reply.mStatusCode).ec != std::errc()) {
        co_return Unexpected(Error::HttpBadReply);
    }
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
            co_return Unexpected(Error::HttpBadReply);
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
            co_return Unexpected(Error::HttpBadReply);
        }
        std::string buffer;
        buffer.resize(len);
        auto ret = co_await client.recvAll(buffer.data(), len);
        if (!ret) {
            co_return Unexpected(ret.error());
        }
        if (ret.value() != len) {
            co_return Unexpected(Error::HttpBadReply);
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
                co_return Unexpected(lenString.error_or(Error::HttpBadReply));
            }
            // Parse it
            if (std::from_chars(lenString->data(), lenString->data() + lenString->size(), len, 16).ec != std::errc()) {
                co_return Unexpected(Error::HttpBadReply);
            }
            // Second, get a chunk
            size_t current = buffer.size();
            buffer.resize(current + len + 2);
            auto ret = co_await client.recvAll(buffer.data() + current, len + 2);
            if (!ret || ret.value() != len + 2) {
                co_return Unexpected(ret.error_or(Error::HttpBadReply));
            }
            auto ptr = buffer.c_str();
            // Drop \r\n
            ILIAS_ASSERT(buffer.back() == '\n');
            buffer.pop_back();
            ILIAS_ASSERT(buffer.back() == '\r');
            buffer.pop_back();
            ::fprintf(stderr, "[Http] chunk size %zu\n", size_t(len));
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
    if (!mProxy.empty()) {
        co_return co_await _connectWithProxy(url);
    }

    auto addr = IPAddress::fromHostname(std::string(url.host()).c_str());
    auto endpoint = IPEndpoint(addr, url.port());
    for (auto iter = mConnections.begin(); iter != mConnections.end(); ++iter) {
        if (iter->endpoint == endpoint) {
            // Cache hint
            ::fprintf(
                stderr, "[Http] Using cached connection on %s\n", 
                endpoint.toString().c_str()
            );
            auto con = std::move(*iter);
            mConnections.erase(iter);
            co_return con;
        }
    }

    // Prepare client on no-proxy
    IStreamClient client;
    if (url.scheme() == "http") {
        TcpClient tcpClient(mIoContext, addr.family());
        client = std::move(tcpClient);
    }
#if !defined(ILIAS_NO_SSL)
    else if (url.scheme() == "https") {
        TcpClient tcpClient(mIoContext, addr.family());
        SslClient<TcpClient> sslClient(mSslContext, std::move(tcpClient));
        sslClient.setHostname(url.host());
        client = std::move(sslClient);
    }
#endif
    else {
        co_return Unexpected(Error::HttpBadRequest);
    }
    if (auto ret = co_await client.connect(endpoint); !ret) {
        co_return Unexpected(ret.error());
    }
    Connection con;
    con.client = std::move(client);
    con.endpoint = endpoint;
    con.lastUsedTime = std::chrono::steady_clock::now();
    co_return con;
}
inline auto HttpSession::_connectWithProxy(const Url &url) -> Task<Connection> {
    auto host = url.host();
    auto port = url.port();
    // Check cache
    for (auto iter = mConnections.begin(); iter != mConnections.end(); ++iter) {
        if (iter->host == host && iter->port == port) {
            // Cache hint
            ::fprintf(
                stderr, "[Http] Using cached connection on %s:%d, proxyed\n",
                iter->host.c_str(), iter->port
            );
            auto con = std::move(*iter);
            mConnections.erase(iter);
            co_return con;
        }
    }

    // Prepare client on proxy
    IStreamClient client;
    Socks5Client socks5(mIoContext, IPEndpoint(std::string(mProxy.host()).c_str(), mProxy.port()));
    // Connect it by proxy
    if (auto err = co_await socks5.connect(host, port); !err) {
        co_return Unexpected(err.error());
    }
    if (url.scheme() == "http") {
        client = std::move(socks5);
    }
#if !defined(ILIAS_NO_SSL)
    else if (url.scheme() == "https") {
        // Wrap it by SSL
        SslClient<> sslClient(mSslContext, std::move(socks5));
        sslClient.setHostname(url.host());
        client = std::move(sslClient);
    }
#endif
    else {
        co_return Unexpected(Error::HttpBadRequest);
    }
    co_return Connection {
        .client = std::move(client),
        .host = std::string(host),
        .port = port,
        .lastUsedTime = std::chrono::steady_clock::now()
    };
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
inline auto HttpReply::content() -> Task<std::vector<uint8_t> > {
    std::vector<uint8_t> vec(mContent.begin(), mContent.end());
    co_return vec;
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