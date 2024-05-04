#include <iostream>

#include "../include/ilias.hpp"
#include "../include/ilias_inet.hpp"
#include "../include/ilias_async.hpp"
#include "../include/ilias_task.hpp"
#include "../include/ilias_resolver.hpp"


#ifdef _WIN32
    #include "../include/ilias_iocp.hpp"
    #include "../include/ilias_iocp.cpp"
#else
    #include "../include/ilias_poll.hpp"
#endif

using namespace ILIAS_NAMESPACE;
using namespace std::chrono_literals;

Ilias::Task<uint64_t> getData(Ilias::IoContext &ctxt) {
    const char *ntp_server = "ntp.aliyun.com";
    Resolver resolver(ctxt);
    auto ntp_server_ip = co_await resolver.resolve(ntp_server);
    if (!ntp_server_ip || ntp_server_ip->size() == 0) {
        std::cout << "Failed to get address of " << ntp_server << std::endl;
        co_return Unexpected(Error::Unknown);
    }

    Ilias::UdpClient client(ctxt, AF_INET);
    char ntp_pkt[48];
    memset(ntp_pkt, 0, sizeof(ntp_pkt));
    ntp_pkt[0] = 0x1B;
    IPEndpoint endpoint(ntp_server_ip.value()[0], 123);
    auto ret = co_await client.sendto(ntp_pkt, sizeof(ntp_pkt), endpoint);
    if (!ret) {
        std::cout << "Failed to send NTP packet to " << ntp_server << std::endl;
        co_return Unexpected(ret.error());
    }
    char buf[1024];
    auto recv = co_await Ilias::WhenAny(client.recvfrom(buf, sizeof(buf)), Sleep(5000ms));
    if (recv.index() == 1) {
        std::cout << "time out" << std::endl;
        co_return Unexpected(Error::TimedOut);
    }
    auto recvdata = std::get<0>(recv);
    if (!recvdata || recvdata.value().first != sizeof(ntp_pkt)) {
        std::cout << "error data" << std::endl;
        co_return Unexpected(Error::Unknown);
    }
    uint64_t *timestamp = (uint64_t*)&buf[40];
    time_t linux_time = ntohl(*timestamp) - 2208988800UL;
    co_return linux_time;
}

Ilias::Task<void> sleepTest() {
    // out put current timestamp ms
    std::cout << "current time: " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << std::endl;
    co_await Ilias::WhenAny(Sleep(1000ms), Sleep(2000ms), Sleep(1500ms), Sleep(500ms));
    std::cout << "end time: " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << std::endl;
    co_return Result<void>();
}

int main(int argc, char **argv) {

#if defined(_WIN32)
    Ilias::IOCPContext ctxt;
#else
    Ilias::PollContext ctxt;
#endif
    auto t = ilias_wait getData(ctxt);
    if (t) {
        std::cout << "time: " << t.value() << std::endl;
    } else {
        std::cout << "error: " << t.error().message() << std::endl;
    }
    ilias_wait sleepTest();
    return 0;
}