#include "../include/ilias_async.hpp"
#include "../include/ilias_loop.hpp"
#include <iostream>

using namespace ILIAS_NAMESPACE;

int main() {
    NativeEventLoop loop;
    IOContext ctxt;

    loop.runTask([&]() -> Task<> {
        // Socket socket(AF_INET, SOCK_STREAM, 0);
        // socket.setBlocking(false);
        // auto ok = co_await ctxt.asyncConnect(socket, IPEndpoint(IPAddress::fromHostname("www.baidu.com"), 80));
        // if (!ok) {
        //     co_return;
        // }
        // std::string_view msg = "GET / HTTP/1.1\r\nHost: www.baidu.com\r\nConnection: close\r\n\r\n";
        // auto n = co_await ctxt.asyncSend(socket, msg.data(), msg.size());
        // char buffer[1024];
        // do {
        //     n = co_await ctxt.asyncRecv(socket, buffer, sizeof(buffer));
        //     if (n > 0) {
        //         // process data
        //         std::cout.write(buffer, n);
        //     }
        //     if (n <= 0) {
        //         break;
        //     }
        // }
        // while (true);

        // TcpServer server;
        // if (auto ret = server.bind(IPEndpoint(IPAddress4::any(), 8080)); !ret) {
        //     ret.error().message();
        //     co_return;
        // }
        // auto result = co_await server.accept();
        // if (result) {
        //     auto &[socket, endpoint] = result.value();
        //     // socket.xxx();
        // }
        // else {
        //     result.error().message();
        // }
        co_return;
    }());
}