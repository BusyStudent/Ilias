#include <ilias/net/sockfd.hpp>
#include <ilias/net/system.hpp>
#include <ilias/buffer.hpp>
#include <ilias/log.hpp>
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
        ASSERT_TRUE(tcpClient.send(makeBuffer(content)).value() == content.size());

        char buffer[1024] {0};
        auto num = peer.recv(makeBuffer(buffer)).value();

        ASSERT_EQ(num, content.size());
        ASSERT_EQ(std::string_view(buffer, num), content);

        // Server -> Client
        content = randomGen();
        ASSERT_TRUE(peer.send(makeBuffer(content)).value() == content.size());

        num = tcpClient.recv(makeBuffer(buffer)).value();

        ASSERT_EQ(num, content.size());
        ASSERT_EQ(std::string_view(buffer, num), content);
    }
}

TEST(Tcp, Sockopt) {
    Socket tcpClient(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    ASSERT_TRUE(tcpClient.isValid());
    ASSERT_TRUE(tcpClient.setOption(sockopt::ReuseAddress(true)));
    ASSERT_TRUE(tcpClient.getOption<sockopt::ReuseAddress>().value());

#if !defined(ILIAS_NO_FORMAT)
    ILIAS_INFO("Test", "{}", tcpClient.getOption<sockopt::RecvBufSize>().value());
    ILIAS_INFO("Test", "{}", tcpClient.getOption<sockopt::SendBufSize>().value());
    ILIAS_INFO("Test", "{}", tcpClient.getOption<sockopt::ReuseAddress>().value());
    ILIAS_INFO("Test", "{}", tcpClient.getOption<sockopt::KeepAlive>().value());
    ILIAS_INFO("Test", "{}", tcpClient.getOption<sockopt::Linger>().value());

    ILIAS_INFO("Test", "{}", tcpClient.getOption<sockopt::TcpKeepCnt>().value());
    ILIAS_INFO("Test", "{}", tcpClient.getOption<sockopt::TcpKeepIdle>().value());
    ILIAS_INFO("Test", "{}", tcpClient.getOption<sockopt::TcpKeepIntvl>().value());
#endif

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
        ASSERT_TRUE(udpClient.sendto(makeBuffer(content), 0, udpServer.localEndpoint().value()).value() == content.size());
        char buffer[1024] {0};
        auto num = udpServer.recvfrom(makeBuffer(buffer), 0, nullptr).value();

        ASSERT_EQ(num, content.size());
        ASSERT_EQ(std::string_view(buffer, num), content);

        // Server -> Client
        content = randomGen();
        ASSERT_TRUE(udpServer.sendto(makeBuffer(content), 0, udpClient.localEndpoint().value()).value() == content.size());

        num = udpClient.recvfrom(makeBuffer(buffer), 0, nullptr).value();
        ASSERT_EQ(num, content.size());
        ASSERT_EQ(std::string_view(buffer, num), content);
    }
}

TEST(Udp, Sockopt) {
    Socket udpClient(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    ASSERT_TRUE(udpClient.isValid());
    ASSERT_TRUE(udpClient.setOption<sockopt::ReuseAddress>(true));
    ASSERT_TRUE(udpClient.getOption<sockopt::ReuseAddress>().value());

    std::cout << udpClient.getOption<sockopt::RecvBufSize>().value_or(0) << std::endl;
}

auto main(int argc, char **argv) -> int {
    SockInitializer init;
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}