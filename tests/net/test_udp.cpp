#include <ilias/testing.hpp>
#include <ilias/net.hpp>
using namespace ilias;
using namespace ilias::literals;
using namespace std::literals;

ILIAS_TEST(Net, Udp) {
    std::byte buffer[1024] {};
    auto client = (co_await UdpSocket::bind("127.0.0.1:0")).value();

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
    auto receiver = (co_await UdpSocket::bind("127.0.0.1:0")).value();
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
        // Test builder
        auto endpoint = IPEndpoint {"127.0.0.1:0"};
        auto socket = co_await UdpBuilder {endpoint.family()}
            .option(sockopt::ReuseAddress(true))
            .option(sockopt::Broadcast(true))
            .bind(endpoint);
        EXPECT_TRUE(socket);

        // Error Path
        socket = co_await UdpBuilder {AF_UNSPEC}
            .option(sockopt::ReuseAddress(true))
            .bind(endpoint);
        EXPECT_FALSE(socket);
    }
}

