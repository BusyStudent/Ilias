#include <ilias/net/sockfd.hpp>
#include <ilias/net/system.hpp>
#include <ilias/buffer.hpp>
#include <gtest/gtest.h>
#include <random>


using namespace ILIAS_NAMESPACE;

auto randomGen() -> std::string {
    std::srand(std::time(nullptr));
    std::string result;
    size_t len = std::rand() % 1022 + 1; //< Make sure at least one byte is sent
    for (size_t i = 0; i < len; ++i) {
        result += static_cast<char>(std::rand() % 256);
    }
    return result;
}

TEST(Tcp, Sending) {
    Socket tcpClient(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    Socket tcpListener(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    ASSERT_TRUE(tcpClient.isValid());
    ASSERT_TRUE(tcpListener.isValid());

    ASSERT_TRUE(tcpListener.bind("127.0.0.1:0").has_value());
    ASSERT_TRUE(tcpListener.listen().has_value());

    ASSERT_TRUE(tcpClient.connect(tcpListener.localEndpoint().value()).has_value());

    auto acceptResult = tcpListener.accept();
    ASSERT_TRUE(acceptResult.has_value());

    auto &[peer, _] = acceptResult.value();

    for (int i = 0; i < 1000; ++i) {
        // Client -> Server
        std::string content = randomGen();
        ASSERT_TRUE(tcpClient.send(as_buffer(content)).value() == content.size());

        char buffer[1024] {0};
        auto num = peer.recv(as_writable_buffer(buffer)).value();

        ASSERT_EQ(num, content.size());
        ASSERT_EQ(std::string_view(buffer, num), content);

        // Server -> Client
        content = randomGen();
        ASSERT_TRUE(peer.send(as_buffer(content)).value() == content.size());

        num = tcpClient.recv(as_writable_buffer(buffer)).value();

        ASSERT_EQ(num, content.size());
        ASSERT_EQ(std::string_view(buffer, num), content);
    }
}

TEST(Udp, Sending) {
    Socket udpClient(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    Socket udpServer(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    ASSERT_TRUE(udpClient.isValid());
    ASSERT_TRUE(udpServer.isValid());

    ASSERT_TRUE(udpServer.bind("127.0.0.1:0").has_value());
    ASSERT_TRUE(udpClient.bind("127.0.0.1:0").has_value());

    for (int i = 0; i < 1000; ++i) {
        // Client -> Server
        std::string content = randomGen();
        ASSERT_TRUE(udpClient.sendto(as_buffer(content), 0, udpServer.localEndpoint().value()).value() == content.size());
        char buffer[1024] {0};
        auto num = udpServer.recvfrom(as_writable_buffer(buffer), 0, nullptr).value();

        ASSERT_EQ(num, content.size());
        ASSERT_EQ(std::string_view(buffer, num), content);

        // Server -> Client
        content = randomGen();
        ASSERT_TRUE(udpServer.sendto(as_buffer(content), 0, udpClient.localEndpoint().value()).value() == content.size());

        num = udpClient.recvfrom(as_writable_buffer(buffer), 0, nullptr).value();
        ASSERT_EQ(num, content.size());
        ASSERT_EQ(std::string_view(buffer, num), content);
    }
}

auto main(int argc, char **argv) -> int {
    SockInitializer init;
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}