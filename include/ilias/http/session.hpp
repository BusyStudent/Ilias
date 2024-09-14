#pragma once

#include <ilias/http/transfer.hpp>
#include <ilias/http/http1.1.hpp>
#include <ilias/http/request.hpp>
#include <ilias/http/cookie.hpp>
#include <ilias/http/reply.hpp>
#include <ilias/net/addrinfo.hpp>
#include <ilias/net/tcp.hpp>
#include <ilias/io/context.hpp>
#include <ilias/ssl.hpp>
#include <ilias/log.hpp>
#include <span>
#include <map>

ILIAS_NS_BEGIN

/**
 * @brief The Http Session of sending and receiving HTTP requests, managing cookies and connection pooling.
 *
 */
class HttpSession {
public:
    HttpSession(IoContext &ctxt);
    HttpSession(const HttpSession &other) = delete;
    ~HttpSession();

    /**
     * @brief Send a GET request to the server
     *
     * @param request
     * @return Task<HttpReply>
     */
    auto get(const HttpRequest &request) -> Task<HttpReply> { return sendRequest("GET", request); }

    /**
     * @brief Send a POST request to the server
     *
     * @param request
     * @param payload The payload to post (if any)
     * @return Task<HttpReply>
     */
    auto post(const HttpRequest &request, std::span<const std::byte> payload = {}) -> Task<HttpReply> {
        return sendRequest("POST", request, payload);
    }

    /**
     * @brief Send a POST request to the server
     *
     * @param request
     * @param payload The payload to post (if any)
     * @return Task<HttpReply>
     */
    auto post(const HttpRequest &request, std::string_view payload) -> Task<HttpReply> {
        return sendRequest("POST", request, std::as_bytes(std::span(payload)));
    }

    /**
     * @brief Send a HEAD request to the server
     *
     * @param request
     * @return Task<HttpReply>
     */
    auto head(const HttpRequest &request) -> Task<HttpReply> { return sendRequest("HEAD", request); }

    /**
     * @brief Send a PUT request to the server
     *
     * @param request
     * @param payload
     * @return Task<HttpReply>
     */
    auto put(const HttpRequest &request, std::span<const std::byte> payload) -> Task<HttpReply> {
        return sendRequest("PUT", request, payload);
    }

    /**
     * @brief Send the request to the server
     *
     * @param method The HTTP method to use (GET, POST, HEAD, etc.)
     * @param request The HTTP request to send
     * @param payload The payload to send (if any)
     * @return Task<HttpReply>
     */
    auto sendRequest(std::string_view method, const HttpRequest &request,
                     std::span<const std::byte> payload = {}) -> Task<HttpReply>;

    /**
     * @brief Set the Cookie Jar object
     *
     * @param jar
     */
    auto setCookieJar(HttpCookieJar *jar) -> void { mCookieJar = jar; }

private:
    /**
     * @brief The sendRequest implementation, only do the connection handling
     *
     * @param method
     * @param request
     * @param payload
     * @return Task<HttpReply>
     */
    auto sendRequestImpl(std::string_view method, const Url &url, HttpHeaders &headers,
                         std::span<const std::byte> payload, bool stream) -> Task<HttpReply>;

    /**
     * @brief Add Cookie headers and another important headers to the request
     *
     * @param url
     * @param headers
     *
     */
    auto normalizeRequest(const Url &url, HttpHeaders &headers) -> void;

    /**
     * @brief Collect cookies from the reply and add them to the cookie jar
     *
     * @param reply
     */
    auto parseReply(HttpReply &reply) -> void;

    /**
     * @brief Connect to the server by url and return the HttpStream for transfer
     *
     * @param url
     * @param fromPool If the connection is from the pool
     * @return Task<std::unique_ptr<HttpStream> >
     */
    auto connect(const Url &url, bool &fromPool) -> Task<std::unique_ptr<HttpStream>>;

    /**
     * @brief The pair for differentiating connections
     *
     */
    struct Endpoint {
        std::string scheme;
        std::string host;
        uint16_t    port;
        Url         proxy;

        auto operator<=>(const Endpoint &other) const = default;
    };

    IoContext &mCtxt;

#if !defined(ILIAS_NO_SSL)
    SslContext mSslCtxt;
#endif

    // Configuration ...
    Url            mProxy; //< The proxy url
    HttpCookieJar *mCookieJar =
        nullptr; //< The cookie jar to use for this session (if null, no cookies will be accepted)

    // State ...
    std::multimap<Endpoint, std::unique_ptr<HttpConnection>> mConnections; //< Conenction Pool
};

inline HttpSession::HttpSession(IoContext &ctxt) : mCtxt(ctxt) {
}

inline HttpSession::~HttpSession() {
    mConnections.clear();
}

inline auto HttpSession::sendRequest(std::string_view method, const HttpRequest &request,
                                     std::span<const std::byte> payload) -> Task<HttpReply> {
    Url         url              = request.url();
    HttpHeaders headers          = request.headers();
    int         maximumRedirects = request.maximumRedirects();
    if (maximumRedirects < 0) {
        maximumRedirects = std::numeric_limits<int>::max(); //< Infinite redirects
    }
    int idx = 0; // The number of redirects
    while (true) {
        auto reply = co_await sendRequestImpl(method, url, headers, payload, request.streamMode());
        if (!reply) {
            co_return Unexpected(reply.error());
        }
        const std::array redirectCodes = {301, 302, 303, 307, 308};
        if (std::find(redirectCodes.begin(), redirectCodes.end(), reply->statusCode()) != redirectCodes.end() &&
            idx < maximumRedirects) {
            auto location = reply->headers().value(HttpHeaders::Location);
            if (location.empty()) {
                co_return Unexpected(Error::HttpBadReply);
            }
            ILIAS_TRACE("Http", "Redirecting to {} ({} of maximum {})", location, idx + 1, maximumRedirects);
            // Do redirect
            url     = location;
            headers = request.headers();
            ++idx;
            continue;
        }
        reply->mRequest = request; //< Store the original request
        reply->mUrl     = url;     //< Store the final url
        co_return std::move(reply);
    }
}

