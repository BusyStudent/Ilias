#pragma once

#include "../socks5.hpp"
#include "../zlib.hpp"
#include "../url.hpp"
#include "../ssl.hpp"
#include "http1.1.hpp"
#include "headers.hpp"
#include "request.hpp"
#include "cookie.hpp"
#include "reply.hpp"
#include <chrono>
#include <array>
#include <map>

ILIAS_NS_BEGIN

class HttpSession;
class HttpReply;


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
     * @brief FAT key for lookup
     * 
     */
    struct Target {
        std::string scheme;
        std::string host;
        uint16_t port = 0;
        Url proxy; //< does it use proxy ?

        auto operator <=>(const Target &) const = default;
    };

    /**
     * @brief Connection used tp keep alive
     * 
     */
    struct Connection {
        std::unique_ptr<HttpConnection> con;
        std::chrono::steady_clock::time_point lastUsed;
        int version; //< The Http version
    };

    /**
     * @brief Actually update the header by hostname and CookieJar...., and connect to the target
     * 
     * @param request 
     * @param extraData 
     * @return Task<HttpReply> 
     */
    auto _sendRequest(HttpRequest request, std::span<const std::byte> extraData = {}) -> Task<HttpReply>;

    /**
     * @brief Update the header by ...
     * 
     * @param request 
     */
    auto _buildRequest(HttpRequest &request) -> void;

    /**
     * @brief Build the http reply from a stream and update current session's info
     * 
     * @param request
     * @param stream 
     * @return Task<HttpReply> 
     */
    auto _buildReply(HttpRequest &request, std::unique_ptr<HttpStream> stream) -> Task<HttpReply>;
    
    /**
     * @brief Connect to the target and open a new stream
     * 
     * @param url 
     * @param proxy
     * @param fromCache
     * @return Task<std::unique_ptr<HttpStream> > 
     */
    auto _connect(const Url &url, const Url &proxy, bool &fromCache) -> Task<std::unique_ptr<HttpStream> >;


    IoContext &mIoContext;

#if !defined(ILIAS_NO_SSL)
    SslContext mSslContext;
#endif

    // TODO: Improve the cache algorithm
    std::multimap<Target, Connection> mConnections;
    size_t mMaxConnectionPerHost = 6; //< Http 1.1x max connection per host
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
    request.setOperation(op);
    int n = 0;
    while (true) {
#if 1
        // In here we add timeout check for avoid waiting to long
        auto [result, timeout] = co_await WhenAny(_sendRequest(request, extraData), Sleep(request.transferTimeout()));
        if (timeout) {
            co_return Unexpected(Error::TimedOut);
        }
        if (!result) {
            co_return Unexpected(Error::Canceled);
        }
        auto reply = std::move(*result);
#else
        auto reply = co_await _sendRequest(request, extraData);
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
inline auto HttpSession::_sendRequest(HttpRequest request, std::span<const std::byte> payload) -> Task<HttpReply> {
    const auto &url = request.url();
    const auto host = url.host();
    const auto port = url.port();
    const auto scheme = url.scheme();

    // Check args
    if (!url.portOrScheme() || (scheme != "https" && scheme != "http")) {
        co_return Unexpected(Error::InvalidArgument);
    }
#if defined(ILIAS_NO_SSL)
    if (scheme == "https") {
        co_return Unexpected(Error::OperationNotSupported);
    }
#endif


    // Update header here
    _buildRequest(request);

while (true) {
    auto fromCache = false;
    auto stream = std::unique_ptr<HttpStream>();
    if (auto val = co_await _connect(url, mProxy, fromCache); !val) {
        co_return Unexpected(val.error());
    }
    else {
        stream = std::move(*val);
    }

    // Send the request
    if (auto val = co_await stream->sendRequest(request, payload); !val) {
        co_return Unexpected(val.error());
    }
    if (auto val = co_await _buildReply(request, std::move(stream)); !val) {
        if (fromCache) {
            // Maybe the connection is closed
            ::fprintf(stderr, "[Http] Failed to get reply by %s, retry\n", val.error().toString().c_str());
            continue;
        }
    }
    else {
        co_return val;
    }
}
}

inline auto HttpSession::_buildRequest(HttpRequest &request) -> void {
    // Cookie
    if (mCookieJar) {
        auto cookies = mCookieJar->cookiesForUrl(request.url());
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
            request.setHeader("Cookie", cookieString);
        }
    }

    // Accept
    if (request.header(HttpHeaders::Accept).empty()) {
        request.setHeader("Accept", "*/*");
    }

    // Accept-Encoding
    if (request.header("Accept-Encoding").empty()) {
#if !defined(ILIAS_NO_ZLIB)
        request.setHeader("Accept-Encoding", "gzip, deflate");
#else
        request.setHeader("Accept-Encoding", "identity");
#endif
    }

}

