#include <ilias/testing.hpp>
#include <ilias/net.hpp>
#include <ilias/io.hpp>
using namespace ilias;
using namespace ilias::literals;
using namespace std::literals;

ILIAS_TEST(Net, AddressInfo) {
    {
        auto info = co_await AddressInfo::fromHostname("www.baidu.com");
        EXPECT_TRUE(info);
    }
    {
        auto info = co_await AddressInfo::fromHostname("impossiblehostname.unknown");
        EXPECT_FALSE(info);
        EXPECT_EQ(info.error(), GaiError::NotFound);
        std::cout << info.error().message() << std::endl;
    }
    {
        auto info = co_await AddressInfo::lookup("www.baidu.com:80");
        EXPECT_TRUE(info);
        for (auto endpoint : info.value()) {
            std::cout << endpoint.toString() << std::endl;
        }
    }
}

ILIAS_TEST(Net, Tcp) {
    {
        auto listener = (co_await TcpListener::bind("127.0.0.1:0")).value();
        auto endpoint = listener.localEndpoint().value();
        { // Test cancel
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
        // Test builder Happy Path
        auto endpoint = IPEndpoint {"127.0.0.1:0"};
        auto listener = co_await TcpBuilder {endpoint.family()}
            .option(sockopt::ReuseAddress(true))
            .bind(endpoint);
        EXPECT_TRUE(listener);

        // Error Path
        listener = co_await TcpBuilder {AF_UNSPEC}
            .option(sockopt::ReuseAddress(true))
            .bind(endpoint);
        EXPECT_FALSE(listener);

        listener = co_await TcpBuilder {endpoint.family()}
            .bind(IPEndpoint {IPAddress6::loopback(), 0}); // Bind 6 address to 4 socket
        EXPECT_FALSE(listener);
    }
}

ILIAS_RTEST(Net, Http) {
    ILIAS_CO_TRY(auto info, co_await AddressInfo::fromHostname("www.baidu.com", "http"));
    ILIAS_CO_TRY(auto client, co_await TcpStream::connect(info.endpoints().at(0)));
    BufStream stream {std::move(client)};

    // Prepare payload
    ILIAS_CO_TRYV(co_await stream.writeAll("GET / HTTP/1.1\r\nHost: www.baidu.com\r\nConnection: close\r\n\r\n"_bin));
    ILIAS_CO_TRYV(co_await stream.flush());

    // Read response
    while (true) {
        ILIAS_CO_TRY(auto line, co_await stream.getline("\r\n"));
        if (line.empty()) { // Header end
            break;
        }
        std::cout << line << std::endl;
    }
    char buffer[4096] {};
    while (true) {
        ILIAS_CO_TRY(auto size, co_await stream.read(makeBuffer(buffer)));
        if (size == 0) {
            break;
        }
        std::cout << std::string_view(buffer, size) << std::endl;
    }
    co_return {};
}