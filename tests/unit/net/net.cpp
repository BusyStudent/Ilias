#include <ilias/platform.hpp>
#include <ilias/buffer.hpp>
#include <ilias/task.hpp>
#include <ilias/net.hpp>
#include <gtest/gtest.h>
#include <random>

#undef min

using namespace ILIAS_NAMESPACE;

TEST(Net, TcpTransfer) {
    // ILIAS_LOG_SET_LEVEL(ILIAS_TRACE_LEVEL);
    auto ctxt = IoContext::currentThread();
    ILIAS_TRACE("test", "create io context");
    TcpListener listener(*ctxt, AF_INET);
    ILIAS_TRACE("test", "create listener");

    ASSERT_TRUE(listener.bind("127.0.0.1:0"));
    ILIAS_TRACE("test", "listener to 127.0.0.1");
    auto endpoint = listener.localEndpoint().value();
    std::cout << endpoint.toString() << std::endl;

    const std::array<size_t, 3> capacities = { 1024, 1024 * 1024, 1024 * 1024 * 1024 };
    for (auto bytesToTransfer : capacities) {
        //< Test from 1MB up to 1GB.
        size_t senderSent = 0;
        size_t receiverReceived = 0;

        std::cout << "bytes to transfer: " << bytesToTransfer << std::endl;

        auto sender = [&]() -> IoTask<void> {
            TcpClient client(*ctxt, AF_INET);
            auto val = co_await client.connect(endpoint);
            if (!val) {
                co_return Unexpected(val.error());
            }
            auto buffer = std::make_unique<std::byte[]>(1024 * 1024);
            while (true) {
                auto n = co_await client.read(makeBuffer(buffer.get(), 1024 * 1024));
                if (!n) {
                    co_return Unexpected(n.error());
                }
                receiverReceived += n.value();
                if (n.value() == 0) {
                    break; //< Done.
                }
            }
            if (auto ret = co_await client.shutdown(); !ret) {
                co_return Unexpected(ret.error());
            }
            co_return {};
        };

        auto receiver = [&]() -> IoTask<void> {
            auto val = co_await listener.accept();
            if (!val) {
                co_return Unexpected(val.error());
            }
            auto [con, endpoint] = std::move(val.value());
            auto buffer = std::make_unique<std::byte[]>(bytesToTransfer);
            ::memset(buffer.get(), 0, bytesToTransfer);
            auto n = co_await con.writeAll(makeBuffer(buffer.get(), bytesToTransfer));
            if (!n) {
                co_return Unexpected(n.error());
            }
            senderSent = n.value();
            co_return {};
        };

        auto [val1, val2] = ilias_wait whenAll(receiver(), sender());
        if (!val1) {
            std::cout << "receiver failed: " << val1.error().message() << std::endl;
        }
        if (!val2) {
            std::cout << "sender failed: " << val2.error().message() << std::endl;
        }
        ASSERT_TRUE(val1);
        ASSERT_TRUE(val2);
        ASSERT_EQ(senderSent, receiverReceived);
        ASSERT_EQ(senderSent, bytesToTransfer);
    }
}

TEST(Net, CloseCancel) {
    ILIAS_LOG_SET_LEVEL(ILIAS_TRACE_LEVEL);
    auto ctxt = IoContext::currentThread();
    UdpClient client(*ctxt, AF_INET);
    ASSERT_TRUE(client.bind("127.0.0.1:0"));

    auto read = [&]() -> IoTask<void> {
        std::byte buffer[1024];
        auto val = co_await client.recvfrom(makeBuffer(buffer));
        if (!val) {
            co_return Unexpected(val.error());
        }
        co_return {};
    };

    auto cancel = [&]() -> IoTask<void> {
        client.close();
        co_return {};
    };

    auto [val1, val2] = ilias_wait whenAll(read(), cancel());
    ASSERT_FALSE(val1);
    ASSERT_TRUE(val2);
}

TEST(Net, TestPoll) {
    auto ctxt = IoContext::currentThread();
    UdpClient client(*ctxt, AF_INET);
    UdpClient client2(*ctxt, AF_INET);
    ASSERT_TRUE(client.bind("127.0.0.1:0"));
    ASSERT_TRUE(client2.bind("127.0.0.1:0"));

    std::string hello = "hello world";
    auto endpoint = client2.localEndpoint();
    ASSERT_TRUE(endpoint);
    ASSERT_TRUE(client.sendto(makeBuffer(hello), *endpoint).wait());
    ASSERT_TRUE(client2.poll(PollEvent::In).wait());
}

#if !defined(ILIAS_NO_AF_UNIX)
TEST(Net, UnixTest) {
    auto ctxt = IoContext::currentThread();
    UnixClient client(*ctxt, SOCK_STREAM);
}
#endif // defined(ILIAS_NO_AF_UNIX)

auto main(int argc, char **argv) -> int {

#if defined(_WIN32)
    ::SetConsoleCP(65001);
    ::SetConsoleOutputCP(65001);
    std::setlocale(LC_ALL, ".utf-8");
#endif

    PlatformContext ctxt;
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}