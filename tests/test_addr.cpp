#include "../include/ilias.hpp"
#include <iostream>

using namespace ILIAS_NAMESPACE;

void printError(const char *str) {
    std::cout << str << std::endl;
    std::cout << SockError::fromErrno().message() << std::endl;
}

int main() {
    SockInitializer initializer;

    IPEndpoint endpoint3(IPAddress::fromHostname("www.baidu.com"), 80);
    IPEndpoint endpoint2(IPAddress::fromRaw(ADDR_ANY), 333);
    IPAddress ipv6("0:0:0:0:0:0:0:0");
    IPEndpoint endpoint1(ipv6, 333);

    std::cout << ipv6.toString() << std::endl;
    std::cout << endpoint1.toString() << std::endl;
    std::cout << endpoint2.toString() << std::endl;
    std::cout << endpoint3.toString() << std::endl;

    Socket socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (!socket.isValid()) {
        return 0;
    }
    if (!socket.bind(IPEndpoint("127.0.0.1", 1145))) {
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


    Socket client(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (!client.connect(IPEndpoint(IPAddress::fromHostname("www.baidu.com"), 80))) {
        printError("FAIL TO CONNECT");
    }
    if (!client.send("GET / HTTP/1.1\r\nHost: www.baidu.com\r\nConnection: close\r\n\r\n")) {
        printError("FAIL TO SEND");
    }
    while (true) {
        char buf[1024];
        auto n = client.recv(buf, sizeof(buf));
        if (n > 0) {
            std::cout.write(buf, n);
        }
        else {
            break;
        }
    }    
}