inline auto HttpSession::_buildReply(HttpRequest &request, std::unique_ptr<HttpStream> stream) -> Task<HttpReply> {
    ILIAS_ASSERT(stream);

    auto reply = co_await HttpReply::from(request.url(), std::move(stream));
    if (!reply) {
        co_return reply;
    }

    // Update cookie here
    const auto cookies = reply->headers().values(HttpHeaders::SetCookie);
    if (!mCookieJar) {
        co_return reply;
    }
    for (const auto &setCookie : cookies) {
        for (auto &cookie : HttpCookie::parse(setCookie)) {
            if (cookie.domain().empty()) {
                cookie.setDomain(request.url().host());
            }
            mCookieJar->insertCookie(cookie);
        }
    }
    co_return reply;
}

inline auto HttpSession::_connect(const Url &url, const Url &proxy, bool &fromCache) -> 
    Task<std::unique_ptr<HttpStream> > 
{
    const auto host = url.host();
    auto port = uint16_t(0);

    if (auto urlPort = url.port(); !urlPort) {
        // Unspecified port
        if (url.scheme() == "http") {
            port = 80;
        }
        else if (url.scheme() == "https") {
            port = 443;
        } 
        else {
            co_return Unexpected(Error::HttpBadRequest);
        }
    }
    else {
        port = urlPort.value();
    }

    // Check cache
    Target target {
        .scheme = std::string(url.scheme()),
        .host = std::string(host),
        .port = port,
        .proxy = proxy
    };
    auto [begin, end] = mConnections.equal_range(target);
    for (auto it = begin; it != end; ) {
        auto &[con, lastTime, version] = it->second;
        if (con->isBroken()) {
            it = mConnections.erase(it);
            continue;
        }
        if (con->activeStreams() == 0 || version == 2) {
            // Http2 or Idle Http1 Connection
            fromCache = true;
            co_return co_await con->newStream();
        }
        ++it;
    }

    // Do connet
    IStreamClient wrapped;
    if (!proxy.empty()) {
        // Do connect proxy ...
        const auto proxyPort = proxy.port();
        const auto proxyHost = IPAddress::fromString(proxy.host().data());
        if (!proxyPort) {
            co_return Unexpected(Error::HttpBadRequest);
        }
        if (proxy.scheme() != "socks5" && proxy.scheme() != "socks5h") {
            co_return Unexpected(Error::HttpBadRequest);
        }

        auto proxyClient = Socks5Client(mIoContext, proxyHost.family());
        proxyClient.setServer(IPEndpoint(proxyHost, proxyPort.value()));
        if (auto val = co_await proxyClient.connect(host, port); !val) {
            co_return Unexpected(val.error());
        }
        wrapped = std::move(proxyClient);
    }
    else {
        const auto info = AddressInfo::fromHostname(host.data());
        if (!info) {
            co_return Unexpected(info.error());
        }
        const auto addrs = info->addresses();
        if (addrs.empty()) {
            co_return Unexpected(Error::HostNotFound);
        }
        Result<void> lastStatus;
        for (const auto &addr : addrs) {
            TcpClient client(mIoContext, addr.family());
            lastStatus = co_await client.connect(IPEndpoint(addr, port));
            fprintf(stderr, "[Http] Do Tcp connect to %s: %s\n",
                addr.toString().c_str(), 
                lastStatus ? "ok" : lastStatus.error().toString().c_str()
            );
            if (!lastStatus && lastStatus.error() == Error::Canceled) {
                break;
            }
            if (!lastStatus) {
                continue;
            }
            // Got it
            wrapped = std::move(client);
            break;
        }
        if (!lastStatus) {
            co_return Unexpected(lastStatus.error());
        }
    }

    // TODO: ALPN and HTTP2
    bool h2 = false;

    // Check if we need wrap ssl
    if (url.scheme() == "https") {
        // Wrap ssl
#if !defined(ILIAS_NO_SSL)
        constexpr std::string_view alpns [] {
            "http/1.1"
        };
        static_assert(SslSniExtension<SslClient<> >); //< The ssl impl must support SNI
        auto ssl = SslClient(mSslContext, std::move(wrapped));
        ssl.setHostname(host);
        ssl.setAlpn(alpns);

        // Begin Handshake make ssl connection
        if (auto val = co_await ssl.handshake(); !val) {
            co_return Unexpected(val.error());
        }
        auto selected = ssl.alpnSelected();
        h2 = (selected == "h2");
        wrapped = std::move(ssl);
        ::fprintf(stderr, "[Http] ALPN Selected: %s\n", std::string(selected).c_str());
#else
        ::abort();
#endif
    }

    // Create The http 1.1 on it
    auto con = Http1Connection::make(std::move(wrapped));
    auto ptr = con.get();
    
    // Add to cache
    mConnections.emplace(target, Connection(std::move(con), std::chrono::steady_clock::now(), 1));
    co_return co_await ptr->newStream();
}

ILIAS_NS_END