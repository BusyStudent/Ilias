#include "../include/ilias/networking.hpp"
#include <iostream>

using namespace ILIAS_NAMESPACE;

int main() {

#ifdef _WIN32
    ::SetConsoleCP(65001);
    ::SetConsoleOutputCP(65001);
    std::setlocale(LC_ALL, ".utf-8");
#endif
    PlatformIoContext ctxt;

    ctxt.runTask([&]() -> Task<> {
        TcpClient client(ctxt, AF_INET);
        ByteStream stream(std::move(client));
        IPEndpoint endpoint(IPAddress4::fromHostname("www.baidu.com"), 80);
        if (auto result = co_await stream.connect(endpoint); !result) {
            std::cout << result.error().toString() << std::endl;
            co_return Result<>();
        }
        std::string request = "GET / HTTP/1.1\r\nHost: www.baidu.com\r\nConnection: close\r\n\r\n";
        if (auto result = co_await stream.sendAll(request.data(), request.size()); !result) {
            std::cout << result.error().toString() << std::endl;
            co_return Result<>();
        }
        // Try get headers here
        while (auto line = co_await stream.getline("\r\n")) {
            std::cout << "lines: " << *line << std::endl;
            if (*line == "") {
                break;
            }
        }

        do {
            char buffer[1024];
            auto readed = co_await stream.recv(buffer, sizeof(buffer));
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