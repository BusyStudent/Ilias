#include <ilias/platform/delegate.hpp>
#include <ilias/platform.hpp>
#include <ilias/buffer.hpp>
#include <ilias/net.hpp>
#include <ilias/tls.hpp>
#include <ilias/io.hpp>
#include <ilias/testing.hpp>
#include <gtest/gtest.h>

using namespace ILIAS_NAMESPACE;
using namespace ILIAS_NAMESPACE::literals;
using namespace std::literals;

// For V4 Address
TEST(Address4, Parse) {
    EXPECT_EQ(IPAddress4::fromString("0.0.0.0").value(), IPAddress4::any());
    EXPECT_EQ(IPAddress4::fromString("255.255.255.255").value(), IPAddress4::none());
    EXPECT_EQ(IPAddress4::fromString("255.255.255.255").value(), IPAddress4::broadcast());
    EXPECT_EQ(IPAddress4::fromString("127.0.0.1").value(), IPAddress4::loopback());

    // Fail cases
    EXPECT_FALSE(IPAddress4::fromString("::1").has_value());
    EXPECT_FALSE(IPAddress4::fromString("::").has_value());
    EXPECT_FALSE(IPAddress4::fromString("127.0.0.1:8080").has_value());
    EXPECT_FALSE(IPAddress4::fromString("256.256.256.256").has_value());

    EXPECT_FALSE(IPAddress4::fromString("127x0.0.1").has_value());
    EXPECT_FALSE(IPAddress4::fromString("127.0.0.1x").has_value());
    EXPECT_FALSE(IPAddress4::fromString("127.0.0x1").has_value());
    EXPECT_FALSE(IPAddress4::fromString("127.0x.1").has_value());
    EXPECT_FALSE(IPAddress4::fromString("127.x.0.1").has_value());
    EXPECT_FALSE(IPAddress4::fromString("127.0.0.1.").has_value());

    EXPECT_FALSE(IPAddress4::fromString("的贷记卡就是").has_value());
    EXPECT_FALSE(IPAddress4::fromString("114.114.114.114.114.114.114.114").has_value());
}

TEST(Address4, ToString) {
    EXPECT_EQ(IPAddress4::fromString("0.0.0.0").value().toString(), "0.0.0.0");
    EXPECT_EQ(IPAddress4::fromString("255.255.255.255").value().toString(), "255.255.255.255");
    EXPECT_EQ(IPAddress4::fromString("127.0.0.1").value().toString(), "127.0.0.1");

    EXPECT_EQ(IPAddress4::any().toString(), "0.0.0.0");
    EXPECT_EQ(IPAddress4::broadcast().toString(), "255.255.255.255");
    EXPECT_EQ(IPAddress4::loopback().toString(), "127.0.0.1");

#if !defined(ILIAS_NO_FORMAT)
    EXPECT_EQ(fmtlib::format("{}", IPAddress4::any()), "0.0.0.0");
    EXPECT_EQ(fmtlib::format("{}", IPAddress4::broadcast()), "255.255.255.255");
    EXPECT_EQ(fmtlib::format("{}", IPAddress4::loopback()), "127.0.0.1");
#endif
}

TEST(Address4, Span) {
    auto addr = IPAddress4::none();
    auto span = addr.span();
    EXPECT_EQ(span[0], std::byte {255});
    EXPECT_EQ(span[1], std::byte {255});
    EXPECT_EQ(span[2], std::byte {255});
    EXPECT_EQ(span[3], std::byte {255});
}

TEST(Address4, Compare) {
    EXPECT_EQ(IPAddress4::none(), IPAddress4::none());
    EXPECT_NE(IPAddress4::none(), IPAddress4::any());
    EXPECT_NE(IPAddress4::none(), IPAddress4::loopback());
}

// For V6 Address
TEST(Address6, Parse) {
    EXPECT_EQ(IPAddress6::fromString("::1").value(), IPAddress6::loopback());
    EXPECT_EQ(IPAddress6::fromString("::").value(), IPAddress6::any());

    EXPECT_FALSE(IPAddress6::fromString("0.0.0.0").has_value());
    EXPECT_FALSE(IPAddress6::fromString("255.255.255.255").has_value());
    EXPECT_FALSE(IPAddress6::fromString("127.0.0.1").has_value());
    EXPECT_FALSE(IPAddress6::fromString("127.0.0.1:8080").has_value());
    EXPECT_FALSE(IPAddress6::fromString("256.256.256.256").has_value());
    EXPECT_FALSE(IPAddress6::fromString("::ffff:256.256.256.256").has_value());
    EXPECT_FALSE(IPAddress6::fromString("asdkljakldjasdnm,sa南萨摩").has_value());
    EXPECT_FALSE(IPAddress6::fromString("::ffff:1121212121:121212:sa1212121211212121212121:12121212121:as2a1s2a1212").has_value());
}