inline auto HttpSession::sendRequestImpl(std::string_view method, const Url &url, HttpHeaders &headers,
                                         std::span<const std::byte> payload, bool streamMode) -> Task<HttpReply> {
    while (true) {
        normalizeRequest(url, headers);
        bool fromPool = false;
        auto stream = co_await connect(url, fromPool);
        if (!stream) {
            co_return Unexpected(stream.error());
        }
        auto &streamPtr = stream.value();
        if (auto ret = co_await streamPtr->send(method, url, headers, payload); !ret) {
            if (fromPool) {
                continue;
            }
            co_return Unexpected(ret.error());
        }
        // Build the reply
        auto reply = co_await HttpReply::make(std::move(streamPtr), streamMode);
        if (!reply) {
            if (fromPool) {
                continue;
            }
            co_return Unexpected(reply.error());
        }
        parseReply(reply.value());
        co_return std::move(*reply);
    }
}

inline auto HttpSession::normalizeRequest(const Url &url, HttpHeaders &headers) -> void {
    // Add Cookie headers
    if (mCookieJar) {
        auto cookies      = mCookieJar->cookiesForUrl(url);
        auto cookieString = std::string();
        for (const auto &cookie : cookies) {
            cookieString += cookie.name();
            cookieString += "=";
            cookieString += cookie.value();
            cookieString += "; ";
        }
        if (!cookieString.empty()) {
            // Remove the last '; '
            cookieString.pop_back();
            cookieString.pop_back();
            headers.append("Cookie", cookieString);
        }
    }

    // Accept
    if (headers.value(HttpHeaders::Accept).empty()) {
        headers.append("Accept", "*/*");
    }

    // Accept-Encoding
    if (headers.value("Accept-Encoding").empty()) {
#if !defined(ILIAS_NO_ZLIB)
        headers.append("Accept-Encoding", "gzip, deflate");
#else
        headers.append("Accept-Encoding", "identity");
#endif
    }
}

inline auto HttpSession::parseReply(HttpReply &reply) -> void {
    // Update cookie here
    if (!mCookieJar) {
        return;
    }
    const auto cookies = reply.headers().values(HttpHeaders::SetCookie);
    for (const auto &setCookie : cookies) {
        for (auto &cookie : HttpCookie::parse(setCookie)) {
            cookie.normalize(reply.url());
            mCookieJar->insertCookie(cookie);
        }
    }
}

inline auto HttpSession::connect(const Url &url, bool &fromPool) -> Task<std::unique_ptr<HttpStream>> {
    // TODO : Improve this by using mpmc channel
    // Check proxy
    auto     scheme = std::string(url.scheme());
    auto     host   = std::string(url.host());
    uint16_t port;

    if (auto p = url.port(); !p) {
        // Get port by scheme
        auto ent = ::getservbyname(scheme.c_str(), "tcp");
        if (!ent) {
            ILIAS_ERROR("Http", "Failed to get port for scheme: {}", scheme);
            co_return Unexpected(Error::HttpBadRequest);
        }
        port = ::ntohs(ent->s_port);
    }
    else {
        port = *p;
    }

    Endpoint endpoint {.scheme = scheme, .host = host, .port = port, .proxy = mProxy};

    for (auto it = mConnections.find(endpoint); it != mConnections.end();) {
        auto &[_, con] = *it;
        if (con->isClosed()) {
            it = mConnections.erase(it);
            continue;
        }
        if (con->version() == HttpConnection::Http1_1 && !con->isIdle()) {
            // Not idle, and also http1.1, so we can't use it
            ++it;
            continue;
        }
        fromPool = true;
        co_return co_await con->newStream();
    }

    // No connection found, create a new one
    auto addrinfo = co_await AddressInfo::fromHostnameAsync(host.c_str());
    if (!addrinfo) {
        co_return Unexpected(addrinfo.error());
    }
    auto endpoints = addrinfo->addresses();
    ILIAS_ASSERT(!endpoints.empty());

    // Try connect to all addresses
    IStreamClient cur;
    for (size_t idx = 0; idx < endpoints.size(); ++idx) {
        auto     &addr = endpoints[idx];
        TcpClient client(mCtxt, addr.family());
        ILIAS_TRACE("Http", "Trying to connect to {}:{} ({} of {})", addr, port, idx + 1, endpoints.size());
        if (auto ret = co_await client.connect(IPEndpoint {addr, port}); !ret && idx == endpoints.size() - 1) {
            continue;
        }
        else if (!ret) {
            co_return Unexpected(ret.error());
        }
        cur = std::move(client);
        break;
    }

    // Try adding a ssl if needed
#if !defined(ILIAS_NO_SSL)
    if (scheme == "https") {
        SslClient sslClient(mSslCtxt, std::move(cur));
        sslClient.setHostname(host);
        if (auto ret = co_await sslClient.handshake(); !ret) {
            co_return Unexpected(ret.error());
        }
        cur = std::move(sslClient);
    }
#endif

    // Done, adding connection
    auto con = std::make_unique<Http1Connection>(std::move(cur));
    auto ptr = con.get();
    mConnections.emplace(endpoint, std::move(con));
    co_return co_await ptr->newStream();
}

ILIAS_NS_END