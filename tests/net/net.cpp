#include <ilias/platform.hpp>
#include <ilias/buffer.hpp>
#include <ilias/net.hpp>
#include <ilias/io.hpp>
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
        CORO_ASSERT_FALSE((co_await std::move(handle)).has_value());
    }

    {
        auto handle = spawn(client.poll(PollEvent::In));
        handle.stop();
        CORO_ASSERT_FALSE((co_await std::move(handle)).has_value());
    }
}

CORO_TEST(Net, Http) {
    auto info = (co_await AddressInfo::fromHostname("www.baidu.com", "http")).value();
    auto client = (co_await TcpClient::connect(info.endpoints().at(0))).value();
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
    char buffer[1024] {};
    while (true) {
        auto size = (co_await stream.read(makeBuffer(buffer))).value();
        if (size == 0) {
            break;
        }
        std::cout << std::string_view(buffer, size) << std::endl;
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