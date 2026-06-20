#include <ilias/platform.hpp>
#include <ilias/task.hpp>
#include <ilias/net.hpp>
#include <iostream>
#include <string>
#include <array>

using namespace ilias;

namespace {
    constexpr size_t requestBufferSize = 1024;
    constexpr size_t responseSize = 10 * 1024;
    constexpr std::string_view responseHeader =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 10240\r\n"
        "Connection: keep-alive\r\n"
        "Keep-Alive: timeout=5, max=1000\r\n"
        "\r\n";
    const std::array<std::byte, responseSize> responseBody {}; // 10K Empty response body
} // namespace

auto handle(TcpStream sock) -> Task<void> {
    std::array<std::byte, requestBufferSize> buffer {};
    while (true) {
        if (!co_await sock.read(buffer)) {
            break;
        }
        if (!co_await sock.writeAll(makeBuffer(responseHeader))) {
            break;
        }
        if (!co_await sock.writeAll(makeBuffer(responseBody))) {
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
