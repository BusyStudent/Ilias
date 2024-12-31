#pragma once

#include <ilias/http/detail/worker.hpp>
#include <ilias/http/transfer.hpp>
#include <ilias/http/http1.1.hpp>
#include <ilias/http/request.hpp>
#include <ilias/http/cookie.hpp>
#include <ilias/http/reply.hpp>
#include <ilias/sync/scope.hpp>
#include <ilias/sync/mutex.hpp>
#include <ilias/net/addrinfo.hpp>
#include <ilias/net/socks5.hpp>
#include <ilias/net/tcp.hpp>
#include <ilias/task.hpp>
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
     * @return IoTask<HttpReply>
     */
    auto get(const HttpRequest &request) -> IoTask<HttpReply> { return sendRequest("GET", request); }

    /**
     * @brief Send a POST request to the server
     *
     * @param request
     * @param payload The payload to post (if any)
     * @return IoTask<HttpReply>
     */
    auto post(const HttpRequest &request, std::span<const std::byte> payload = {}) -> IoTask<HttpReply> {
        return sendRequest("POST", request, payload);
    }

    /**
     * @brief Send a POST request to the server
     *
     * @param request
     * @param payload The payload to post (if any)
     * @return IoTask<HttpReply>
     */
    auto post(const HttpRequest &request, std::string_view payload) -> IoTask<HttpReply> {
        return sendRequest("POST", request, std::as_bytes(std::span(payload)));
    }

    /**
     * @brief Send a HEAD request to the server
     *
     * @param request
     * @return IoTask<HttpReply>
     */
    auto head(const HttpRequest &request) -> IoTask<HttpReply> { 
        return sendRequest("HEAD", request); 
    }

    /**
     * @brief Send a PUT request to the server
     *
     * @param request
     * @param payload
     * @return IoTask<HttpReply>
     */
    auto put(const HttpRequest &request, std::span<const std::byte> payload) -> IoTask<HttpReply> {
        return sendRequest("PUT", request, payload);
    }

    /**
     * @brief Send the request to the server
     *
     * @param method The HTTP method to use (GET, POST, HEAD, etc.)
     * @param request The HTTP request to send
     * @param payload The payload to send (if any)
     * @return IoTask<HttpReply>
     */
    auto sendRequest(std::string_view method, const HttpRequest &request,
                     std::span<const std::byte> payload = {}) -> IoTask<HttpReply>;

    /**
     * @brief Set the Cookie Jar object
     *
     * @param jar
     */
    auto setCookieJar(HttpCookieJar *jar) -> void { mCookieJar = jar; }

    /**
     * @brief Set the Proxy object
     * 
     * @param proxy 
     */
    auto setProxy(const Url &proxy) -> void { mProxy = proxy; }

    /**
     * @brief Get the Cookie Jar object
     * 
     * @return HttpCookieJar* 
     */
    auto cookieJar() const -> HttpCookieJar * { return mCookieJar; }

    /**
     * @brief Get the proxy
     * 
     * @return const Url& 
     */
    auto proxy() const -> const Url & { return mProxy; }
private:
    /**
     * @brief The sendRequest implementation, only do the connection handling
     *
     * @param method
     * @param request
     * @param payload
     * @return IoTask<HttpReply>
     */
    auto sendRequestImpl(std::string_view method, const Url &url, HttpHeaders &headers,
                         std::span<const std::byte> payload, bool stream) -> IoTask<HttpReply>;

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
     * @param url Current request url
     */
    auto parseReply(HttpReply &reply, const Url &url) -> void;

    /**
     * @brief Connect to the server by url and return the HttpStream for transfer
     *
     * @param url
     * @param fromPool If the connection is from the pool
     * @return IoTask<std::unique_ptr<HttpStream> >
     */
    auto connect(const Url &url, bool &fromPool) -> IoTask<std::unique_ptr<HttpStream>>;

    IoContext &mCtxt;
    TaskScope  mScope; //< For manage all worker's lifetime

#if !defined(ILIAS_NO_SSL)
    SslContext mSslCtxt;
#endif

    // Configuration ...
    Url            mProxy; //< The proxy url
    HttpCookieJar *mCookieJar =
        nullptr; //< The cookie jar to use for this session (if null, no cookies will be accepted)
    size_t         mMaxConnectionHttp1_1 = 5; //< Max connection limit for http1

    // State ...
    Mutex mWorkersMutex; //< The mutex for read write the Worker pool
    std::map<HttpEndpoint, HttpWorker> mWorkers; //< Worker Pool
};

// Implement Begin
inline HttpSession::HttpSession(IoContext &ctxt) : mCtxt(ctxt), mScope(ctxt) {
}

