#pragma once

#include <ilias/http/transfer.hpp>
#include <ilias/http/http1.1.hpp>
#include <ilias/task/when_any.hpp>
#include <ilias/sync/scope.hpp>
#include <ilias/net/addrinfo.hpp>
#include <ilias/net/socks5.hpp>
#include <ilias/net/tcp.hpp>
#include <ilias/url.hpp>
#include <ilias/ssl.hpp>
#include <ilias/log.hpp>
#include <deque>

ILIAS_NS_BEGIN

/**
 * @brief The info for describe the endpoint of an http site
 * 
 */
class HttpEndpoint {
public:
    std::string scheme;
    std::string host;
    uint16_t    port {0};
    Url         proxy;

    auto operator<=>(const HttpEndpoint &other) const = default;
};

/**
 * @brief The http worker, for manage a connection pool for multiplexing, if no connection alive, it will quit
 * 
 */
class HttpWorker {
public:
    enum QuitPolicy {
        QuitOnNoConnections, //< If no connection alive, the quit event will be set
    };

    HttpWorker(const HttpEndpoint &endpoint);
    HttpWorker(const HttpWorker &) = delete;
    ~HttpWorker();

    /**
     * @brief Create a new stream 
     * 
     * @return IoTask<std::unique_ptr<HttpStream> > 
     */
    auto newStream() -> IoTask<std::unique_ptr<HttpStream> >;

    auto quitEvent() -> Event &;

#if !defined(ILIAS_NO_SSL)
    auto setSslContext(SslContext &ctxt) -> void;
#endif

    /**
     * @brief Set the max connection limits on http1.1
     * 
     * @param n 
     */
    auto setMaxConnectionHttp1(size_t n) -> void;
private:
    auto newStream1() -> IoTask<std::unique_ptr<HttpStream> >;
    auto startConnection() -> Task<void>;
    auto connect() const -> IoTask<IStreamClient>;

    TaskScope mScope; //< The scope for limit
    HttpEndpoint mEndpoint; //< The endpoint of the
    Error        mError; //< The error

    // Quit...
    Event        mQuitEvent; //< If set, the worker quiting
    QuitPolicy   mQuitPolicy = QuitOnNoConnections;

    // Init...
    Event        mInitEvent; //< The event of init

#if !defined(ILIAS_NO_SSL)
    SslContext *mSslCtxt = nullptr;
#endif // !defined(ILIAS_NO_SSL)

    // Http1.1...
    std::deque<
        Http1Connection *
    >             mIdleConnection1;
    size_t        mMaxConenction1 = 5; //< The max 
    size_t        mConnection1Size = 0; //< The logical size of connections, (including initing conenction)
    Event         mConenction1Idle; //< The event used to notify there is some 

    // Http2
    bool mHttp2Aavailable = false; //< Does the remote site support http2?
};

inline HttpWorker::HttpWorker(const HttpEndpoint &endpoint) : mEndpoint(endpoint) {
    mConnection1Size += 1;
    mScope.spawn(&HttpWorker::startConnection, this);
}

inline HttpWorker::~HttpWorker() {
    mScope.cancel();
    mScope.wait();
}

inline auto HttpWorker::newStream() -> IoTask<std::unique_ptr<HttpStream> > {
    if (mQuitEvent) { //< The worker is quit
        co_return Unexpected(Error::Canceled);
    }
    // Wait the first
    if (auto ret = co_await mInitEvent; !ret) {
        co_return Unexpected(ret.error());
    }
    if (!mError.isOk()) {
        co_return Unexpected(mError);
    }
    if (mHttp2Aavailable) {
        // TODO: Forward to http2 ...

    }
    // Http1.1
    co_return co_await newStream1();
}

inline auto HttpWorker::quitEvent() -> Event & {
    return mQuitEvent;
}

#if !defined(ILIAS_NO_SSL)
inline auto HttpWorker::setSslContext(SslContext &ctxt) -> void {
    mSslCtxt = &ctxt;
}
#endif

inline auto HttpWorker::setMaxConnectionHttp1(size_t n) -> void {
    mMaxConenction1 = n;
}

inline auto HttpWorker::newStream1() -> IoTask<std::unique_ptr<HttpStream>> {
    while (mIdleConnection1.empty()) {
        mConenction1Idle.clear();
        if (mConnection1Size < mMaxConenction1) {
            // We can start a new connection
            mConnection1Size += 1;
            mScope.spawn(&HttpWorker::startConnection, this);
            ILIAS_TRACE("HttpWorker", "Start a new connection, size: {}", mConnection1Size);
        }
        ILIAS_TRACE("HttpWorker", "No idle connection, waiting for idle");
        // Waiting for the idle
        auto [idle, quit] = co_await whenAny(mConenction1Idle, mQuitEvent);
        if (quit) {
            if (!quit->has_value()) { //< Maybe cancel
                co_return Unexpected(quit->error());
            }
            // Quit requested
            co_return Unexpected(mError);
        }
        if (!idle->has_value()) { //< Maybe cancel
            co_return Unexpected(idle->error());
        }
    }
    ILIAS_TRACE("HttpWorker", "Has idle connection {}", mIdleConnection1.size());
    ILIAS_ASSERT(!mIdleConnection1.empty());
    auto con = mIdleConnection1.front();
    mIdleConnection1.pop_front();
    co_return co_await con->newStream();
}

