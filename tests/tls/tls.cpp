#include <ilias/platform.hpp>
#include <ilias/testing.hpp>
#include <ilias/buffer.hpp>
#include <ilias/net.hpp>
#include <ilias/io.hpp>
#include <ilias/tls.hpp>
#include "certs.inl"

using namespace ILIAS_NAMESPACE::literals;
using namespace ILIAS_NAMESPACE;
using namespace std::literals;

#if defined(ILIAS_TLS)

// Test Client / Server
auto onServer(TlsContext &tlsCtxt, DuplexStream duplexStream) -> IoTask<void> {
    auto stream = TlsStream {tlsCtxt, std::move(duplexStream)};
    if (auto res = co_await stream.handshake(TlsRole::Server); !res) {
        co_return Err(res.error());
    }
    // Read the hello world
    auto content = std::string {};
    if (auto res = co_await stream.readToEnd(content); !res) {
        co_return Err(res.error());
    }
    EXPECT_EQ(content, "Hello World");
    co_return {};
}

auto onClient(TlsContext &tlsCtxt, DuplexStream duplexStream) -> IoTask<void> {
    auto stream = TlsStream {tlsCtxt, std::move(duplexStream)};
    stream.setHostname("localhost");
    if (auto res = co_await stream.handshake(TlsRole::Client); !res) {
        co_return Err(res.error());
    }
    // Send hello world and read back
    if (auto res = co_await stream.writeAll("Hello World"_bin); !res) {
        co_return Err(res.error());
    }
    if (auto res = co_await stream.flush(); !res) {
        co_return Err(res.error());
    }
    if (auto res = co_await stream.shutdown(); !res) {
        co_return Err(res.error());
    }
    co_return {};
}

ILIAS_TEST(Tls, Local) {
    auto clientCtxt = TlsContext { TlsContext::NoDefaultRootCerts | TlsContext::NoVerify };
    auto serverCtxt = TlsContext { TlsContext::NoDefaultRootCerts };

    // Configure it
    EXPECT_TRUE(serverCtxt.useCert(makeBuffer(tlsCertString)));
    EXPECT_TRUE(serverCtxt.usePrivateKey(makeBuffer(tlsKeyString)));

    EXPECT_TRUE(clientCtxt.loadRootCerts(makeBuffer(tlsCertString)));

    auto [clientStream, serverStream] = DuplexStream::make(4096);
    auto [client, server] = co_await whenAll(
        onClient(clientCtxt, std::move(clientStream)), 
        onServer(serverCtxt, std::move(serverStream))
    );
    EXPECT_TRUE(client);
    EXPECT_TRUE(server);
}

auto doHttps(TlsContext &tlsCtxt, std::string_view hostname) -> Task<void> {
    auto info = (co_await AddressInfo::fromHostname(hostname, "https")).value();
    auto client = (co_await TcpStream::connect(info.endpoints().at(0))).value();
    auto ssl = TlsStream {tlsCtxt, std::move(client)};

    // Do ssl here
    auto alpn = std::to_array({"http/1.1"sv});
    ssl.setHostname(hostname);
    ssl.setAlpnProtocols(alpn);
    (co_await ssl.handshake(TlsRole::Client)).value();

    std::cout << "Alpn Result : " << ssl.alpnSelected() << std::endl;

    // Prepare payload, as same as Http
    auto stream = BufStream {std::move(ssl)};
    auto headers = "GET / HTTP/1.1\r\nHost: " + std::string(hostname) + "\r\nConnection: close\r\n\r\n";
    (co_await stream.writeAll(makeBuffer(headers))).value();
    (co_await stream.flush()).value();

    while (true) {
        auto line = (co_await stream.getline("\r\n")).value();
        if (line.empty()) { // Header end
            break;
        }
        std::cout << line << std::endl;
    }
    char buffer[4096] {};
    while (true) {
        auto size = (co_await stream.read(makeBuffer(buffer))).value();
        if (size == 0) {
            break;
        }
        std::cout << std::string_view(buffer, size) << std::endl;
    }
}

ILIAS_TEST(Tls, Https) {
    auto ctxt = TlsContext {};
    co_await doHttps(ctxt, "www.baidu.com");
}

ILIAS_TEST(Tls, NoVerify) {
    auto ctxt = TlsContext { TlsContext::NoVerify };
    co_await doHttps(ctxt, "expired.badssl.com");
}
#endif // ILIAS_TLS

int main(int argc, char** argv) {
    ILIAS_LOG_SET_LEVEL(ILIAS_TRACE_LEVEL);
    ILIAS_TEST_SETUP_UTF8();
    PlatformContext ctxt;
    ctxt.install();

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}