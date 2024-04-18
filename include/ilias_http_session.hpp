#pragma once

#include "ilias_http_headers.hpp"
#include "ilias_http_request.hpp"
#include "ilias_async.hpp"
#include "ilias_url.hpp"
#include "ilias_ssl.hpp"
#include <map>

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
private:
    HttpReply();

    Url mUrl;
    int mStatusCode = 0;
    std::string mContent;
    HttpHeaders mRequestHeaders;
    HttpHeaders mResponseHeaders;
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
    auto sendRequest(Operation op, const HttpRequest &request, std::span<uint8_t> extraData = {}) -> Task<HttpReply>;
private:
    auto _readReply(Operation op, const HttpRequest &request, IStreamClient client) -> Task<HttpReply>;
    auto _takeClient(const Url &url) -> Task<IStreamClient>;
    auto _putClient(const Url &url, IStreamClient) -> void;
    auto _sprintf(std::string &buf, const char *fmt, ...) -> void;

    IoContext &mIoContext;
    SslContext mSslContext;

    // TODO: Improve the cache algorithm
    std::multimap<std::string, IStreamClient, std::less<> > mClients; //< Cached client
friend class HttpReply;
};

inline HttpSession::HttpSession(IoContext &ctxt) : mIoContext(ctxt) { }
inline HttpSession::HttpSession() : mIoContext(*IoContext::instance()) { }
inline HttpSession::~HttpSession() {

}

inline auto HttpSession::get(const HttpRequest &request) -> Task<HttpReply> {
    return sendRequest(Operation::GET, request);
}
inline auto HttpSession::sendRequest(Operation op, const HttpRequest &request, std::span<uint8_t> extraData) -> Task<HttpReply> {
    const auto &url = request.url();
    const auto host = url.host();
    const auto port = url.port();
    const auto path = url.path();
    const auto scheme = url.scheme();

    // Check args
    if (port <= 0 || host.empty() || (scheme != "https" && scheme != "http")) {
        co_return Unexpected(Error::InvalidArgument);
    }

    // Try peek from cache
    auto client = co_await _takeClient(url);
    if (!client) {
        co_return Unexpected(client.error());
    }

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
    _sprintf(buffer, "%s %s HTTP/1.1\r\n", operation, std::string(path).c_str());
    for (const auto &[key, value] : request.headers()) {
        _sprintf(buffer, "%s: %s\r\n", key.c_str(), value.c_str());
    }
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
        auto ret = co_await client->send(buffer.c_str() + cur, bytesLeft);
        if (!ret) {
            co_return Unexpected(ret.error());
        }
        cur += ret.value();
        bytesLeft -= ret.value();
    }
    co_return co_await _readReply(op, request, std::move(*client));
}
inline auto HttpSession::_readReply(Operation op, const HttpRequest &request, IStreamClient client) -> Task<HttpReply> {
    // Read response
    std::string content;
    char buffer[1024];
    while (true) {
        auto ret = co_await client.recv(buffer, sizeof(buffer));
        if (!ret) {
            co_return Unexpected(ret.error());
        }
        if (ret.value() == 0) {
            break;
        }
        content.append(buffer, ret.value());
        if (content.contains("\r\n\r\n")) {
            // Got headers
            break;
        }
    }

    // Parse the headers
    // Begin with HTTP/1.1 xxx OK
    std::string_view sv(content);

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
    auto messageString = firstLine.substr(secondSpace + 1);
    std::from_chars(codeString.data(), codeString.data() + codeString.size(), statusCode);

    // Second split header strings
    auto headersString = sv.substr(firstLineEnd + 2, sv.find("\r\n\r\n") - firstLineEnd - 2);
    auto header = HttpHeaders::parse(headersString);
    if (header.empty()) {
        co_return Unexpected(Error::Unknown);
    }
    // Read the content
    auto value = header.value(HttpHeaders::ContentLength);
    bool keepAlive = header.value(HttpHeaders::Connection) == "keep-alive";
    // Move the header parts
    content.erase(0, firstLineEnd + 2 + headersString.size() + 4);
    if (!value.empty()) {
        int64_t len = 0;
        std::from_chars(value.data(), value.data() + value.size(), len);

        len -= content.size(); //< Still has a a lot of bytes in byffer
        while (len > 0) {
            auto ret = co_await client.recv(buffer, (std::min<int64_t>)(sizeof(buffer), len));
            if (!ret) {
                co_return Unexpected(ret.error());
            }
            content.append(buffer, ret.value());
            len -= ret.value();
        }
    }
    else {
        while (true) {
            auto ret = co_await client.recv(buffer, sizeof(buffer));
            if (!ret) {
                co_return Unexpected(ret.error());
            }
            if (ret.value() == 0) {
                break;
            }
            content.append(buffer, ret.value());
        }
    }

    // Build response
    HttpReply reply;
    reply.mResponseHeaders = std::move(header);
    reply.mRequestHeaders = request.headers();
    reply.mUrl = request.url();
    reply.mContent = std::move(content);
    reply.mStatusCode = statusCode;

    // put the client to cache
    if (keepAlive) {
        _putClient(request.url(), std::move(client));
    }

    co_return reply;
}
inline auto HttpSession::_takeClient(const Url &url) -> Task<IStreamClient> {
    auto iter = mClients.find(url.host());
    if (iter != mClients.end()) {
        // Cache hint
        auto client = std::move(iter->second);
        mClients.erase(iter);
        co_return client;
    }

    auto addr = IPAddress::fromHostname(std::string(url.host()).c_str());
    auto endpoint = IPEndpoint(addr, url.port());

    // Prepare client
    IStreamClient client;
    if (url.scheme() == "https") {
        TcpClient tcpClient(mIoContext, addr.family());
        SslClient<TcpClient> sslClient(mSslContext, std::move(tcpClient));
        client = std::move(sslClient);
    }
    else {
        TcpClient tcpClient(mIoContext, addr.family());
        client = std::move(tcpClient);
    }
    if (auto ret = co_await client.connect(endpoint); !ret) {
        co_return Unexpected(ret.error());
    }
    co_return client;
}
inline auto HttpSession::_putClient(const Url &url, IStreamClient client) -> void {
    mClients.emplace(url.host(), std::move(client));
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

ILIAS_NS_END