TEST(Address6, Compare) {
    EXPECT_EQ(IPAddress6::loopback(), IPAddress6::loopback());
    EXPECT_NE(IPAddress6::loopback(), IPAddress6::any());
    EXPECT_NE(IPAddress6::loopback(), IPAddress6::none());
}

// For V4 / 6 Address
TEST(Address, Parse) {
    EXPECT_EQ(IPAddress("0.0.0.0").family(), AF_INET);
    EXPECT_EQ(IPAddress("255.255.255.255").family(), AF_INET);
    EXPECT_EQ(IPAddress("127.0.0.1").family(), AF_INET);
    
    EXPECT_EQ(IPAddress("::1").family(), AF_INET6);
    EXPECT_EQ(IPAddress("::").family(), AF_INET6);
    EXPECT_EQ(IPAddress("::ffff:192.168.1.1").family(), AF_INET6);

    EXPECT_FALSE(IPAddress::fromString("127.0.0.1:8080").has_value());
    EXPECT_FALSE(IPAddress::fromString("256.256.256.256").has_value());
    EXPECT_FALSE(IPAddress::fromString("::ffff:256.256.256.256").has_value());
}

TEST(Address, ToString) {
    EXPECT_EQ(IPAddress(IPAddress4::any()).toString(), "0.0.0.0");
    EXPECT_EQ(IPAddress(IPAddress4::none()).toString(), "255.255.255.255");
}

TEST(Address, Compare) {
    EXPECT_EQ(IPAddress(), IPAddress());
    EXPECT_EQ(IPAddress(IPAddress4::any()), IPAddress(IPAddress4::any()));
    EXPECT_NE(IPAddress(IPAddress4::any()), IPAddress(IPAddress4::none()));
    EXPECT_EQ(IPAddress(IPAddress6::loopback()), IPAddress(IPAddress6::loopback()));
    EXPECT_NE(IPAddress(IPAddress6::loopback()), IPAddress(IPAddress6::any()));
    EXPECT_NE(IPAddress(IPAddress4::loopback()), IPAddress(IPAddress6::none()));
    EXPECT_NE(IPAddress(IPAddress4::loopback()), IPAddress());
}

TEST(Endpoint, Parse4) {
    IPEndpoint endpoint1("127.0.0.1:8080");
    EXPECT_TRUE(endpoint1.isValid());
    EXPECT_EQ(endpoint1.address(), "127.0.0.1");
    EXPECT_EQ(endpoint1.port(), 8080);
    std::cout << endpoint1.toString() << std::endl;

    IPEndpoint endpoint2("127.0.0.1:11451");
    EXPECT_TRUE(endpoint2.isValid());
    EXPECT_EQ(endpoint2.address(), "127.0.0.1");
    EXPECT_EQ(endpoint2.port(), 11451);
    std::cout << endpoint2.toString() << std::endl;


    IPEndpoint endpoint3("127.0.0.1:65535");
    EXPECT_TRUE(endpoint3.isValid());
    EXPECT_EQ(endpoint3.address(), "127.0.0.1");
    EXPECT_EQ(endpoint3.port(), 65535);
    std::cout << endpoint3.toString() << std::endl;

    // False test cases
    IPEndpoint endpoint4("127.0.0.1:65536");
    EXPECT_FALSE(endpoint4.isValid());
    std::cout << endpoint4.toString() << std::endl;

    IPEndpoint endpoint5("127.0.0.1:8080:8080");
    EXPECT_FALSE(endpoint5.isValid());
    std::cout << endpoint5.toString() << std::endl;

    IPEndpoint endpoint6("127asdlllll:askasjajskajs");
    EXPECT_FALSE(endpoint6.isValid());
    std::cout << endpoint6.toString() << std::endl;

    IPEndpoint endpoint7("127.0.0.1"); // Only IP address
    EXPECT_FALSE(endpoint7.isValid());
    std::cout << endpoint7.toString() << std::endl;

    IPEndpoint endpoint8("127.0.0.1:"); // Only port
    EXPECT_FALSE(endpoint8.isValid());
    std::cout << endpoint8.toString() << std::endl;

    IPEndpoint endpoint9(":8080"); // Only port
    EXPECT_FALSE(endpoint9.isValid());
    std::cout << endpoint9.toString() << std::endl;

    IPEndpoint endpoint10("127.0.0.1:8080:8080"); // Too many colons
    EXPECT_FALSE(endpoint10.isValid());
    std::cout << endpoint10.toString() << std::endl;

    IPEndpoint endpoint11("127.0.0.1.11.11.11.11.11.11.11.11:8080"); // Too long
    EXPECT_FALSE(endpoint11.isValid());
    std::cout << endpoint11.toString() << std::endl;

    IPEndpoint endpoint12(":"); // Only :
    EXPECT_FALSE(endpoint12.isValid());
    std::cout << endpoint12.toString() << std::endl;
}

