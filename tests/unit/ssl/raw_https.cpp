// TODO: rewrite the test
#include <ilias/platform.hpp>
#include <ilias/buffer.hpp>
#include <ilias/ssl.hpp>
#include <ilias/net.hpp>
#include <iostream>


using namespace ILIAS_NAMESPACE;

auto main() -> int {
    PlatformContext ctxt;
    SslContext sslCtxt;

    ::SetConsoleCP(65001);
    ::SetConsoleOutputCP(65001);
    std::setlocale(LC_ALL, ".utf-8");

    ILIAS_LOG_SET_LEVEL(ILIAS_TRACE_LEVEL);

    [&]() -> Task<> {
        auto info = co_await AddressInfo::fromHostnameAsync("www.baidu.com");
        auto target = info.value().addresses().at(0);

        TcpClient client(ctxt, target.family());
        if (auto val = co_await client.connect(IPEndpoint(target, 443)); !val) {
            std::cerr << "connect failed: " << val.error().toString() << '\n';
            std::cerr << "target: " << IPEndpoint(target, 443).toString() << '\n';
            co_return {};
        }
        SslClient<TcpClient> sslClient(sslCtxt, std::move(client));
        sslClient.setHostname("www.baidu.com");
        std::string_view requestHeaders = 
            "GET / HTTP/1.1\r\nHost: www.baidu.com\r\nConnection: close\r\n\r\n";
        auto val = co_await sslClient.write(makeBuffer(requestHeaders));
        if (!val) {
            std::cerr << "write failed: " << val.error().toString() << '\n';
            co_return {};
        }
        if (*val != requestHeaders.size()) {
            std::cerr << "write " << *val << " bytes, expected " << requestHeaders.size() << '\n';
            co_return {};
        }

        char buffer[1024];
        while (true) {
            auto val = co_await sslClient.read(makeBuffer(buffer));
            if (!val) {
                std::cerr << "read failed: " << val.error().toString() << '\n';
                co_return {};
            }
            if (*val == 0) {
                std::cerr << "read 0 bytes" << '\n';
                break;
            }
            std::cout << std::string_view(buffer, *val);
        }
        co_return {};
    }().wait();
}