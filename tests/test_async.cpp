#include "../include/ilias_async.hpp"
#include "../include/ilias_loop.hpp"
#include <iostream>

using namespace ILIAS_NAMESPACE;

#ifdef _WIN32
    #include "../include/ilias_iocp.hpp"
    #include "../include/ilias_iocp.cpp"
#else
    #include "../include/ilias_poll.hpp"
#endif

int main() {
    NativeEventLoop loop;

#ifdef _WIN32
    IOCPContext ctxt;
#else
    PollContext ctxt;
#endif

    loop.runTask([&]() -> Task<> {
        TcpClient client(ctxt, AF_INET);
        IPEndpoint endpoint(IPAddress4::fromHostname("www.baidu.com"), 80);
        if (auto result = co_await client.connect(endpoint); !result) {
            std::cout << result.error().message() << std::endl;
            co_return;
        }
        std::string request = "GET / HTTP/1.1\r\nHost: www.baidu.com\r\nConnection: close\r\n\r\n";
        if (auto result = co_await client.send(request.data(), request.size()); !result) {
            std::cout << result.error().message() << std::endl;
            co_return;
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
        co_return;
    }());
}