TEST(Endpoint, Parse6) {
    IPEndpoint endpoint1("[::1]:8080");
    EXPECT_TRUE(endpoint1.isValid());
    EXPECT_EQ(endpoint1.address(), "::1");
    EXPECT_EQ(endpoint1.port(), 8080);
    std::cout << endpoint1.toString() << std::endl;

    IPEndpoint endpoint2("[::1]:11451");
    EXPECT_TRUE(endpoint2.isValid());
    EXPECT_EQ(endpoint2.address(), "::1");
    EXPECT_EQ(endpoint2.port(), 11451);
    std::cout << endpoint2.toString() << std::endl;

    IPEndpoint endpoint3("[::1]:65535");
    EXPECT_TRUE(endpoint3.isValid());
    EXPECT_EQ(endpoint3.address(), "::1");
    EXPECT_EQ(endpoint3.port(), 65535);
    std::cout << endpoint3.toString() << std::endl;

    // False test cases
    IPEndpoint endpoint4("[::1]:65536");
    EXPECT_FALSE(endpoint4.isValid());
    std::cout << endpoint4.toString() << std::endl;

    IPEndpoint endpoint5("[askasjajskajs]:8080");
    EXPECT_FALSE(endpoint5.isValid());
    std::cout << endpoint5.toString() << std::endl;

    IPEndpoint endpoint6("[]:1145");
    EXPECT_FALSE(endpoint6.isValid());
    std::cout << endpoint6.toString() << std::endl;

    IPEndpoint endpoint7("[aslakkkkkkkkkkkkkkkkkkkkasllaskjlask伯纳斯卡扣设计::1]:8080:8080");
    EXPECT_FALSE(endpoint7.isValid());
    std::cout << endpoint7.toString() << std::endl;
}

TEST(Endpoint, Access4) {
    IPEndpoint endpoint("127.0.0.1:8080");
    EXPECT_TRUE(endpoint.isValid());
    EXPECT_EQ(endpoint.address4(), IPAddress4::loopback());
}

TEST(Endpoint, Access6) {
    IPEndpoint endpoint("[::1]:8080");
    EXPECT_TRUE(endpoint.isValid());
    EXPECT_EQ(endpoint.address6(), IPAddress6::loopback());
}

TEST(Endpoint, Compare) {
    EXPECT_EQ(IPEndpoint(IPAddress4::loopback(), 8080), "127.0.0.1:8080");
    EXPECT_EQ(IPEndpoint("127.0.0.1:8080"), "127.0.0.1:8080");
    EXPECT_EQ(IPEndpoint("[::1]:8080"), "[::1]:8080");
    EXPECT_NE(IPEndpoint("[::1]:8080"), "127.0.0.1:8080");
    EXPECT_EQ(IPEndpoint(), IPEndpoint());
}

TEST(Endpoint, Invalid) {
    IPEndpoint endpoint7;
    EXPECT_FALSE(endpoint7.isValid());
}

TEST(Endpoint, ToString) {
    IPEndpoint endpoint(IPAddress4::any(), 8080);
    EXPECT_EQ(endpoint.toString(), "0.0.0.0:8080");

#if !defined(ILIAS_NO_FORMAT)
    EXPECT_EQ(fmtlib::format("{}", endpoint), "0.0.0.0:8080");
#endif

    IPEndpoint endpoint2(IPAddress6::none(), 8080);
    EXPECT_EQ(endpoint2.toString(), "[::]:8080");

#if !defined(ILIAS_NO_FORMAT)
    EXPECT_EQ(fmtlib::format("{}", endpoint2), "[::]:8080");
#endif
}

