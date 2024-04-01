#include "../include/ilias_inet.hpp"
#include <iostream>

using namespace ILIAS_NAMESPACE;

void printError(const char *where, Error err) {
    std::cerr << where << ": " << err.message() << std::endl;
    ::exit(1);
}

int main() {
    SockInitializer initializer;

    IPEndpoint endpoint3(IPAddress4::fromHostname("www.baidu.com"), 80);
    IPEndpoint endpoint2(IPAddress4::any(), 333);
    IPAddress ipv6(IPAddress6::loopback());
    IPEndpoint endpoint1(ipv6, 333);
    IPEndpoint endpoint4("114.123.111.1:1145");

    std::cout << ipv6.toString() << std::endl;
    std::cout << endpoint1.toString() << std::endl;
    std::cout << endpoint2.toString() << std::endl;
    std::cout << endpoint3.toString() << std::endl;

    Socket socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (!socket.isValid()) {
        return 0;
    }
    if (auto ret = socket.bind(IPEndpoint()); !ret) {
        printError("FAIL TO BIND", ret.error());
    }
    if (auto ret = socket.listen(); !ret) {
        printError("FAIL TO LISTEN", ret.error());
    }
    auto local = socket.localEndpoint().value();
    std::cout << local.toString() << std::endl;
    std::cout << (local.address() == "127.0.0.1") << std::endl;
    std::cout << (local.port() == 1145) << std::endl;
    std::cout << (local == IPEndpoint("127.0.0.1", 1145)) << std::endl;
}