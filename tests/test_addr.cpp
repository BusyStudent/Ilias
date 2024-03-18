#include "../include/ilias.hpp"
#include <iostream>

using namespace ILIAS_NAMESPACE;

void printError(const char *str) {
    std::cout << str << std::endl;
    std::cout << SockError::fromErrno().message() << std::endl;
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
    if (!socket.bind(IPEndpoint(IPAddress4::loopback(), 1145))) {
        printError("FAIL TO BIND");
    }
    if (!socket.listen()) {
        printError("FAIL TO LISTEN");
    }
    auto local = socket.localEndpoint();
    std::cout << local.toString() << std::endl;
    std::cout << (local.address() == "127.0.0.1") << std::endl;
    std::cout << (local.port() == 1145) << std::endl;
    std::cout << (local == IPEndpoint("127.0.0.1", 1145)) << std::endl;
}