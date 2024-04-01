#include "../include/ilias_resolver.hpp"
#include "../include/ilias_async.hpp"
#include "../include/ilias_loop.hpp"
#include "../include/ilias_co.hpp"
#include <iostream>

// #ifdef _WIN32
#if 0
    #include "../include/ilias_iocp.hpp"
    #include "../include/ilias_iocp.cpp"
#else
    #include "../include/ilias_poll.hpp"
#endif

using namespace ILIAS_NAMESPACE;

int main() {
    MiniEventLoop loop;
// #ifdef _WIN32
#if 0
    IOCPContext ctxt;
#else
    PollContext ctxt;
#endif

    loop.runTask([&]() -> Task<> {
        DnsQuery query("www.baidu.com", DnsQuery::A);
        std::vector<uint8_t> data;
        query.fillBuffer(0, data);

        UdpClient client(ctxt, AF_INET);
        if (auto ret = client.bind(IPEndpoint(IPAddress4::any(), 0)); !ret) {
            std::cout << ret.error().message() << std::endl;
            co_return;
        }
        std::cout << client.localEndpoint()->toString() << std::endl;
        auto ret = co_await client.sendto(data.data(), data.size(), IPEndpoint("114.114.114.114", 53));
        if (!ret) {
            std::cout << ret.error().message() << std::endl;
            co_return;
        }

        uint8_t recvdata[1024];
        auto nret = co_await client.recvfrom(recvdata, sizeof(recvdata));
        if (!nret) {
            std::cout << ret.error().message() << std::endl;
            co_return;
        }
        auto response = DnsResponse::parse(recvdata, nret->first);
        if (!response) {
            std::cout << "Parse error " << "Stop at" << response.error() << std::endl;
            co_return;
        }
        for (auto &addr : response->addresses()) {
            std::cout << addr.toString() << std::endl;
        }
        co_return;
    }());
}