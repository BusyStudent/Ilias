#include <ilias/platform.hpp>
#include <ilias/task.hpp>
#include <ilias/net.hpp>
#include <iostream>

using namespace ilias;

auto handle(TcpClient sock) -> Task<void> {
    std::string_view header =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 10240\r\n"
        "Connection: keep-alive\r\n"
        "Keep-Alive: timeout=5, max=1000\r\n"
        "\r\n";

    std::byte buffer[1024 * 10] {};
    while (true) {
        if (!co_await sock.read(buffer)) {
            break;
        }
        if (!co_await sock.writeAll(makeBuffer(header))) {
            break;
        }
        if (!co_await sock.writeAll(buffer)) {
            break;
        }
    }
}

auto doAccept(TcpListener &listener) -> IoTask<void> {
    while (true) {
        auto sock = co_await listener.accept();
        if (!sock) {
            break;
        }
        auto &[stream, _] = *sock;
        auto other = stream.setOption(sockopt::TcpNoDelay(true));
        spawn(handle(std::move(stream)));
    }
    co_return {};
}

void ilias_main() {
    auto listener = (co_await TcpListener::bind("127.0.0.1:8081")).value(); 
    auto vector = std::vector<IoTask<void> > {};
    for (int i = 0; i < 32; ++i) {
        vector.emplace_back(doAccept(listener));
    }
    co_await whenAll(std::move(vector));
}