ILIAS_TEST(Net, Tcp) {
    {
        auto listener = (co_await TcpListener::bind("127.0.0.1:0")).value();
        auto endpoint = listener.localEndpoint().value();
        {
            auto client = [&]() -> Task<void> {
                auto strean =  (co_await TcpStream::connect(endpoint)).value();
                EXPECT_TRUE(co_await strean.writeAll("Hello, World!"_bin));
            };
            std::string content;
            auto handle = spawn(client());
            auto [peer, _] = (co_await listener.accept()).value();
            EXPECT_TRUE(co_await peer.readToEnd(content));
            EXPECT_TRUE(co_await std::move(handle));
            EXPECT_EQ(content, "Hello, World!");
        }
    }
    {
        // Test bind with configure
        auto configure = [](SocketView view) {
            return view.setOption(sockopt::ReuseAddress(true));
        };
        auto listener = (co_await TcpListener::bind("127.0.0.1:0", SOMAXCONN, configure)).value();
    }
}

ILIAS_TEST(Net, Udp) {
    std::byte buffer[1024] {};
    auto client = (co_await UdpClient::bind("127.0.0.1:0")).value();

    EXPECT_TRUE((co_await client.poll(PollEvent::Out)).has_value());
    // Test the cancel
    {
        auto handle = spawn(client.recvfrom(buffer));
        handle.stop();
        EXPECT_FALSE((co_await std::move(handle)).has_value());
    }

    {
        auto handle = spawn(client.poll(PollEvent::In));
        handle.stop();
        EXPECT_FALSE((co_await std::move(handle)).has_value());
    }

    // Test send data
    auto receiver = (co_await UdpClient::bind("127.0.0.1:0")).value();
    auto endpoint = receiver.localEndpoint().value();

    { // Test normal sendto & recvfrom
        EXPECT_TRUE(co_await client.sendto("Hello, World!"_bin, endpoint));
        std::byte buffer[1024] {};
        auto [n, _] = (co_await receiver.recvfrom(buffer)).value();
        EXPECT_EQ(n, 13);
        EXPECT_EQ(std::string_view(reinterpret_cast<char *>(buffer), n), "Hello, World!");
    }

    {
        // Test vector version
        Buffer buffers [] = {
            "Hello"_bin,
            ", "_bin,
            "World!"_bin,
        };
        EXPECT_EQ(co_await client.sendto(buffers, endpoint), 13);

        char hello[5] {};
        char comma[2] {};
        char world[6] {};
        MutableBuffer mutBuffers [] = {
            makeBuffer(hello),
            makeBuffer(comma),
            makeBuffer(world),
        };
        auto [n, _] = (co_await receiver.recvfrom(mutBuffers)).value();
        EXPECT_EQ(n, 13);
    }

    {
        // Test bind with configure
        auto configure = [](SocketView view) {
            return view.setOption(sockopt::ReuseAddress(true));
        };
        auto any = (co_await UdpClient::bind("127.0.0.1:0", configure)).value();
    }
}

ILIAS_TEST(Net, Http) {
    auto info = (co_await AddressInfo::fromHostname("www.baidu.com", "http")).value();
    auto client = (co_await TcpStream::connect(info.endpoints().at(0))).value();
    auto stream = BufStream(std::move(client));

    // Prepare payload
    (co_await stream.writeAll("GET / HTTP/1.1\r\nHost: www.baidu.com\r\nConnection: close\r\n\r\n"_bin)).value();
    (co_await stream.flush()).value();

    // Read response
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

#if defined(ILIAS_TLS)
ILIAS_TEST(Net, Https) {
    TlsContext sslCtxt;
    auto info = (co_await AddressInfo::fromHostname("www.baidu.com", "https")).value();
    auto client = (co_await TcpStream::connect(info.endpoints().at(0))).value();
    auto ssl = TlsStream(sslCtxt, std::move(client));

    // Do ssl here
    auto alpn = std::to_array({"http/1.1"sv});
    ssl.setHostname("www.baidu.com");
    ssl.setAlpnProtocols(alpn);
    (co_await ssl.handshake()).value();

    std::cout << "Alpn Result : " << ssl.alpnSelected() << std::endl;

    // Prepare payload, as same as Http
    auto stream = BufStream(std::move(ssl));
    (co_await stream.writeAll("GET / HTTP/1.1\r\nHost: www.baidu.com\r\nConnection: close\r\n\r\n"_bin)).value();
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
#endif // ILIAS_SSL

class IoEventLoop : public DelegateContext<PlatformContext> {
public:
    auto post(void (*fn)(void*), void* arg) -> void override {
        mLoop.post(fn, arg);
    }

    auto run(runtime::StopToken token) -> void override {
        mLoop.run(std::move(token));
    }
private:
    EventLoop mLoop;
};


int main(int argc, char** argv) {
    ILIAS_LOG_SET_LEVEL(ILIAS_TRACE_LEVEL);
    ILIAS_TEST_SETUP_UTF8();
    IoEventLoop ctxt;
    ctxt.install();

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}