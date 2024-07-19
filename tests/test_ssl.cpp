#include "../include/ilias/networking.hpp"
#include "../include/ilias/ssl.hpp"
#include <iostream>

using namespace ILIAS_NAMESPACE;

int main() {
#ifdef _WIN32
    ::SetConsoleCP(65001);
    ::SetConsoleOutputCP(65001);
    std::setlocale(LC_ALL, ".utf-8");
#endif
    PlatformIoContext ctxt;
    SslContext sslCtxt;

    ctxt.runTask([&]() -> Task<> {
        TcpClient tcpClient = TcpClient(ctxt, AF_INET);
        SslClient<> client(sslCtxt, std::move(tcpClient));
        if constexpr (SslSniExtension<SslClient<> >) {
            client.setHostname("www.baidu.com");
        }
        if constexpr (SslAlpnExtension<SslClient<> >) {
            const char *alpns [] {
                "http/1.1"
            };
            client.setAlpn(alpns);
        }

        IPEndpoint endpoint(IPAddress4::fromHostname("www.baidu.com"), 443);
        if (auto result = co_await client.connect(endpoint); !result) {
            std::cout << result.error().toString() << std::endl;
            co_return Result<>();
        }

        if constexpr (SslAlpnExtension<SslClient<> >) {
            std::cout << "ALPN Selected: " << client.alpnSelected() << std::endl;
        }

        std::string request = "GET / HTTP/1.1\r\nHost: www.baidu.com\r\nConnection: close\r\n\r\n";
        if (auto result = co_await client.send(request.data(), request.size()); !result) {
            std::cout << result.error().toString() << std::endl;
            co_return Result<>();
        }
        do {
            char buffer[1024];
            auto readed = co_await client.recv(buffer, sizeof(buffer));
            if (!readed) {
                break;
            }
            if (*readed == 0) {
                break;
            }
            std::cout.write(buffer, *readed);
        }
        while (true);
        co_return Result<>();
    }());
}