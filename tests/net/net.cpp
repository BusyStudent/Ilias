#include <ilias/platform.hpp>
#include <ilias/buffer.hpp>
#include <ilias/net.hpp>
#include <gtest/gtest.h>
#include "testing.hpp"

using namespace ILIAS_NAMESPACE;
using namespace ILIAS_NAMESPACE::literals;

CORO_TEST(Net, Tcp) {
    auto listener = (co_await TcpListener::bind("127.0.0.1:0")).value();
}

CORO_TEST(Net, Udp) {
    std::byte buffer[1024] {};
    auto client = (co_await UdpClient::bind("127.0.0.1:0")).value();

    CORO_ASSERT_TRUE((co_await client.poll(PollEvent::Out)).has_value());
    // Test the cancel
    {
        auto handle = spawn(client.recvfrom(buffer));
        handle.stop();
        CORO_ASSERT_EQ(co_await std::move(handle), std::nullopt);
    }

    {
        auto handle = spawn(client.poll(PollEvent::In));
        handle.stop();
        CORO_ASSERT_EQ(co_await std::move(handle), std::nullopt);
    }
}

CORO_TEST(Net, Http) {
    auto info = (co_await AddressInfo::fromHostname("www.baidu.com", "http")).value();
    auto client = (co_await TcpClient::connect(info.endpoints().at(0))).value();

    // Prepare payload
    (co_await client.writeAll("GET / HTTP/1.1\r\nHost: www.baidu.com\r\nConnection: close\r\n\r\n"_bin)).value();
    (co_await client.flush()).value();

    // Read response
    std::byte buffer[1024] {};
    while (true) {
        auto n = (co_await client.read(buffer)).value();
        if (n == 0) {
            break;
        }
        auto view = std::span(buffer, n);
        auto str = std::string_view(reinterpret_cast<const char*>(view.data()), view.size());
        std::cout << str;
    }
}

int main(int argc, char** argv) {
    ILIAS_LOG_SET_LEVEL(ILIAS_TRACE_LEVEL);
    CORO_USE_UTF8();
    PlatformContext ctxt;
    ctxt.install();

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}