inline HttpSession::~HttpSession() {
    mScope.cancel();
    mScope.wait();
}

inline auto HttpSession::sendRequest(std::string_view method, const HttpRequest &request,
                                     std::span<const std::byte> payload) -> IoTask<HttpReply> {
    Url         url              = request.url();
    HttpHeaders headers          = request.headers();
    int         maximumRedirects = request.maximumRedirects();
    if (maximumRedirects < 0) {
        maximumRedirects = std::numeric_limits<int>::max(); //< Infinite redirects
    }
    int idx = 0; // The number of redirects
    while (true) {
#if 1
        auto transferTimeout = request.transferTimeout();
        if (transferTimeout == std::chrono::milliseconds{}) {
            transferTimeout = std::chrono::hours(10); //< Just like INFINITE timeout
        }
        auto [reply_, timeout] = co_await whenAny(
            sendRequestImpl(method, url, headers, payload, request.streamMode()),
            sleep(transferTimeout)
        );
        if (timeout) { //< Timed out
            co_return Unexpected(Error::TimedOut);
        }
        // Has reply
        if (!reply_->has_value()) { //< Failed to get 
            co_return Unexpected(reply_->error());
        }
        auto &reply = reply_.value();
#else
        auto reply = co_await sendRequestImpl(method, url, headers, payload, request.streamMode());
        if (!reply) {
            co_return Unexpected(reply.error());
        }
#endif
        const std::array redirectCodes = {301, 302, 303, 307, 308};
        if (std::find(redirectCodes.begin(), redirectCodes.end(), reply->statusCode()) != redirectCodes.end() &&
            idx < maximumRedirects) {
            Url location = reply->headers().value(HttpHeaders::Location);
            if (location.empty()) {
                co_return Unexpected(Error::HttpBadReply);
            }
            ILIAS_INFO("Http", "Redirecting to {} ({} of maximum {})", location, idx + 1, maximumRedirects);
            // Do redirect
            url     = url.resolved(location);
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
                                         std::span<const std::byte> payload, bool streamMode) -> IoTask<HttpReply> {
    normalizeRequest(url, headers);
    while (true) {
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
        auto reply = co_await HttpReply::make(std::move(streamPtr), streamMode, method == "HEAD");
        if (!reply) {
            if (fromPool) {
                continue;
            }
            co_return Unexpected(reply.error());
        }
        parseReply(reply.value(), url);
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

inline auto HttpSession::parseReply(HttpReply &reply, const Url &url) -> void {
    // Update cookie here
    if (!mCookieJar) {
        return;
    }
    const auto cookies = reply.headers().values(HttpHeaders::SetCookie);
    for (const auto &setCookie : cookies) {
        for (auto &cookie : HttpCookie::parse(setCookie)) {
            cookie.normalize(url);
            mCookieJar->insertCookie(cookie);
        }
    }
}

inline auto HttpSession::connect(const Url &url, bool &fromPool) -> IoTask<std::unique_ptr<HttpStream> > {
    // Check proxy
    auto     scheme = std::string(url.scheme());
    auto     host   = std::string(url.host());
    uint16_t port;

    if (auto p = url.port(); !p) {
        // Get port by scheme
        auto ent = ::getservbyname(scheme.c_str(), "tcp");
        if (!ent) {
            ILIAS_ERROR("Http", "Failed to get port for scheme: {}", scheme);
            co_return Unexpected(SystemError::fromErrno());
        }
        port = ::ntohs(ent->s_port);
    }
    else {
        port = *p;
    }

    HttpEndpoint endpoint {.scheme = scheme, .host = host, .port = port, .proxy = mProxy};

    // Try get mutex
    auto lock = co_await mWorkersMutex.uniqueLock();
    if (!lock) {
        co_return Unexpected(lock.error());
    }
    ILIAS_TRACE("Http", "Got workers mutex");

    auto [it, emplace] = mWorkers.try_emplace(endpoint, endpoint);
    auto &[_, worker] = *it;
    if (emplace) {
        // New worker? add the cleanup task
#if !defined(ILIAS_NO_SSL)
        worker.setSslContext(mSslCtxt);
#endif

        mScope.spawn([this](auto it) -> Task<> {
            auto &[_, worker] = *it;
            if (!co_await worker.quitEvent()) {
                co_return;
            }
            if (auto lock = co_await mWorkersMutex.uniqueLock(); lock) {
                ILIAS_INFO("Http", "Session got {} worker quit, remove it", (void*) &worker);
                mWorkers.erase(it);
            }
        }, it);
    }
    lock->unlock();
    co_return co_await worker.newStream();
}

ILIAS_NS_END