inline auto HttpWorker::startConnection() -> Task<void> {
    do {
        auto stream = co_await connect();
        if (!stream) {
            // Failed, we should decrease the size
            mError = stream.error();
            break;
        }
        // Build the connection
        Http1Connection con(std::move(*stream));
        auto findConnection = [this, &con]() {
            return std::find(mIdleConnection1.begin(), mIdleConnection1.end(), &con);
        };
        mInitEvent.set(); //< If still in init stage
        while (true) {
            ILIAS_TRACE("HttpWorker", "Adding {} into idle list", (void*) &con);
            mIdleConnection1.emplace_back(&con);
            mConenction1Idle.set();
            con.idleEvent().clear(); //< Clear the event, make us got awkae in next idle
            if (!co_await con.idleEvent()) {
                // Failed to wait the idle event?, may cancel
                break;
            }
            ILIAS_TRACE("HttpWorker", "Connection {} into idle state", (void*) &con);
            if (con.isClosed()) {
                // Close 
                break;
            }
            // Add self into the idle list
    #if !defined(NDEBUG)
            ILIAS_ASSERT(findConnection() == mIdleConnection1.end());
            // We should not in the idle list
    #endif
        }
        // Cleanup self
        if (auto it = findConnection(); it != mIdleConnection1.end()) {
            ILIAS_TRACE("HttpWorker", "Connection {} quit but still in idle list, remove it", (void*) &con);
            mIdleConnection1.erase(it);
        }
    }
    while (false);
    mConnection1Size -= 1;
    if (mConnection1Size == 0) {
        ILIAS_INFO("HttpWorker", "No conenction alive, quiting");
        mInitEvent.set();
        mQuitEvent.set();
    }
    co_return;
}

inline auto HttpWorker::connect() const -> IoTask<IStreamClient> {
    const auto &scheme = mEndpoint.scheme;
    const auto &proxy = mEndpoint.proxy;
    const auto &host = mEndpoint.host;
    const auto &port = mEndpoint.port;
    auto &&ctxt = co_await currentIoContext();
    IStreamClient cur;

    if (!proxy.empty()) {
        // Proxy
        auto proxyPort = proxy.port();
        if (!proxyPort || (proxy.scheme() != "socks5" && proxy.scheme() != "socks5h")) {
            ILIAS_ERROR("Http", "Invalid proxy: {}", proxy);
            co_return Unexpected(Error::HttpBadRequest);
        }
        auto endpoint = IPEndpoint(std::string(proxy.host()).c_str(), *proxyPort);
        if (!endpoint.isValid()) {
            ILIAS_ERROR("Http", "Invalid proxy: {}", proxy);
            co_return Unexpected(Error::HttpBadRequest);
        }
        ILIAS_TRACE("Http", "Connecting to the {}:{} by proxy: {}", host, port, proxy);
        TcpClient client(ctxt, endpoint.family());
        if (auto ret = co_await client.connect(endpoint); !ret) {
            co_return Unexpected(ret.error());
        }
        // Do Socks5 handshake
        Socks5Connector socks5(client);
        if (auto ret = co_await socks5.connect(host, port); !ret) {
            co_return Unexpected(ret.error());
        }
        cur = std::move(client);
    }
    else { //< No proxy
        auto addrinfo = co_await AddressInfo::fromHostnameAsync(host.c_str(), std::to_string(port).c_str());
        if (!addrinfo) {
            co_return Unexpected(addrinfo.error());
        }
        auto endpoints = addrinfo->endpoints();
        if (endpoints.empty()) {
            co_return Unexpected(Error::HostNotFound);
        }

        // Try connect to all endpoints
        for (size_t idx = 0; idx < endpoints.size(); ++idx) {
            auto     &endpoint = endpoints[idx];
            TcpClient client(ctxt, endpoint.family());
            ILIAS_TRACE("Http", "Trying to connect to {} ({} of {})", endpoint, idx + 1, endpoints.size());
            if (auto ret = co_await client.connect(endpoint); !ret && idx != endpoints.size() - 1) {
                // Try another address
                if (ret.error() != Error::Canceled) {
                    continue;
                }
                ILIAS_TRACE("Http", "Got Cancel, Exiting");
                co_return Unexpected(ret.error());
            }
            else if (!ret) {
                co_return Unexpected(ret.error());
            }
            cur = std::move(client);
            break;
        }
    }

    // Try adding a ssl if needed
#if !defined(ILIAS_NO_SSL)
    if (scheme == "https") {
        SslClient sslClient(*mSslCtxt, std::move(cur));
        sslClient.setHostname(host);
        if (auto ret = co_await sslClient.handshake(); !ret) {
            co_return Unexpected(ret.error());
        }
        cur = std::move(sslClient);
    }
#else
    if (scheme == "https") {
        co_return Unexpected(Error::ProtocolNotSupported);
    }
#endif

    co_return cur;
}

ILIAS_NS_END